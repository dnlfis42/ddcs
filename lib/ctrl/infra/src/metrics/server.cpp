#include "ddcs/ctrl/infra/metrics/server.hpp"

#include "ddcs/common/throw_errno.hpp"
#include "ddcs/ctrl/port/metrics/inbound.hpp"
#include "ddcs/io/reactor.hpp"
#include "ddcs/logger/log.hpp"

#include <string>
#include <utility>

#include <cerrno>
#include <cstddef>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>

namespace ddcs::ctrl::infra::metrics {

namespace {

constexpr std::size_t pool_chunk{8};
constexpr std::uint32_t read_interest{EPOLLIN | EPOLLET};
constexpr std::uint32_t write_interest{EPOLLOUT | EPOLLET};

// Prometheus exposition(text/plain) 본문을 HTTP/1.0 스타일 응답으로 감싼다(Connection: close).
std::string http_ok(std::string const& body) {
    std::string r;
    r += "HTTP/1.1 200 OK\r\n";
    r += "Content-Type: text/plain; version=0.0.4; charset=utf-8\r\n";
    r += "Content-Length: ";
    r += std::to_string(body.size());
    r += "\r\nConnection: close\r\n\r\n";
    r += body;
    return r;
}

} // namespace

Server::Server(io::Reactor& reactor, Inbound& provider, std::uint16_t port, int backlog)
    : reactor_{reactor}, provider_{provider}, listen_port_{port}, backlog_{backlog},
      pool_{common::make_pool<Connection>(0, pool_chunk)} {}

Server::~Server() { conns_.clear(); }

void Server::start() {
    listen_fd_.reset(::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
    if (!listen_fd_) {
        common::throw_errno(errno, "metrics socket");
    }
    int const yes{1};
    if (::setsockopt(listen_fd_.get(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        common::throw_errno(errno, "metrics setsockopt SO_REUSEADDR");
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(listen_port_);
    if (::bind(listen_fd_.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        common::throw_errno(errno, "metrics bind");
    }
    if (::listen(listen_fd_.get(), backlog_) < 0) {
        common::throw_errno(errno, "metrics listen");
    }
    if (!reactor_.add(listen_fd_.get(), read_interest, this)) {
        common::throw_errno(errno, "metrics epoll_ctl ADD listen_fd");
    }
    LOG_INFO("metrics.server.start", ddcs::logger::kv("port", port()));
}

std::uint16_t Server::port() const {
    sockaddr_in addr{};
    socklen_t len{sizeof(addr)};
    if (::getsockname(listen_fd_.get(), reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
        return 0;
    }
    return ntohs(addr.sin_port);
}

void Server::on_io(std::uint32_t events) {
    if ((events & (EPOLLERR | EPOLLHUP)) != 0u) {
        LOG_ERROR("metrics.listener_error", ddcs::logger::kv("events", events));
        return;
    }
    if ((events & EPOLLIN) != 0u) {
        accept_loop();
    }
    reap();
}

void Server::accept_loop() {
    for (;;) {
        common::Fd fd{::accept4(listen_fd_.get(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC)};
        if (!fd) {
            int const err = errno;
            if (err == EAGAIN || err == EWOULDBLOCK) {
                return;
            }
            if (err == EINTR || err == ECONNABORTED) {
                continue;
            }
            return; // EMFILE 등 - metrics 는 best-effort (shed 안 함)
        }
        auto conn = pool_.acquire();
        conn->set_server(*this);
        int const cfd = fd.get();
        conn->assign(std::move(fd));
        auto [it, inserted] = conns_.try_emplace(cfd, std::move(conn));
        if (!inserted) {
            continue; // fd 중복(있을 수 없음) - 핸들 드롭
        }
        Connection* const p = it->second.get();
        if (reactor_.add(p->fd(), read_interest, p)) {
            p->enter_epoll();
        } else {
            conns_.erase(cfd); // 등록 실패 -> 드롭(핸들 -> reset -> fd close)
        }
    }
}

void Server::on_event(Connection& conn, std::uint32_t events) {
    dispatch_event(conn, events);
    reap();
}

void Server::dispatch_event(Connection& conn, std::uint32_t events) {
    if (conn.state() == Connection::State::done) {
        return; // 이미 reap 예약
    }
    if ((events & (EPOLLERR | EPOLLHUP)) != 0u) {
        schedule_close(conn);
        return;
    }

    if (conn.state() == Connection::State::reading) {
        if ((events & EPOLLIN) == 0u) {
            return;
        }
        auto const r = conn.receive();
        if (r == Connection::IoResult::error) {
            schedule_close(conn);
            return;
        }
        if (!conn.request_complete()) {
            if (r == Connection::IoResult::peer_closed) {
                schedule_close(conn); // 미완 요청에 FIN -> 포기
            }
            return; // would_block: 더 읽기
        }
        respond(conn); // request 완료 -> 응답 빌드(state=writing)
    }

    if (conn.state() == Connection::State::writing) {
        auto const r = conn.transmit();
        if (r == Connection::IoResult::error) {
            schedule_close(conn);
            return;
        }
        if (conn.state() == Connection::State::done) {
            schedule_close(conn); // 응답 완송 -> close
            return;
        }
        (void)reactor_.mod(conn.fd(), write_interest); // would_block -> EPOLLOUT 대기
    }
}

void Server::respond(Connection& conn) { conn.begin_response(http_ok(provider_.scrape())); }

void Server::schedule_close(Connection& conn) { pending_close_.push_back(conn.fd()); }

void Server::reap() {
    // 인덱스 순회: 재진입 push 안전(반복자 무효화 없음).
    for (std::size_t i = 0; i < pending_close_.size(); ++i) {
        int const fd = pending_close_[i];
        auto* conn = find(fd);
        if (conn == nullptr) {
            continue;
        }
        if (conn->in_epoll()) {
            reactor_.del(conn->fd());
            conn->leave_epoll();
        }
        conns_.erase(fd); // 핸들 drop -> reset() -> fd close
    }
    pending_close_.clear();
}

Connection* Server::find(int fd) {
    auto const it = conns_.find(fd);
    return it == conns_.end() ? nullptr : it->second.get();
}

} // namespace ddcs::ctrl::infra::metrics
