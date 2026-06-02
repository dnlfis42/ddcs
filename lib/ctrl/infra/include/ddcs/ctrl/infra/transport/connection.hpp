#pragma once

#include "ddcs/common/fd.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/common/ring_buffer.hpp"
#include "ddcs/ctrl/infra/transport/endpoint.hpp"
#include "ddcs/ctrl/port/transport/connection_id.hpp"
#include "ddcs/io/io_handler.hpp"

#include <queue>
#include <span>
#include <utility>

#include <cstddef>
#include <cstdint>

namespace ddcs::ctrl::infra::transport {

using ddcs::ctrl::port::transport::ConnectionId;

class ConnectionCoordinator; // on_io 위임 대상 (순환 의존 회피)

inline constexpr std::size_t inbound_buffer_capacity{1 << 12};

// 순수 메커니즘: syscall + 버퍼 + IoResult 보고만 한다.
// 상태 전이는 스스로 하지 않고 ConnectionCoordinator 가 transition() 으로 구동한다.
//
// io::IoHandler 로서: Reactor 가 이 conn fd 의 readiness 를 알려오면 on_io 가 호출되고,
// 정책은 갖지 않은 채 곧장 coordinator 로 위임한다.
class Connection : public io::IoHandler {
public:
    enum class State : std::uint8_t {
        idle,          // 풀 슬롯 미사용. 재사용 대기. fd 없음
        open,          // epoll 등록 완료. 양방향 I/O 가능
        active_close,  // 우리가 닫기로 결정. shutdown(WR), 송신 차단
        passive_wait,  // active_close 후 peer FIN 대기
        passive_close, // peer FIN 수신. inbound 위로 흘린 뒤 outbound flush 중
        aborting,      // 문제 감지, 통지 후 close() 대기. close 안 함
        closing,       // close() 확인됨. pending_close_ 등록, close 가능
    };

    enum class IoResult : std::uint8_t {
        ok,          // 정상
        full,        // 정상. inbound 버퍼 꽉 참
        would_block, // 정상. EAGAIN
        peer_closed, // 정상. FIN
        error,       // 비정상. 복구 불가 (RST 등)
    };

public:
    Connection() = default;
    ~Connection() override = default; // Fd RAII 가 fd 를 닫는다

    Connection(Connection const&) = delete;
    Connection& operator=(Connection const&) = delete;
    Connection(Connection&&) noexcept = delete;
    Connection& operator=(Connection&&) noexcept = delete;

public: // io::IoHandler - 정책 없음. 곧장 coordinator 로 위임.
    void on_io(std::uint32_t events) override;

public: // state query
    ConnectionId id() const noexcept { return id_; }
    int fd() const noexcept { return fd_.get(); }
    Endpoint peer() const noexcept { return peer_; }
    std::uint32_t io_interest() const noexcept { return io_interest_; }
    State state() const noexcept { return state_; }
    bool in_epoll() const noexcept { return in_epoll_; }
    bool close_requested() const noexcept { return close_requested_; }

public: // mutation (ConnectionCoordinator 전용)
    // 이 conn 을 구동할 coordinator 바인딩. 풀에서 꺼낸 직후 1회 설정(전이 전).
    void set_coordinator(ConnectionCoordinator& coordinator) noexcept { coordinator_ = &coordinator; }
    // 풀에서 갓 꺼낸 idle 슬롯에 자원 배정. 전이는 하지 않는다.
    void assign(ConnectionId id, common::Fd fd, Endpoint peer, std::uint32_t io_interest) noexcept;
    // 합법 엣지면 전이 후 true, 불법이면 무변경 후 false (assert 안 함. 호출부 책임)
    [[nodiscard]]
    bool transition(State to) noexcept;
    // in_epoll_ 미러. 실제 epoll_ctl 은 ConnectionCoordinator 소관
    void enter_epoll() noexcept { in_epoll_ = true; }
    void leave_epoll() noexcept { in_epoll_ = false; }
    // 운영 중 epoll interest 변경분을 미러에 반영 (첫 세팅은 assign 이 담당)
    void set_io_interest(std::uint32_t io_interest) noexcept { io_interest_ = io_interest; }
    // 이후 fd close 가 RST 를 보내도록 래치 (SO_LINGER{1,0}). 토글 아님(되돌릴 수 없음)
    void latch_rst() noexcept;
    // graceful close 요청 마킹 (one-way). tx 드레인 완료 시 coordinator 가 closing 으로 전이
    void request_close() noexcept { close_requested_ = true; }
    // half-close: 송신 방향 FIN. 이후 write 불가(드레인 완료 후 호출). 수신은 계속 가능
    void shutdown_write() noexcept;
    // idle 로 되돌림 (풀 반납 시 ObjectPool 이 호출). fd 닫고 전 상태 초기화
    void reset() noexcept;

public: // I/O event
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

private: // data
    ConnectionCoordinator* coordinator_{nullptr};
    ConnectionId id_{};
    common::Fd fd_{};
    Endpoint peer_{};
    std::uint32_t io_interest_{};
    State state_{State::idle};
    bool in_epoll_{false};
    bool close_requested_{false};
    common::RingBuffer<inbound_buffer_capacity> rx_buffer_;
    std::queue<common::PoolHandle<common::LinearBuffer>> tx_queue_;
};

} // namespace ddcs::ctrl::infra::transport
