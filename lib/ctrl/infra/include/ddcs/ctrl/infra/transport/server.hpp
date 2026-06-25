#pragma once

#include "ddcs/io/channel_events.hpp"

#include <cstdint>
#include <memory>

namespace ddcs::io {

class Fd;
class Reactor;

} // namespace ddcs::io

namespace ddcs::ctrl::app::transport::port {

class ConnectionListener;
class Disconnector;
class MessageReceiver;
class MessageSender;

} // namespace ddcs::ctrl::app::transport::port

namespace ddcs::ctrl::infra::transport {

namespace port = ddcs::ctrl::app::transport::port;

class Connection;
struct PeerAddress;

class Server {
public:
    Server(io::Reactor& reactor, std::uint16_t port, int backlog);
    ~Server();

    Server(Server const&) = delete;
    Server& operator=(Server const&) = delete;
    Server(Server&&) noexcept = delete;
    Server& operator=(Server&&) noexcept = delete;

    [[nodiscard]] bool
    init(port::ConnectionListener& listener, port::MessageReceiver& receiver) noexcept;
    [[nodiscard]] bool start();
    void stop() noexcept;
    void close() noexcept;

    [[nodiscard]] port::Disconnector& disconnector() noexcept;
    [[nodiscard]] port::MessageSender& sender() noexcept;

    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] bool active() const noexcept;

    void handle_accepted(io::Fd&& fd, PeerAddress peer);
    void handle_accept_error(int err);
    void handle_acceptor_failure(io::ChannelEvents events);

    void handle_connection_ready(Connection& connection, io::ChannelEvents events);

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace ddcs::ctrl::infra::transport
