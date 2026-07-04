#pragma once

#include "ddcs/common/circular_buffer.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/io/channel.hpp"
#include "ddcs/io/channel_events.hpp"
#include "ddcs/io/fd.hpp"
#include "ddcs/net/stream_io.hpp"

#include <cstddef>
#include <cstdint>
#include <queue>
#include <utility>

namespace ddcs::agent::infra::transport {

// on_ready 위임 대상 (순환 의존 회피)
class Connector;

inline constexpr std::size_t rx_buffer_capacity = 1 << 12;

// 단일 클라이언트 연결
// - 순수 메커니즘: syscall + 버퍼 + IoResult 보고만 한다.
// - 상태 전이는 스스로 하지 않고 Connector가 transition()/reset()으로 구동한다.
// - io::ChannelHandler 로서 Reactor의 readiness 통지를 정책 없이 Connector로 위임한다.
class Connection final : private io::ChannelHandler {
public:
    enum class State : std::uint8_t {
        idle,       // fd 없음. 연결 전/끊김 후
        connecting, // socket()+connect() 호출. writable(완료) 대기
        connected,  // 연결 성립. 양방향 I/O
    };

    // ok | full | would_block | peer_closed | error. ctrl transport Connection과 공유한다.
    using IoResult = net::StreamResult;

    Connection() = default;
    ~Connection() override = default; // Channel/Fd RAII가 fd를 닫는다.

    Connection(Connection const&) = delete;
    Connection& operator=(Connection const&) = delete;
    Connection(Connection&&) noexcept = delete;
    Connection& operator=(Connection&&) noexcept = delete;

    int fd() const noexcept {
        return channel_.fd();
    }

    io::Channel& channel() noexcept {
        return channel_;
    }

    io::Channel const& channel() const noexcept {
        return channel_;
    }

    io::ChannelEvents io_interest() const noexcept {
        return channel_.interests();
    }

    State state() const noexcept {
        return state_;
    }

    bool registered() const noexcept {
        return channel_.registered();
    }

    void set_connector(Connector& connector) noexcept {
        connector_ = &connector;
    }

    IoResult receive();
    IoResult transmit();

    // framing 헬퍼(wire::frame::extract_frames)에 rx ring을 직접 넘기기 위한 접근자
    common::CircularBuffer& rx_buffer() noexcept {
        return rx_buffer_;
    }

    bool tx_empty() const noexcept {
        return tx_queue_.empty();
    }

    void tx_enqueue(common::PoolHandle<common::LinearBuffer>&& buffer) {
        tx_queue_.push(std::move(buffer));
    }

    [[nodiscard]] bool assign(io::Fd fd, io::ChannelEvents io_interest) noexcept;
    [[nodiscard]] bool transition(State to) noexcept;

    void reset() noexcept; // idle로. fd 닫고 버퍼 비움

private:
    void on_ready(io::Channel& channel, io::ChannelEvents events) override;

    Connector* connector_ = nullptr;
    io::Channel channel_;
    State state_ = State::idle;
    common::CircularBuffer rx_buffer_{rx_buffer_capacity};
    std::queue<common::PoolHandle<common::LinearBuffer>> tx_queue_;
};

} // namespace ddcs::agent::infra::transport
