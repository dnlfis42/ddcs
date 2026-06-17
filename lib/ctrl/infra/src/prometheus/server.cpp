#include "ddcs/ctrl/infra/prometheus/server.hpp"

#include "ddcs/common/fd.hpp"
#include "ddcs/ctrl/app/metrics/port/metrics_source.hpp"
#include "ddcs/io/reactor.hpp"
#include "ddcs/logger/log.hpp"

#include <cerrno>
#include <cstddef>
#include <string>
#include <utility>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

namespace ddcs::ctrl::infra::prometheus {

namespace {

constexpr std::size_t pool_chunk{8};
constexpr io::ChannelEvents read_interest{
    io::ChannelEvents::readable | io::ChannelEvents::edge_triggered
};
constexpr io::ChannelEvents write_interest{
    io::ChannelEvents::writable | io::ChannelEvents::edge_triggered
};

// Prometheus exposition(text/plain) 본문을 HTTP 응답으로 감싼다(Connection: close).
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

[[nodiscard]] std::uint16_t query_bound_port(int fd) noexcept {
    sockaddr_in addr{};
    socklen_t len{sizeof(addr)};
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
        return 0;
    }
    return ntohs(addr.sin_port);
}

} // namespace

Server::Server(
    io::Reactor& reactor, port::MetricsSource& source, std::uint16_t listen_port, int backlog
)
    : reactor_{reactor},
      source_{source},
      listen_port_{listen_port},
      backlog_{backlog},
      pool_{common::ObjectPool<Connection>::create<pool_chunk>()} {}

Server::~Server() {
    close();
}

bool Server::init() noexcept {
    if (state_ != State::idle) {
        return false;
    }

    common::Fd fd{::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)};
    if (!fd.valid()) {
        LOG_WARN("prometheus.socket_fail", logger::kv("errno", errno));
        return false;
    }

    int const yes{1};
    if (::setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        LOG_WARN("prometheus.setsockopt_fail", logger::kv("errno", errno));
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(listen_port_);
    if (::bind(fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG_WARN(
            "prometheus.bind_fail", logger::kv("port", listen_port_), logger::kv("errno", errno)
        );
        return false;
    }

    if (::listen(fd.get(), backlog_) < 0) {
        LOG_WARN("prometheus.listen_fail", logger::kv("errno", errno));
        return false;
    }

    bound_port_ = query_bound_port(fd.get());
    if (!listen_channel_.init(std::move(fd), read_interest, *this)) {
        return false;
    }

    state_ = State::ready;
    return true;
}

bool Server::start() {
    if (state_ == State::active) {
        return true;
    }
    if (state_ != State::ready) {
        return false;
    }
    if (!reactor_.add(listen_channel_)) {
        return false;
    }

    state_ = State::active;
    LOG_INFO("prometheus.server.start", logger::kv("port", bound_port_));
    return true;
}

void Server::stop() noexcept {
    if (state_ != State::active) {
        return;
    }

    reactor_.remove(listen_channel_);
    for (auto& [fd, conn] : connections_) {
        if (conn->channel().registered()) {
            reactor_.remove(conn->channel());
        }
    }
    connections_.clear(); // 핸들 drop 시 reset 후 fd close
    pending_close_.clear();
    state_ = State::ready;
}

void Server::close() noexcept {
    if (state_ == State::idle) {
        return;
    }

    stop();
    listen_channel_.reset();
    bound_port_ = 0;
    state_ = State::idle;
}

void Server::on_ready(io::Channel& channel, io::ChannelEvents events) {
    if (&channel != &listen_channel_) {
        return;
    }

    if (contains(events, io::ChannelEvents::error) || contains(events, io::ChannelEvents::hangup)) {
        LOG_ERROR(
            "prometheus.listener_error", logger::kv("events", static_cast<std::uint32_t>(events))
        );
        return;
    }

    if (contains(events, io::ChannelEvents::readable)) {
        drain_accepts();
    }
    reap();
}

void Server::drain_accepts() {
    for (;;) {
        common::Fd fd{
            ::accept4(listen_channel_.fd(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC)
        };
        if (!fd.valid()) {
            int const err = errno;
            if (err == EAGAIN || err == EWOULDBLOCK) {
                return;
            }
            if (err == EINTR || err == ECONNABORTED) {
                continue;
            }
            return; // EMFILE 등. 스크레이프는 best-effort (shed 안 함)
        }

        auto conn = pool_.acquire();
        int const cfd = fd.get();
        if (!conn->assign(*this, std::move(fd), read_interest)) {
            continue; // 방어. 핸들 드롭 시 reset
        }

        auto const [it, inserted] = connections_.try_emplace(cfd, std::move(conn));
        if (!inserted) {
            continue; // fd 중복(있을 수 없음). 핸들 드롭
        }

        Connection* const p = it->second.get();
        if (!reactor_.add(p->channel())) {
            connections_.erase(cfd); // 등록 실패 시 드롭(핸들 drop 시 reset 후 fd close)
        }
    }
}

void Server::handle_connection_ready(Connection& conn, io::ChannelEvents events) {
    dispatch(conn, events);
    reap();
}

void Server::dispatch(Connection& conn, io::ChannelEvents events) {
    if (conn.state() == Connection::State::done) {
        return; // 이미 reap 예약
    }

    if (contains(events, io::ChannelEvents::error) || contains(events, io::ChannelEvents::hangup)) {
        schedule_close(conn);
        return;
    }

    if (conn.state() == Connection::State::reading) {
        if (!contains(events, io::ChannelEvents::readable)) {
            return;
        }
        auto const r = conn.receive();
        if (r == Connection::IoResult::error) {
            schedule_close(conn);
            return;
        }
        if (!conn.request_complete()) {
            if (r == Connection::IoResult::peer_closed) {
                schedule_close(conn); // 미완 요청에 FIN 오면 포기
            }
            return; // would_block: 더 읽기
        }
        respond(conn); // request 완료 시 응답 빌드(state=writing)
    }

    if (conn.state() == Connection::State::writing) {
        auto const r = conn.transmit();
        if (r == Connection::IoResult::error) {
            schedule_close(conn);
            return;
        }
        if (conn.state() == Connection::State::done) {
            schedule_close(conn); // 응답 완송 후 close
            return;
        }
        (void)reactor_.modify(conn.channel(), write_interest); // would_block 시 writable 대기
    }
}

void Server::respond(Connection& conn) {
    conn.begin_response(http_ok(source_.scrape()));
}

void Server::schedule_close(Connection& conn) {
    pending_close_.push_back(conn.fd());
}

void Server::reap() {
    // 인덱스 순회: 재진입 push 안전(반복자 무효화 없음).
    for (std::size_t i = 0; i < pending_close_.size(); ++i) {
        int const fd = pending_close_[i];
        auto* conn = find(fd);
        if (conn == nullptr) {
            continue;
        }

        if (conn->channel().registered()) {
            reactor_.remove(conn->channel());
        }
        connections_.erase(fd); // 핸들 drop 시 reset() 후 fd close
    }
    pending_close_.clear();
}

Connection* Server::find(int fd) {
    auto const it = connections_.find(fd);
    return it == connections_.end() ? nullptr : it->second.get();
}

} // namespace ddcs::ctrl::infra::prometheus
