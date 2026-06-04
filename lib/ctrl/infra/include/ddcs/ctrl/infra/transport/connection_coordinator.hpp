#pragma once

#include "ddcs/common/fd.hpp"
#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/infra/transport/connection.hpp"
#include "ddcs/ctrl/infra/transport/endpoint.hpp"
#include "ddcs/ctrl/port/transport/inbound.hpp"
#include "ddcs/ctrl/port/transport/outbound.hpp"
#include "ddcs/runtime/timer_handler.hpp"
#include "ddcs/runtime/timer_id.hpp"

#include <unordered_map>
#include <vector>

#include <cstddef>
#include <cstdint>

namespace ddcs::runtime {
class Reactor;
}

namespace ddcs::ctrl::infra::transport {

using ddcs::ctrl::port::transport::CloseMode;
using ddcs::ctrl::port::transport::CloseReason;
using ddcs::ctrl::port::transport::Inbound;
using ddcs::ctrl::port::transport::Outbound;

// transport 의 정책/오케스트레이션 중심.
//  - app 쪽: Outbound 구현(payload_buffer/send/close/timer).
//  - runtime 쪽: runtime::TimerHandler(타이머 만료 수신), Connection 의 per-conn 이벤트 구동.
// Reactor(runtime)는 이 클래스를 모른다. 연결 수명/FSM/framing 은 전부 여기 산다.
class ConnectionCoordinator final : public Outbound, public runtime::TimerHandler {
public:
    explicit ConnectionCoordinator(runtime::Reactor& reactor);
    ~ConnectionCoordinator() override { connection_map_.clear(); }

    ConnectionCoordinator(ConnectionCoordinator const&) = delete;
    ConnectionCoordinator& operator=(ConnectionCoordinator const&) = delete;
    ConnectionCoordinator(ConnectionCoordinator&&) noexcept = delete;
    ConnectionCoordinator& operator=(ConnectionCoordinator&&) noexcept = delete;

public: // DI
    void init(Inbound& handler) noexcept { handler_ = &handler; }

public: // 조회
    std::size_t size() const noexcept { return connection_map_.size(); }

public: // Outbound (app -> transport)
    common::PoolHandle<common::LinearBuffer> payload_buffer() override;
    void send(ConnectionId id, std::uint8_t type, common::PoolHandle<common::LinearBuffer> body) override;
    void close(ConnectionId id, CloseMode mode) override;

public:
    void on_timer(runtime::TimerId id) override {
        handle_timer(id);
        close_connections();
    }
    void on_accept(common::Fd fd, Endpoint peer) {
        handle_accept(std::move(fd), peer);
        close_connections();
    }
    void on_event(Connection& conn, std::uint32_t events) {
        handle_event(conn, events);
        close_connections();
    }

    // 조립 루트(Controller)가 sweep 등 entry-point 밖에서 close 한 뒤 reap 을 구동.
    void close_connections(); // pending close 일괄 정리(reap). 위 on_* 래퍼도 호출.

private:
    void handle_timer(runtime::TimerId id);
    void handle_accept(common::Fd fd, Endpoint peer);
    void handle_event(Connection& conn, std::uint32_t events);

private: // per-connection 이벤트
    void handle_readable(Connection* conn);
    void handle_writable(Connection* conn);

private:
    void update_interest(Connection* conn);

private: // epoll
    bool epoll_add(Connection* conn);
    void epoll_mod(Connection* conn, std::uint32_t events);
    void epoll_del(Connection* conn);

private: // FSM 전이
    void to_passive_wait(Connection* conn);
    void fail(Connection* conn, CloseReason reason);
    void schedule_close(Connection* conn); // close 예약

private: // 조회
    Connection* find(ConnectionId id);

private: // 타이머 장부 (opaque TimerId -> 의미)
    enum class TimerKind : std::uint8_t { handshake, pw };
    struct TimerSlot {
        ConnectionId cid;
        TimerKind kind;
    };
    void cancel_handshake(ConnectionId id); // 첫 프레임 도착/close 시 handshake 타이머 취소(멱등)

private:
    runtime::Reactor& reactor_;
    Inbound* handler_{nullptr};
    std::uint64_t next_id_{0}; // ConnectionId 발급 카운터(transport mint). 1 부터
    common::ObjectPool<Connection> connection_pool_;
    common::ObjectPool<common::LinearBuffer> payload_pool_;
    std::unordered_map<ConnectionId, common::PoolHandle<Connection>> connection_map_;
    std::vector<ConnectionId> pending_close_;
    std::unordered_map<runtime::TimerId, TimerSlot> timer_slot_;            // 발화 TimerId -> (conn, kind)
    std::unordered_map<ConnectionId, runtime::TimerId> handshake_timer_id_; // accept->첫 프레임 한도(3s)
    std::unordered_map<ConnectionId, runtime::TimerId> pw_timer_id_;        // passive_wait 한도(5s)
};

} // namespace ddcs::ctrl::infra::transport
