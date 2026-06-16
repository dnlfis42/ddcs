#pragma once

#include "ddcs/io/channel_events.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace ddcs::common {

class Fd;

} // namespace ddcs::common

namespace ddcs::io {

class Reactor;

} // namespace ddcs::io

namespace ddcs::ctrl::app::agent::port {

class ConnectionObserver;
class Disconnector;
class MessageSender;

} // namespace ddcs::ctrl::app::agent::port

namespace ddcs::ctrl::infra::frame {

namespace port = ddcs::ctrl::app::agent::port;

class Connection;
struct PeerAddress;

class Server {
public:
    // max_payload_size: 한 frame이 실을 수 있는 acmp payload 상한 (frame header 제외)
    //                   rx ring 한도로 clamp된다.
    Server(io::Reactor& reactor, std::uint16_t port, int backlog, std::size_t max_payload_size);
    ~Server();

    Server(Server const&) = delete;
    Server& operator=(Server const&) = delete;
    Server(Server&&) noexcept = delete;
    Server& operator=(Server&&) noexcept = delete;

    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] bool active() const noexcept;

    [[nodiscard]] bool init(port::ConnectionObserver& observer) noexcept;
    [[nodiscard]] bool start();
    void stop() noexcept;
    void close() noexcept;

    // app 배선용 함수
    [[nodiscard]] port::MessageSender& sender() noexcept;
    [[nodiscard]] port::Disconnector& disconnector() noexcept;

public: // Acceptor callback
    void handle_accepted(common::Fd&& fd, PeerAddress peer);
    void handle_accept_error(int err);
    void handle_acceptor_failure(io::ChannelEvents events);

public: // Connection callback
    void handle_connection_ready(Connection& connection, io::ChannelEvents events);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ddcs::ctrl::infra::frame
