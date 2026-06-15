#pragma once

#include "ddcs/common/fd.hpp"
#include "ddcs/io/channel.hpp"
#include "ddcs/io/channel_events.hpp"

#include <cstdint>

namespace ddcs::ctrl::infra::frame {

class Server;

class Acceptor final : private io::ChannelHandler {
public:
    Acceptor(Server& server, std::uint16_t listen_port, int accept_backlog) noexcept;
    ~Acceptor() override = default;

    Acceptor(Acceptor const&) = delete;
    Acceptor& operator=(Acceptor const&) = delete;
    Acceptor(Acceptor&&) noexcept = delete;
    Acceptor& operator=(Acceptor&&) noexcept = delete;

    [[nodiscard]] std::uint16_t port() const noexcept { return bound_port_; }
    [[nodiscard]] io::Channel& channel() noexcept { return channel_; }
    [[nodiscard]] io::Channel const& channel() const noexcept { return channel_; }
    [[nodiscard]] bool valid() const noexcept { return channel_.valid(); }

    [[nodiscard]] bool init() noexcept;
    void close() noexcept;

private: // io::ChannelHandler
    void on_ready(io::Channel& channel, io::ChannelEvents events) override;

private:
    void drain_accepts();

    // EMFILE/ENFILE: spare fd를 잠시 반납해 대기 연결 하나를 accept 후 즉시 닫아 거절한다.
    // 반환값이 true면 accept 루프를 계속, false면 이번 drain을 중단한다.
    [[nodiscard]] bool reject_pending_connection(int exhausted_err);

private:
    Server& server_;
    std::uint16_t listen_port_{};
    std::uint16_t bound_port_{};
    int accept_backlog_{};
    io::Channel channel_{};
    common::Fd accept_spare_fd_{};
};

} // namespace ddcs::ctrl::infra::frame
