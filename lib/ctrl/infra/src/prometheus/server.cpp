#include "ddcs/ctrl/infra/prometheus/server.hpp"

#include "ddcs/ctrl/app/metrics/port/prometheus_source.hpp"
#include "ddcs/io/fd.hpp"
#include "ddcs/io/reactor.hpp"
#include "ddcs/logger/event.hpp"
#include "ddcs/net/socket.hpp"

#include <cerrno>
#include <cstddef>
#include <string>
#include <utility>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

namespace ddcs::ctrl::infra::prometheus {

namespace {

constexpr std::size_t pool_chunk = 8;
constexpr io::ChannelEvents read_interest =
    io::ChannelEvents::readable | io::ChannelEvents::edge_triggered;
constexpr io::ChannelEvents write_interest =
    io::ChannelEvents::writable | io::ChannelEvents::edge_triggered;

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

} // namespace

Server::Server(
    io::Reactor& reactor, port::PrometheusSource& source, std::uint16_t listen_port, int backlog
)
    : reactor_(reactor),
      source_(source),
      listen_port_(listen_port),
      backlog_(backlog),
      connection_pool_(common::ObjectPool<Connection>::create<pool_chunk>()) {}

Server::~Server() {
    close();
}

io::SysResult Server::init() noexcept {
    if (listen_channel_.valid()) {
        return io::SysResult::fail(); // 이중 init
    }

    // init 실패는 여기서 알리지 않는다. Acceptor::init 과 같이 errno 를 얹어 올려보내고,
    // 부팅을 세우는 쪽이 한 번만 알린다.
    io::Fd fd{::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)};
    if (!fd.valid()) {
        return io::SysResult::fail(errno);
    }

    int const yes = 1;
    if (::setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        return io::SysResult::fail(errno);
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(listen_port_);
    if (::bind(fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        return io::SysResult::fail(errno);
    }

    if (::listen(fd.get(), backlog_) < 0) {
        return io::SysResult::fail(errno);
    }

    auto const bound_port = net::bound_port(fd.get());
    if (!bound_port) {
        return io::SysResult::fail(errno); // getsockname 실패의 errno가 남아 있다
    }
    bound_port_ = *bound_port;
    listen_channel_.init(std::move(fd), read_interest, *this);

    return io::SysResult::success();
}

io::SysResult Server::start() {
    if (listen_channel_.registered()) {
        return io::SysResult::success();
    }
    if (!listen_channel_.valid()) {
        return io::SysResult::fail(); // init 전 start
    }

    if (auto const result = reactor_.add(listen_channel_); !result) {
        return result;
    }

    LOG_PROMETHEUS_LISTEN(bound_port_);
    return io::SysResult::success();
}

void Server::stop() noexcept {
    if (!listen_channel_.registered()) {
        return;
    }

    reactor_.remove(listen_channel_);
    for (auto& [fd, conn] : connections_) {
        if (conn->channel().registered()) {
            reactor_.remove(conn->channel());
        }
    }
    connections_.clear(); // 핸들 drop 시 reset 후 fd close
    reap_queue_.clear();
}

void Server::close() noexcept {
    if (!listen_channel_.valid()) {
        return;
    }

    stop();
    listen_channel_.close();
    bound_port_ = 0;
}

void Server::on_ready(io::Channel& channel, io::ChannelEvents events) {
    if (&channel != &listen_channel_) {
        return;
    }

    if (contains(events, io::ChannelEvents::error) || contains(events, io::ChannelEvents::hangup)) {
        LOG_PROMETHEUS_LISTEN_FAIL(io::to_underlying(events));
        close(); // 리스닝 fd 고장
        return;
    }

    if (contains(events, io::ChannelEvents::readable)) {
        accept_connections();
    }
    reap_scheduled();
}

void Server::accept_connections() {
    for (;;) {
        io::Fd fd{::accept4(listen_channel_.fd(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC)};
        if (!fd.valid()) {
            int const err = errno;
            if (err == EAGAIN || err == EWOULDBLOCK) {
                return;
            }
            if (err == EINTR || err == ECONNABORTED) {
                continue;
            }
            // EMFILE 등. 스크레이프는 best-effort(shed 안 함)지만, 조용히 멈추면
            // 메트릭이 꺼진 이유가 어디에도 안 남는다.
            LOG_TRANSPORT_ACCEPT_FAIL(err);
            return;
        }

        auto conn = connection_pool_.acquire();
        int const cfd = fd.get();
        conn->init(*this, std::move(fd), read_interest);

        auto const [it, inserted] = connections_.try_emplace(cfd, std::move(conn));
        if (!inserted) {
            continue; // fd 중복(있을 수 없음). 핸들 드롭
        }

        Connection* const p = it->second.get();
        if (auto const result = reactor_.add(p->channel()); !result) {
            LOG_TRANSPORT_REACTOR_ADD_FAIL(result.err);
            connections_.erase(cfd); // 등록 실패 시 드롭(핸들 drop 시 reset 후 fd close)
        }
    }
}

void Server::on_connection_event(Connection& conn, io::ChannelEvents events) {
    do {
        if (contains(events, io::ChannelEvents::error) ||
            contains(events, io::ChannelEvents::hangup)) {
            schedule_reap(conn);
            break;
        }

        if (conn.state() == Connection::State::reading) {
            if (!contains(events, io::ChannelEvents::readable)) {
                break;
            }
            auto const r = conn.receive();
            if (r.code == net::ReceiveResult::Code::error) {
                LOG_TRANSPORT_RECEIVE_FAIL(r.err);
                schedule_reap(conn);
                break;
            }
            if (!conn.request_complete()) {
                if (r.code == net::ReceiveResult::Code::peer_closed) {
                    schedule_reap(conn); // 미완 요청에 FIN 오면 포기
                }
                break; // would_block: 더 읽기
            }

            conn.begin_response(http_ok(source_.scrape()));
        }

        if (conn.state() == Connection::State::writing) {
            auto const r = conn.transmit();
            if (r.code == net::TransmitResult::Code::error) {
                LOG_TRANSPORT_SEND_FAIL(r.err);
                schedule_reap(conn);
                break;
            }
            if (r.code == net::TransmitResult::Code::drained) {
                schedule_reap(conn); // 응답 완송 후 close
                break;
            }
            // would_block: writable 대기. 관심 전환 실패 시 좀비 방지를 위해 폐기
            if (auto const result = reactor_.modify(conn.channel(), write_interest); !result) {
                LOG_TRANSPORT_REACTOR_MODIFY_FAIL(result.err);
                schedule_reap(conn);
            }
        }
    } while (false);

    reap_scheduled();
}

void Server::schedule_reap(Connection& conn) {
    reap_queue_.push_back(conn.fd());
}

void Server::reap_scheduled() {
    // 인덱스 순회: 재진입 push 안전(반복자 무효화 없음).
    for (std::size_t i = 0; i < reap_queue_.size(); ++i) {
        auto const it = connections_.find(reap_queue_[i]);
        if (it == connections_.end()) {
            continue;
        }

        Connection& conn = *it->second;
        if (conn.channel().registered()) {
            reactor_.remove(conn.channel());
        }
        connections_.erase(it); // 핸들 drop 시 reset() 후 fd close
    }
    reap_queue_.clear();
}

} // namespace ddcs::ctrl::infra::prometheus
