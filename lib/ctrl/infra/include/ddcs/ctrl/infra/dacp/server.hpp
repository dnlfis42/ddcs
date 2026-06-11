#pragma once

#include "ddcs/io/channel_events.hpp"

#include <cstdint>
#include <memory>

namespace ddcs::common {

class Fd;

} // namespace ddcs::common

namespace ddcs::io {

class Reactor;

} // namespace ddcs::io

namespace ddcs::ctrl::app::agent::port {

class Inbound;
class Outbound;

} // namespace ddcs::ctrl::app::agent::port

namespace ddcs::ctrl::infra::dacp {

namespace port = ddcs::ctrl::app::agent::port;

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

    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] bool active() const noexcept;

    [[nodiscard]] bool init(port::Inbound& inbound) noexcept;
    [[nodiscard]] bool start();
    void stop() noexcept;
    void close() noexcept;

    // app 배선용 함수
    [[nodiscard]] port::Outbound& outbound() noexcept;

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

} // namespace ddcs::ctrl::infra::dacp
