#include "ddcs/ctrl/infra/transport/acceptor.hpp"

#include "ddcs/common/throw_errno.hpp"
#include "ddcs/ctrl/infra/transport/connection_coordinator.hpp"
#include "ddcs/ctrl/infra/transport/endpoint.hpp"
#include "ddcs/io/reactor.hpp"
#include "ddcs/logger/log.hpp"

#include <utility>

#include <cerrno>
#include <cstring>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace ddcs::ctrl::infra::transport {

namespace {

[[nodiscard]]
Endpoint from_sockaddr(sockaddr const& sa, socklen_t len) noexcept {
    Endpoint ep{};
    if (sa.sa_family == AF_INET && len >= static_cast<socklen_t>(sizeof(sockaddr_in))) {
        sockaddr_in s4{};
        std::memcpy(&s4, &sa, sizeof(s4));
        ep.family = Endpoint::Family::v4;
        ep.port = ntohs(s4.sin_port);
        std::memcpy(ep.addr.data(), &s4.sin_addr.s_addr, 4);
    } else if (sa.sa_family == AF_INET6 && len >= static_cast<socklen_t>(sizeof(sockaddr_in6))) {
        sockaddr_in6 s6{};
        std::memcpy(&s6, &sa, sizeof(s6));
        ep.family = Endpoint::Family::v6;
        ep.port = ntohs(s6.sin6_port);
        std::memcpy(ep.addr.data(), &s6.sin6_addr, 16);
    }
    return ep;
}

} // namespace

Acceptor::Acceptor(
    io::Reactor& reactor, ConnectionCoordinator& coordinator, std::uint16_t listen_port, int accept_backlog
)
    : reactor_{reactor}, coordinator_{coordinator}, listen_port_{listen_port}, accept_backlog_{accept_backlog} {}

void Acceptor::start() {
    listen_fd_.reset(::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
    if (!listen_fd_) {
        common::throw_errno(errno, "socket");
    }

    int const yes{1};
    if (::setsockopt(listen_fd_.get(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        common::throw_errno(errno, "setsockopt SO_REUSEADDR");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(listen_port_);
    if (::bind(listen_fd_.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        common::throw_errno(errno, "bind");
    }

    if (::listen(listen_fd_.get(), accept_backlog_) < 0) {
        common::throw_errno(errno, "listen");
    }

    if (!reactor_.add(listen_fd_.get(), EPOLLIN | EPOLLET, this)) {
        common::throw_errno(errno, "epoll_ctl ADD listen_fd");
    }

    // reserve-fd: EMFILE/ENFILE 시 spare 를 풀어 kernel accept 큐를 shed 한다.
    accept_spare_fd_.reset(::open("/dev/null", O_RDONLY | O_CLOEXEC));
    if (!accept_spare_fd_) {
        common::throw_errno(errno, "open /dev/null (accept spare)");
    }

    LOG_INFO("transport.acceptor.start", ddcs::logger::kv("port", port()));
}

std::uint16_t Acceptor::port() const {
    sockaddr_in addr{};
    socklen_t len{sizeof(addr)};
    if (::getsockname(listen_fd_.get(), reinterpret_cast<sockaddr*>(&addr), &len) < 0) {
        return 0;
    }
    return ntohs(addr.sin_port);
}

void Acceptor::on_io(std::uint32_t events) {
    if ((events & (EPOLLERR | EPOLLHUP)) != 0u) {
        LOG_ERROR("transport.listener_error", ddcs::logger::kv("events", events));
        reactor_.stop();
        return;
    }
    if ((events & EPOLLIN) != 0u) {
        accept_loop();
    }
}

void Acceptor::accept_loop() {
    for (;;) {
        sockaddr_storage peer_addr{};
        socklen_t peer_addr_len{sizeof(peer_addr)};
        common::Fd conn_fd{::accept4(
            listen_fd_.get(), reinterpret_cast<sockaddr*>(&peer_addr), &peer_addr_len, SOCK_NONBLOCK | SOCK_CLOEXEC
        )};

        if (!conn_fd) {
            int const err = errno;
            if (err == EAGAIN || err == EWOULDBLOCK) {
                return;
            }
            if (err == EINTR || err == ECONNABORTED) {
                continue;
            }
            if (err == EMFILE || err == ENFILE) {
                LOG_WARN("transport.accept.fd_exhausted", ddcs::logger::kv("errno", err));
                // reserve-fd: spare 1개를 풀어 accept->즉시 close 로 kernel 큐 1건 shed, spare 재확보 후 재시도.
                accept_spare_fd_.reset();
                common::Fd reject{::accept4(listen_fd_.get(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC)};
                bool const drained = static_cast<bool>(reject); // dtor 가 close -> 클라이언트 정리
                accept_spare_fd_.reset(::open("/dev/null", O_RDONLY | O_CLOEXEC));
                if (!drained) {
                    return; // 큐 비었거나 더 못 shed
                }
                continue;
            }
            return; // 기타 unknown errno
        }

        // 소켓 옵션 (best-effort, 에러 무시):
        // - TCP_NODELAY=1: Nagle 비활성(작은 제어 메시지 즉시 전송).
        // - SO_KEEPALIVE=0: TCP keepalive 미사용(app 레벨 heartbeat 가 담당).
        {
            int const yes{1};
            (void)::setsockopt(conn_fd.get(), IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
            int const no{0};
            (void)::setsockopt(conn_fd.get(), SOL_SOCKET, SO_KEEPALIVE, &no, sizeof(no));
        }

        Endpoint const peer = from_sockaddr(reinterpret_cast<sockaddr const&>(peer_addr), peer_addr_len);
        coordinator_.on_accept(std::move(conn_fd), peer); // 연결 생성/등록/통지는 coordinator 소관
    }
}

} // namespace ddcs::ctrl::infra::transport
