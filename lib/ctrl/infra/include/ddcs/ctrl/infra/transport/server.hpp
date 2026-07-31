#pragma once

#include "ddcs/io/channel_events.hpp"
#include "ddcs/io/sys_result.hpp"

#include <cstddef>
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
    // rx_buffer_size의 단일 출처는 Controller::Config다. 기본값 없이 명시 전달만 받는다.
    Server(io::Reactor& reactor, std::uint16_t port, int backlog, std::size_t rx_buffer_size);
    ~Server();

    Server(Server const&) = delete;
    Server& operator=(Server const&) = delete;
    Server(Server&&) noexcept = delete;
    Server& operator=(Server&&) noexcept = delete;

    [[nodiscard]] io::SysResult
    init(port::ConnectionListener& listener, port::MessageReceiver& receiver) noexcept;
    [[nodiscard]] io::SysResult start();
    void stop() noexcept;
    void close() noexcept;

    [[nodiscard]] port::Disconnector& disconnector() noexcept;
    [[nodiscard]] port::MessageSender& sender() noexcept;

    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] bool active() const noexcept;

    void on_accepted(io::Fd&& fd, PeerAddress peer);
    void on_accept_error(int err);
    void on_acceptor_failure(io::ChannelEvents events);

    void on_connection_event(Connection& connection, io::ChannelEvents events);

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace ddcs::ctrl::infra::transport
