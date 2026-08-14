#pragma once

#include "ddcs/common/circular_buffer.hpp"
#include "ddcs/ctrl/app/transport/port/connection_id.hpp"
#include "ddcs/ctrl/app/transport/port/message_buffer.hpp"
#include "ddcs/ctrl/infra/transport/peer_address.hpp"
#include "ddcs/io/channel.hpp"
#include "ddcs/io/channel_events.hpp"
#include "ddcs/io/fd.hpp"
#include "ddcs/net/stream_io.hpp"

#include <cstddef>
#include <queue>
#include <utility>

namespace ddcs::ctrl::infra::transport {

namespace port = ddcs::ctrl::app::transport::port;

class Server;

// Server가 소유하는 TCP connection slot
class Connection final : private io::ChannelHandler {
public:
    // rx ring 용량은 정책이라 위(Server)가 정해 주입한다.
    explicit Connection(std::size_t rx_buffer_size)
        : rx_buffer_(rx_buffer_size) {}
    ~Connection() override = default;

    Connection(Connection const&) = delete;
    Connection& operator=(Connection const&) = delete;
    Connection(Connection&&) noexcept = delete;
    Connection& operator=(Connection&&) noexcept = delete;

    // 전제조건: idle(reset된) connection + 유효한 fd
    void init(
        Server& server, port::ConnectionId id, PeerAddress peer, io::Fd&& fd,
        io::ChannelEvents interests
    ) noexcept;
    void close() noexcept;

    [[nodiscard]] port::ConnectionId id() const noexcept {
        return id_;
    }

    [[nodiscard]] PeerAddress peer() const noexcept {
        return peer_;
    }

    [[nodiscard]] io::Channel& channel() noexcept {
        return channel_;
    }

    [[nodiscard]] bool registered() const noexcept {
        return channel_.registered();
    }

    void reset() noexcept;

private:
    friend class Server;

    void on_ready(io::Channel& channel, io::ChannelEvents events) override;

    // syscall 결과만 보고한다. 결과 어휘는 agent transport Connection과 공유한다.
    [[nodiscard]] net::ReceiveResult receive();
    [[nodiscard]] net::TransmitResult transmit();

    // framing 헬퍼(wire::frame::dispatch_frames)에 rx ring을 직접 넘기기 위한 접근자
    [[nodiscard]] common::CircularBuffer& rx_buffer() noexcept {
        return rx_buffer_;
    }

    [[nodiscard]] bool tx_empty() const noexcept {
        return tx_queue_.empty();
    }

    // 송신 큐 깊이. 큐가 무상한이라 이 값이 유일한 감시 수단이다 (메트릭 노출용).
    [[nodiscard]] std::size_t tx_queued() const noexcept {
        return tx_queue_.size();
    }

    void tx_enqueue(port::MessageBuffer&& buffer) {
        tx_queue_.push(std::move(buffer));
    }

    Server* server_ = nullptr;
    port::ConnectionId id_;
    PeerAddress peer_;
    io::Channel channel_;
    common::CircularBuffer rx_buffer_;
    std::queue<port::MessageBuffer> tx_queue_;
};

} // namespace ddcs::ctrl::infra::transport
