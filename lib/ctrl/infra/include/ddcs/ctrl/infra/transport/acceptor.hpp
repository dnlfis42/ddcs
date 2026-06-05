#pragma once

#include "ddcs/common/fd.hpp"
#include "ddcs/runtime/fd_handler.hpp"

#include <cstdint>

namespace ddcs::runtime {
class Reactor;
}

namespace ddcs::ctrl::infra::transport {

class ConnectionCoordinator;

// listen fd 전담 FdHandler. accept 루프 + reserve-fd(EMFILE/ENFILE shed) + 소켓옵션만 안다.
// accept 된 fd/peer 는 ConnectionCoordinator 로 넘긴다(연결 수명/FSM 은 coordinator 소관).
class Acceptor : public runtime::FdHandler {
public:
    Acceptor(
        runtime::Reactor& reactor, ConnectionCoordinator& coordinator, std::uint16_t listen_port, int accept_backlog
    );
    ~Acceptor() override = default;

    Acceptor(Acceptor const&) = delete;
    Acceptor& operator=(Acceptor const&) = delete;
    Acceptor(Acceptor&&) noexcept = delete;
    Acceptor& operator=(Acceptor&&) noexcept = delete;

    void start();               // socket/bind/listen + reactor.add(listen_fd) + reserve-fd 확보
    std::uint16_t port() const; // 실제 바인드된 포트 (ephemeral=0 확인용)

public:                                              // runtime::FdHandler (listen fd)
    void on_fd_event(std::uint32_t events) override; // 에러 -> reactor.stop / EPOLLIN -> accept 루프

private:
    void accept_loop();

    runtime::Reactor& reactor_;
    ConnectionCoordinator& coordinator_;
    std::uint16_t listen_port_;
    int const accept_backlog_;
    common::Fd listen_fd_{};
    common::Fd accept_spare_fd_{}; // reserve-fd: EMFILE/ENFILE 시 kernel accept 큐 shed 용
};

} // namespace ddcs::ctrl::infra::transport
