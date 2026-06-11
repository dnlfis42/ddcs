
#include "ddcs/ctrl/infra/dacp/acceptor.hpp"

#include "ddcs/ctrl/infra/dacp/peer_address.hpp"
#include "ddcs/ctrl/infra/dacp/server.hpp"
#include "ddcs/logger/log.hpp"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <utility>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

namespace ddcs::ctrl::infra::dacp {

namespace {

[[nodiscard]] PeerAddress to_peer_address(sockaddr const& sa, socklen_t len) noexcept {
    PeerAddress peer{};
    if (sa.sa_family == AF_INET && len >= static_cast<socklen_t>(sizeof(sockaddr_in))) {
        sockaddr_in s4{};
        std::memcpy(&s4, &sa, sizeof(s4));
        peer.family = PeerAddress::Family::v4;
        peer.port = ntohs(s4.sin_port);
        std::memcpy(peer.addr.data(), &s4.sin_addr.s_addr, 4);
    } else if (sa.sa_family == AF_INET6 && len >= static_cast<socklen_t>(sizeof(sockaddr_in6))) {
        sockaddr_in6 s6{};
        std::memcpy(&s6, &sa, sizeof(s6));
        peer.family = PeerAddress::Family::v6;
        peer.port = ntohs(s6.sin6_port);
        std::memcpy(peer.addr.data(), &s6.sin6_addr, 16);
    }
    return peer;
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

Acceptor::Acceptor(Server& server, std::uint16_t listen_port, int accept_backlog) noexcept
    : server_{server}, listen_port_{listen_port}, accept_backlog_{accept_backlog} {}

bool Acceptor::init() noexcept {
    if (valid()) {
        return false;
    }

    common::Fd fd{::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)};
    if (!fd.valid()) {
        return false;
    }

    int const yes{1};
    if (::setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(listen_port_);
    if (::bind(fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        return false;
    }

    if (::listen(fd.get(), accept_backlog_) < 0) {
        return false;
    }

    std::uint16_t const bound_port = query_bound_port(fd.get());
    if (bound_port == 0) {
        return false;
    }

    auto const interests = io::ChannelEvents::readable | io::ChannelEvents::edge_triggered;
    if (!channel_.init(std::move(fd), interests, *this)) {
        errno = EINVAL;
        return false;
    }

    accept_spare_fd_.reset(::open("/dev/null", O_RDONLY | O_CLOEXEC));
    if (!accept_spare_fd_.valid()) {
        int const err = errno;
        close();
        errno = err;
        return false;
    }

    bound_port_ = bound_port;

    LOG_INFO("dacp.acceptor.init", logger::kv("port", port()));
    return true;
}

void Acceptor::close() noexcept {
    assert(!channel_.registered());

    bound_port_ = 0;
    channel_.close();
    accept_spare_fd_.close();
}

void Acceptor::on_ready(io::Channel& channel, io::ChannelEvents events) {
    if (&channel != &channel_) {
        return;
    }

    if (io::contains(events, io::ChannelEvents::error) || io::contains(events, io::ChannelEvents::hangup)) {
        server_.handle_acceptor_failure(events); // 로깅은 Server가 담당한다
        return;
    }
    if (io::contains(events, io::ChannelEvents::readable)) {
        drain_accepts();
    }
}

void Acceptor::drain_accepts() {
    for (;;) {
        sockaddr_storage peer_addr{};
        socklen_t peer_addr_len{sizeof(peer_addr)};
        common::Fd conn_fd{::accept4(
            channel_.fd(), reinterpret_cast<sockaddr*>(&peer_addr), &peer_addr_len, SOCK_NONBLOCK | SOCK_CLOEXEC
        )};

        if (!conn_fd.valid()) {
            int const err = errno;
            if (err == EAGAIN || err == EWOULDBLOCK) {
                return;
            }
            if (err == EINTR || err == ECONNABORTED) {
                continue;
            }
            if (err == EMFILE || err == ENFILE) {
                if (!reject_pending_connection(err)) {
                    return;
                }
                continue;
            }
            server_.handle_accept_error(err);
            return;
        }

        int const yes{1};
        (void)::setsockopt(conn_fd.get(), IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
        PeerAddress const peer = to_peer_address(reinterpret_cast<sockaddr const&>(peer_addr), peer_addr_len);
        server_.handle_accepted(std::move(conn_fd), peer);
    }
}

bool Acceptor::reject_pending_connection(int exhausted_err) {
    LOG_WARN("dacp.accept.fd_exhausted", logger::kv("errno", exhausted_err));

    accept_spare_fd_.close();
    common::Fd reject{::accept4(channel_.fd(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC)};
    bool const drained = reject.valid();
    int const reject_err = drained ? 0 : errno;
    reject.close(); // 거절. spare 복구보다 먼저 슬롯을 반납해야 open이 성공한다.

    accept_spare_fd_.reset(::open("/dev/null", O_RDONLY | O_CLOEXEC));
    if (!accept_spare_fd_.valid()) {
        int const reopen_err = errno;
        LOG_ERROR("dacp.accept.spare_fd_reopen_failed", logger::kv("errno", reopen_err));
        server_.handle_accept_error(reopen_err);
        return false;
    }

    if (!drained) {
        if (reject_err == EAGAIN || reject_err == EWOULDBLOCK) {
            return false; // 그 사이 백로그가 비었다 - 정상 종료
        }
        server_.handle_accept_error(reject_err);
        return false;
    }
    return true;
}

} // namespace ddcs::ctrl::infra::dacp
