#pragma once

#include "ddcs/common/fd.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/common/ring_buffer.hpp"
#include "ddcs/io/channel.hpp"
#include "ddcs/io/channel_events.hpp"

#include <queue>
#include <span>
#include <utility>

#include <cstddef>
#include <cstdint>

namespace ddcs::agent::infra::frame {

class Connector; // on_ready 위임 대상 (순환 의존 회피)

inline constexpr std::size_t inbound_buffer_capacity{1 << 12};

// 단일 클라이언트 연결. 순수 메커니즘: syscall + 버퍼 + IoResult 보고만 한다.
// 상태 전이는 스스로 하지 않고 Connector가 transition()/reset()으로 구동한다.
// io::ChannelHandler 로서 Reactor의 readiness 통지를 정책 없이 Connector로 위임한다.
class Connection final : private io::ChannelHandler {
public:
    enum class State : std::uint8_t {
        idle,       // fd 없음. 연결 전/끊김 후
        connecting, // socket()+connect() 호출. writable(완료) 대기
        connected,  // 연결 성립. 양방향 I/O
    };

    enum class IoResult : std::uint8_t {
        ok,          // 정상
        full,        // 정상. inbound 버퍼 꽉 참
        would_block, // 정상. EAGAIN
        peer_closed, // 정상. FIN
        error,       // 비정상. 복구 불가
    };

public:
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

    std::size_t rx_size() const noexcept {
        return rx_buffer_.size();
    }
    bool rx_peek(std::span<std::byte> dst) const noexcept {
        return rx_buffer_.peek(dst);
    }
    bool rx_read(std::span<std::byte> dst) noexcept {
        return rx_buffer_.read(dst);
    }
    bool rx_consume(std::size_t n) noexcept {
        return rx_buffer_.consume(n);
    }

    bool tx_empty() const noexcept {
        return tx_queue_.empty();
    }
    void tx_enqueue(common::PoolHandle<common::LinearBuffer>&& buffer) {
        tx_queue_.push(std::move(buffer));
    }

    [[nodiscard]] bool assign(common::Fd fd, io::ChannelEvents io_interest) noexcept;
    [[nodiscard]] bool transition(State to) noexcept;
    void reset() noexcept; // idle로. fd 닫고 버퍼 비움

private:
    void on_ready(io::Channel& channel, io::ChannelEvents events) override;

private:
    Connector* connector_{nullptr};
    io::Channel channel_{};
    State state_{State::idle};
    common::RingBuffer<inbound_buffer_capacity> rx_buffer_;
    std::queue<common::PoolHandle<common::LinearBuffer>> tx_queue_;
};

} // namespace ddcs::agent::infra::frame
