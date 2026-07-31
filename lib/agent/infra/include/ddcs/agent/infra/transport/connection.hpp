#pragma once

#include "ddcs/agent/app/transport/port/message_buffer.hpp"
#include "ddcs/common/circular_buffer.hpp"
#include "ddcs/io/channel.hpp"
#include "ddcs/io/channel_events.hpp"
#include "ddcs/io/fd.hpp"
#include "ddcs/net/stream_io.hpp"

#include <cstddef>
#include <cstdint>
#include <queue>
#include <string_view>
#include <utility>

namespace ddcs::agent::infra::transport {

namespace port = ddcs::agent::app::transport::port;

// on_ready 위임 대상 (순환 의존 회피)
class Connector;

// 단일 클라이언트 연결. 순수 메커니즘: syscall + 버퍼 + 결과 보고만 한다.
// 상태 전이는 스스로 하지 않고 Connector가 transition()/reset()으로 구동한다.
class Connection final : private io::ChannelHandler {
public:
    enum class State : std::uint8_t {
        idle,       // fd 없음. 연결 전/끊김 후
        connecting, // socket()+connect() 호출. writable(완료) 대기
        connected,  // 연결 성립. 양방향 I/O
    };

    // 로그/진단용 이름. 어휘 밖 값은 빈 문자열로 노출한다.
    static constexpr std::string_view to_string(State state) noexcept {
        switch (state) {
        case State::idle:
            return "idle";
        case State::connecting:
            return "connecting";
        case State::connected:
            return "connected";
        }
        return {};
    }

    // rx ring 용량은 정책이라 위(Connector)가 정해 주입한다.
    explicit Connection(std::size_t rx_buffer_size)
        : rx_buffer_(rx_buffer_size) {}
    ~Connection() override = default; // Channel/Fd RAII가 fd를 닫는다.

    Connection(Connection const&) = delete;
    Connection& operator=(Connection const&) = delete;
    Connection(Connection&&) noexcept = delete;
    Connection& operator=(Connection&&) noexcept = delete;

    // 전제조건: idle connection + 유효한 fd
    void init(Connector& connector, io::Fd fd, io::ChannelEvents io_interest) noexcept;
    void close() noexcept;

    int fd() const noexcept {
        return channel_.fd();
    }

    io::ChannelEvents io_interest() const noexcept {
        return channel_.interests();
    }

    io::Channel& channel() noexcept {
        return channel_;
    }

    State state() const noexcept {
        return state_;
    }

    bool registered() const noexcept {
        return channel_.registered();
    }

    // 결과 어휘는 ctrl transport Connection과 공유한다.
    [[nodiscard]] net::ReceiveResult receive() {
        return net::receive_into(channel_.fd(), rx_buffer_);
    }

    [[nodiscard]] net::TransmitResult transmit() {
        return net::transmit_from(channel_.fd(), tx_queue_);
    }

    // framing 헬퍼(wire::frame::dispatch_frames)에 rx ring을 직접 넘기기 위한 접근자
    common::CircularBuffer& rx_buffer() noexcept {
        return rx_buffer_;
    }

    bool tx_empty() const noexcept {
        return tx_queue_.empty();
    }

    void tx_enqueue(port::MessageBuffer&& buffer) {
        tx_queue_.push(std::move(buffer));
    }

    void transition(State to) noexcept;

private:
    // Reactor의 readiness 통지를 정책 없이 Connector로 위임
    void on_ready(io::Channel& channel, io::ChannelEvents events) override;

    Connector* connector_ = nullptr;
    io::Channel channel_;
    State state_ = State::idle;
    common::CircularBuffer rx_buffer_;
    std::queue<port::MessageBuffer> tx_queue_;
};

} // namespace ddcs::agent::infra::transport
