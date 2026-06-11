#pragma once

#include "ddcs/common/fd.hpp"
#include "ddcs/common/ring_buffer.hpp"
#include "ddcs/ctrl/app/agent/port/connection_id.hpp"
#include "ddcs/ctrl/app/agent/port/message_buffer.hpp"
#include "ddcs/ctrl/infra/dacp/peer_address.hpp"
#include "ddcs/io/channel.hpp"
#include "ddcs/io/channel_events.hpp"

#include <cstddef>
#include <cstdint>
#include <queue>
#include <span>
#include <utility>

namespace ddcs::ctrl::infra::dacp {

namespace port = ddcs::ctrl::app::agent::port;

class Server;

// DACP Server가 소유하는 TCP connection slot. Agent 등록 상태는 app::agent가 관리한다.
class Connection final : private io::ChannelHandler {
    friend class Server;

public:
    enum class State : std::uint8_t {
        idle,
        ready,   // fd와 Channel은 준비됐지만 Server 소유 전
        tracked, // Server map이 소유함. Reactor 등록 전이거나 해제된 후
        active,  // tracked + Channel이 Reactor에 등록되어 이벤트가 흐름
    };

    enum class IoResult : std::uint8_t {
        ok,
        full,
        would_block,
        peer_closed,
        error,
    };

    static constexpr std::size_t rx_buffer_capacity{1 << 12};

public:
    Connection() = default;
    ~Connection() override = default;

    Connection(Connection const&) = delete;
    Connection& operator=(Connection const&) = delete;
    Connection(Connection&&) noexcept = delete;
    Connection& operator=(Connection&&) noexcept = delete;

    [[nodiscard]] port::ConnectionId id() const noexcept { return id_; }
    [[nodiscard]] PeerAddress peer() const noexcept { return peer_; }
    [[nodiscard]] State state() const noexcept { return state_; }
    [[nodiscard]] io::Channel& channel() noexcept { return channel_; }
    [[nodiscard]] io::Channel const& channel() const noexcept { return channel_; }

    bool init(
        Server& server, port::ConnectionId id, PeerAddress peer, common::Fd&& fd, io::ChannelEvents interests
    ) noexcept;
    void close() noexcept;
    void reset() noexcept { close(); }

private: // io::ChannelHandler
    void on_ready(io::Channel& channel, io::ChannelEvents events) override;

private:
    // 상태 전이는 Server가 결정하고, 여기서는 그 사실만 기록한다.
    [[nodiscard]] bool mark_tracked() noexcept; // ready(접수) 또는 active(해체) → tracked
    [[nodiscard]] bool mark_active() noexcept;  // tracked → active

    // syscall 결과만 보고하고 상태 전이는 Server가 결정한다.
    IoResult receive();  // full | would_block | peer_closed | error
    IoResult transmit(); // ok | would_block | error

    [[nodiscard]] std::size_t rx_size() const noexcept { return rx_buffer_.size(); }
    bool rx_peek(std::span<std::byte> dst) const noexcept { return rx_buffer_.peek(dst); }
    bool rx_read(std::span<std::byte> dst) noexcept { return rx_buffer_.read(dst); }
    bool rx_consume(std::size_t n) noexcept { return rx_buffer_.consume(n); }

    [[nodiscard]] bool tx_empty() const noexcept { return tx_queue_.empty(); }
    void tx_enqueue(port::MessageBuffer&& buffer) { tx_queue_.push(std::move(buffer)); }

private:
    Server* server_{nullptr};
    port::ConnectionId id_{};
    PeerAddress peer_{};
    State state_{State::idle};
    io::Channel channel_{};
    common::RingBuffer<rx_buffer_capacity> rx_buffer_;
    std::queue<port::MessageBuffer> tx_queue_;
};

} // namespace ddcs::ctrl::infra::dacp
