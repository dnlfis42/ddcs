#pragma once

#include "ddcs/common/fd.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/common/ring_buffer.hpp"
#include "ddcs/runtime/fd_handler.hpp"

#include <queue>
#include <span>
#include <utility>

#include <cstddef>
#include <cstdint>

namespace ddcs::agent::infra {

class Connector; // on_io 위임 대상 (순환 의존 회피)

inline constexpr std::size_t inbound_buffer_capacity{1 << 12};

// 단일 클라이언트 연결. 순수 메커니즘: syscall + 버퍼 + IoResult 보고만 한다.
// 상태 전이는 스스로 하지 않고 Connector 가 transition()/reset() 으로 구동한다.
// runtime::FdHandler 로서 Reactor 의 readiness 통지를 정책 없이 Connector 로 위임한다.
class Connection : public runtime::FdHandler {
public:
    enum class State : std::uint8_t {
        idle,       // fd 없음. 연결 전/끊김 후
        connecting, // socket()+connect() 호출. EPOLLOUT(완료) 대기
        connected,  // 연결 성립. 양방향 I/O
    };

    enum class IoResult : std::uint8_t {
        ok,          // 정상
        full,        // 정상. inbound 버퍼 꽉 참
        would_block, // 정상. EAGAIN
        peer_closed, // 정상. FIN
        error,       // 비정상. 복구 불가
    };

    Connection() = default;
    ~Connection() override = default; // Fd RAII 가 fd 를 닫는다

    Connection(Connection const&) = delete;
    Connection& operator=(Connection const&) = delete;
    Connection(Connection&&) noexcept = delete;
    Connection& operator=(Connection&&) noexcept = delete;

public: // runtime::FdHandler - 정책 없음. 곧장 Connector 로 위임.
    void on_io(std::uint32_t events) override;

public: // query
    int fd() const noexcept { return fd_.get(); }
    std::uint32_t io_interest() const noexcept { return io_interest_; }
    State state() const noexcept { return state_; }
    bool in_epoll() const noexcept { return in_epoll_; }

public: // mutation (Connector 전용)
    void set_connector(Connector& connector) noexcept { connector_ = &connector; }
    void assign(common::Fd fd, std::uint32_t io_interest) noexcept;
    [[nodiscard]]
    bool transition(State to) noexcept;
    void enter_epoll() noexcept { in_epoll_ = true; }
    void leave_epoll() noexcept { in_epoll_ = false; }
    void set_io_interest(std::uint32_t io_interest) noexcept { io_interest_ = io_interest; }
    void reset() noexcept; // idle 로. fd 닫고 버퍼 비움

public: // I/O
    IoResult receive();
    IoResult transmit();

public: // rx
    std::size_t rx_size() const noexcept { return rx_buffer_.size(); }
    bool rx_peek(std::span<std::byte> dst) const noexcept { return rx_buffer_.peek(dst); }
    bool rx_read(std::span<std::byte> dst) noexcept { return rx_buffer_.read(dst); }
    bool rx_consume(std::size_t n) noexcept { return rx_buffer_.consume(n); }

public: // tx
    bool tx_empty() const noexcept { return tx_queue_.empty(); }
    void tx_enqueue(common::PoolHandle<common::LinearBuffer>&& buffer) { tx_queue_.push(std::move(buffer)); }

private:
    Connector* connector_{nullptr};
    common::Fd fd_{};
    std::uint32_t io_interest_{};
    State state_{State::idle};
    bool in_epoll_{false};
    common::RingBuffer<inbound_buffer_capacity> rx_buffer_;
    std::queue<common::PoolHandle<common::LinearBuffer>> tx_queue_;
};

} // namespace ddcs::agent::infra
