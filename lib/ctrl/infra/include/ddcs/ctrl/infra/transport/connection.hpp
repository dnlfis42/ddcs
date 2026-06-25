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
#include <cstdint>
#include <queue>
#include <span>
#include <utility>

namespace ddcs::ctrl::infra::transport {

namespace port = ddcs::ctrl::app::transport::port;

class Server;

// Server가 소유하는 TCP connection slot
class Connection final : private io::ChannelHandler {
public:
    enum class State : std::uint8_t {
        idle,
        ready,   // fd와 Channel은 준비됐지만 Server 소유 전
        tracked, // Server map이 소유함. Reactor 등록 전이거나 해제된 후
        active,  // tracked + Channel이 Reactor에 등록되어 이벤트가 흐름
    };

    // ok | full | would_block | peer_closed | error. agent transport Connection과 공유한다.
    using IoResult = net::StreamResult;

    static constexpr std::size_t rx_buffer_capacity = 1 << 12;

    Connection() = default;
    ~Connection() override = default;

    Connection(Connection const&) = delete;
    Connection& operator=(Connection const&) = delete;
    Connection(Connection&&) noexcept = delete;
    Connection& operator=(Connection&&) noexcept = delete;

    [[nodiscard]] bool init(
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

    [[nodiscard]] State state() const noexcept {
        return state_;
    }

    [[nodiscard]] io::Channel& channel() noexcept {
        return channel_;
    }

    [[nodiscard]] io::Channel const& channel() const noexcept {
        return channel_;
    }

    void reset() noexcept;

private:
    friend class Server;

    void on_ready(io::Channel& channel, io::ChannelEvents events) override;

    // 상태 전이는 Server가 결정하고, 여기서는 그 사실만 기록한다.
    [[nodiscard]] bool mark_tracked() noexcept; // ready(접수) 또는 active(해체)를 tracked로
    [[nodiscard]] bool mark_active() noexcept;  // tracked를 active로

    // syscall 결과만 보고하고 상태 전이는 Server가 결정한다.
    [[nodiscard]] IoResult receive();  // full | would_block | peer_closed | error
    [[nodiscard]] IoResult transmit(); // ok | would_block | error

    [[nodiscard]] std::size_t rx_size() const noexcept {
        return rx_buffer_.size();
    }

    [[nodiscard]] bool rx_peek(std::span<std::byte> dst) const noexcept {
        return rx_buffer_.try_peek(dst);
    }

    [[nodiscard]] bool rx_read(std::span<std::byte> dst) noexcept {
        return rx_buffer_.try_read(dst);
    }

    [[nodiscard]] bool rx_consume(std::size_t n) noexcept {
        return rx_buffer_.try_consume(n);
    }

    // framing 헬퍼(wire::frame::extract_frames)에 rx ring을 직접 넘기기 위한 접근자
    [[nodiscard]] common::CircularBuffer& rx_buffer() noexcept {
        return rx_buffer_;
    }

    [[nodiscard]] bool tx_empty() const noexcept {
        return tx_queue_.empty();
    }

    void tx_enqueue(port::MessageBuffer&& buffer) {
        tx_queue_.push(std::move(buffer));
    }

    Server* server_ = nullptr;
    port::ConnectionId id_;
    PeerAddress peer_;
    State state_ = State::idle;
    io::Channel channel_;
    common::CircularBuffer rx_buffer_{rx_buffer_capacity};
    std::queue<port::MessageBuffer> tx_queue_;
};

} // namespace ddcs::ctrl::infra::transport
