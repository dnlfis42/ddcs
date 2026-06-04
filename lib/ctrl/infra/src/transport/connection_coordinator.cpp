#include "ddcs/ctrl/infra/transport/connection_coordinator.hpp"

#include "ddcs/runtime/reactor.hpp"
#include "ddcs/runtime/timer_source.hpp"
#include "ddcs/logger/log.hpp"
#include "ddcs/proto/frame/frame.hpp"

#include <cassert>
#include <chrono>
#include <utility>

#include <cstddef>
#include <cstdint>

#include <sys/epoll.h>

namespace ddcs::ctrl::infra::transport {

namespace {

constexpr std::size_t pool_chunk{64};
constexpr std::size_t payload_buf_capacity{proto::frame::header_size + proto::frame::max_payload};
constexpr std::uint32_t read_interest{EPOLLIN | EPOLLET};
constexpr std::chrono::nanoseconds pw_timeout{std::chrono::seconds{5}};        // passive_wait: peer FIN 대기 한도
constexpr std::chrono::nanoseconds handshake_timeout{std::chrono::seconds{3}}; // accept -> 첫 프레임 한도

} // namespace

ConnectionCoordinator::ConnectionCoordinator(runtime::Reactor& reactor, runtime::TimerSource& timers)
    : reactor_{reactor}, timers_{timers}, connection_pool_{common::make_pool<Connection>(0, pool_chunk)},
      payload_pool_{common::make_pool<common::LinearBuffer>(0, pool_chunk, payload_buf_capacity)} {}

// --- Outbound (app -> transport) -------------------------------------------

common::PoolHandle<common::LinearBuffer> ConnectionCoordinator::payload_buffer() {
    auto buf = payload_pool_.acquire();
    buf->reserve(proto::frame::header_size); // frame header 자리 미리 확보
    return buf;
}

void ConnectionCoordinator::send(ConnectionId id, std::uint8_t type, common::PoolHandle<common::LinearBuffer> body) {
    auto* conn = find(id);
    if (conn == nullptr || conn->state() != Connection::State::open) {
        return; // 없거나 이미 닫는 중 - 드롭
    }
    if (body->size() > proto::frame::max_payload) {
        assert(false && "body size > max_payload - caller error");
        return; // 드롭: uint16 truncation 방지
    }
    auto const hdr = proto::frame::encode(
        {.magic = proto::frame::magic, .type = type, .length = static_cast<std::uint16_t>(body->size())}
    );
    if (!body->write_front({hdr.data(), hdr.size()})) {
        assert(false && "payload_buffer() 로 받지 않은 버퍼 - headroom 없음");
        return;
    }
    conn->tx_enqueue(std::move(body));
    update_interest(conn); // EPOLLOUT 무장
}

void ConnectionCoordinator::close(ConnectionId id, CloseMode mode) {
    auto* conn = find(id);
    if (conn == nullptr || conn->state() == Connection::State::closing) {
        return; // 없거나 이미 예약됨 (멱등)
    }
    if (mode == CloseMode::force) {
        conn->latch_rst(); // 즉시 RST
        schedule_close(conn);
        return;
    }
    // graceful
    auto const s = conn->state();
    if (s == Connection::State::open) {
        // active close: 새 send 거부(send 가 state!=open 이면 드롭) -> 큐 드레인 -> shutdown(WR) -> passive_wait.
        conn->request_close();
        (void)conn->transition(Connection::State::active_close);
        if (conn->tx_empty()) {
            to_passive_wait(conn); // 드레인 즉시 완료
        } else {
            update_interest(conn); // EPOLLOUT 로 드레인 (on_writable 이 완료 시 pw 진입)
        }
        return;
    }
    if (s == Connection::State::passive_close && !conn->tx_empty()) {
        // passive graceful: 드레인 후 closing (peer 이미 FIN, pw 불필요)
        conn->request_close();
        update_interest(conn);
        return;
    }
    // passive_close tx empty / aborting / etc.: 즉시 closing (FIN, latch 없음)
    schedule_close(conn);
}

void ConnectionCoordinator::handle_timer(runtime::TimerId id) {
    auto const it = timer_slot_.find(id);
    if (it == timer_slot_.end()) {
        return; // 방어 (Reactor 가 취소분은 거르므로 정상 경로엔 없음)
    }
    TimerSlot const slot = it->second;
    timer_slot_.erase(it);
    auto* conn = find(slot.cid);

    switch (slot.kind) {
    case TimerKind::handshake:
        handshake_timer_id_.erase(slot.cid); // 발화로 소비
        // 3초 내 첫 프레임 없음 -> 미확인 연결. (프레임 왔으면 cancel 돼 발화 안 함.)
        if (conn != nullptr && conn->state() == Connection::State::open) {
            LOG_WARN("transport.handshake_timeout", ddcs::logger::kv("id", slot.cid.value()));
            conn->latch_rst();
            schedule_close(conn);
        }
        break;
    case TimerKind::pw:
        pw_timer_id_.erase(slot.cid); // 발화로 소비
        if (conn != nullptr && conn->state() == Connection::State::passive_wait) {
            LOG_WARN("transport.pw_timeout", ddcs::logger::kv("id", slot.cid.value()));
            conn->latch_rst(); // pw TIMEOUT -> RST + closing
            schedule_close(conn);
        }
        break;
    }
}

void ConnectionCoordinator::handle_accept(common::Fd fd, Endpoint peer) {
    auto conn = connection_pool_.acquire();
    ConnectionId const id{++next_id_}; // transport mint (reserve_id 폐기)
    conn->set_coordinator(*this);
    conn->assign(id, std::move(fd), peer, read_interest);

    auto [it, inserted] = connection_map_.try_emplace(id, std::move(conn));
    if (!inserted) {
        assert(false && "ConnectionId 중복 발급");
        return; // conn 핸들 드롭 -> 풀 반납
    }
    Connection* const p = it->second.get();

    if (epoll_add(p)) {
        (void)p->transition(Connection::State::open);
        LOG_DEBUG("transport.accept", ddcs::logger::kv("id", id.value()), ddcs::logger::kv("peer_port", peer.port));
        handler_->on_connect(id);
        // handshake 한도: 3초 내 첫 프레임이 없으면 close. 첫 on_recv 가 cancel.
        runtime::TimerId const tid = timers_.schedule(handshake_timeout, this);
        timer_slot_[tid] = TimerSlot{id, TimerKind::handshake};
        handshake_timer_id_[id] = tid;
    } else {
        LOG_ERROR("transport.epoll_add_failed", ddcs::logger::kv("id", id.value()));
        (void)p->transition(Connection::State::closing); // orphan (app 미매핑)
        pending_close_.push_back(id);
    }
}

void ConnectionCoordinator::handle_event(Connection& conn, std::uint32_t events) {
    Connection* const c = &conn;
    if (c->state() == Connection::State::closing) {
        return; // 이미 reap 예약됨
    }
    if ((events & (EPOLLERR | EPOLLHUP)) != 0u) {
        fail(c, CloseReason::conn_error);
        return;
    }
    if ((events & EPOLLIN) != 0u) {
        handle_readable(c);
        if (c->state() == Connection::State::closing) {
            return;
        }
    }
    if ((events & EPOLLOUT) != 0u) {
        handle_writable(c);
        if (c->state() == Connection::State::closing) {
            return;
        }
    }
    update_interest(c);
}

// open: framing. passive_wait: peer FIN 대기(수신분 폐기, FIN/에러만 본다).
void ConnectionCoordinator::handle_readable(Connection* conn) {
    if (conn->state() == Connection::State::passive_wait) {
        for (;;) {
            auto const r = conn->receive();
            (void)conn->rx_consume(conn->rx_size()); // 폐기 (close 후 on_recv 금지)
            switch (r) {
            case Connection::IoResult::peer_closed:
                schedule_close(conn); // 정상 FIN 완주 -> closing (latch 없음)
                return;
            case Connection::IoResult::error:
                fail(conn, CloseReason::conn_error);
                return;
            case Connection::IoResult::full:
                continue; // 폐기로 공간 확보 -> 더 읽기
            case Connection::IoResult::would_block:
            case Connection::IoResult::ok:
                return;
            }
        }
    }

    while (conn->state() == Connection::State::open) {
        auto const r = conn->receive();

        // framing
        for (;;) {
            if (conn->rx_size() < proto::frame::header_size) {
                return; // 헤더 미달
            }

            proto::frame::HeaderBytes hb{};
            conn->rx_peek({hb.data(), hb.size()});
            auto const h = proto::frame::decode(hb);

            if (h.magic != proto::frame::magic) {
                fail(conn, CloseReason::protocol_error);
                return;
            }

            std::size_t const total = proto::frame::header_size + h.length;
            if (total > inbound_buffer_capacity) {
                fail(conn, CloseReason::protocol_error); // 링에 못 담는 길이 - 손상/악성
                return;
            }
            if (conn->rx_size() < total) {
                return; // 부분 프레임 - 더 기다림
            }

            conn->rx_consume(proto::frame::header_size);
            auto payload = payload_pool_.acquire();
            if (h.length > 0) {
                auto const w = payload->writable();
                conn->rx_read({w.data(), h.length});
                payload->commit(h.length);
            }
            cancel_handshake(conn->id());                              // 첫 완성 프레임 -> handshake 한도 해제
            handler_->on_recv(conn->id(), h.type, std::move(payload)); // type 은 opaque 전달

            if (conn->state() == Connection::State::closing) {
                return; // on_recv 중 app 이 close() 호출
            }
        }

        if (conn->state() != Connection::State::open) {
            return; // framing 이 open 을 벗어나게 함
        }

        switch (r) {
        case Connection::IoResult::would_block:
            return; // 소진 완료
        case Connection::IoResult::full:
            continue; // framing 이 공간 확보 -> 더 읽기
        case Connection::IoResult::peer_closed:
            (void)conn->transition(Connection::State::passive_close);
            handler_->on_close_request(conn->id(), CloseReason::peer_closed); // 1회
            return;
        case Connection::IoResult::error:
            fail(conn, CloseReason::conn_error);
            return;
        case Connection::IoResult::ok:
            return; // receive() 는 반환하지 않음 - 방어
        }
    }
}

// open / passive_close / active_close 에서 송신(드레인). close_requested 면 완료 시 다음 단계.
void ConnectionCoordinator::handle_writable(Connection* conn) {
    auto const s = conn->state();
    if (s != Connection::State::open && s != Connection::State::passive_close && s != Connection::State::active_close) {
        return;
    }
    if (conn->transmit() == Connection::IoResult::error) {
        fail(conn, CloseReason::conn_error); // close_requested 면 fail 이 closing 처리
        return;
    }
    if (conn->close_requested() && conn->tx_empty()) {
        if (conn->state() == Connection::State::active_close) {
            to_passive_wait(conn); // 드레인 완료 -> shutdown(WR) -> passive_wait
        } else {
            schedule_close(conn); // passive_close 드레인 완료 -> closing (FIN)
        }
    }
}

void ConnectionCoordinator::update_interest(Connection* conn) {
    std::uint32_t desired = EPOLLET;
    switch (conn->state()) {
    case Connection::State::open:
        desired |= EPOLLIN; // 양방향
        if (!conn->tx_empty()) {
            desired |= EPOLLOUT;
        }
        break;
    case Connection::State::active_close:
    case Connection::State::passive_close:
        if (!conn->tx_empty()) {
            desired |= EPOLLOUT;
        }
        break;
    case Connection::State::passive_wait:
        desired |= EPOLLIN;
        break;
    default:
        return; // idle / aborting / closing
    }
    if (desired != conn->io_interest()) {
        epoll_mod(conn, desired);
    }
}

// --- epoll

bool ConnectionCoordinator::epoll_add(Connection* conn) {
    if (!reactor_.add(conn->fd(), conn->io_interest(), conn)) {
        return false;
    }
    conn->enter_epoll();
    return true;
}

void ConnectionCoordinator::epoll_mod(Connection* conn, std::uint32_t events) {
    if (!reactor_.mod(conn->fd(), events)) {
        fail(conn, CloseReason::conn_error);
        return;
    }
    conn->set_io_interest(events);
}

void ConnectionCoordinator::epoll_del(Connection* conn) {
    if (!conn->in_epoll()) {
        return;
    }
    reactor_.del(conn->fd());
    conn->leave_epoll();
}

// --- FSM

// shutdown(WR) + passive_wait 전이 + pw 타임아웃 예약.
void ConnectionCoordinator::to_passive_wait(Connection* conn) {
    conn->shutdown_write();
    (void)conn->transition(Connection::State::passive_wait);
    runtime::TimerId const tid = timers_.schedule(pw_timeout, this);
    timer_slot_[tid] = TimerSlot{conn->id(), TimerKind::pw};
    pw_timer_id_[conn->id()] = tid; // 정상 close 시 close_connections 가 cancel
    update_interest(conn);          // pw -> EPOLLIN 만
}

void ConnectionCoordinator::fail(Connection* conn, CloseReason reason) {
    auto const s = conn->state();
    if (s == Connection::State::closing || s == Connection::State::aborting || s == Connection::State::idle) {
        return;
    }
    LOG_WARN(
        "transport.fail", ddcs::logger::kv("id", conn->id().value()),
        ddcs::logger::kv("reason", static_cast<std::uint8_t>(reason))
    );
    conn->latch_rst();
    if (conn->close_requested()) {
        schedule_close(conn);
        return;
    }
    (void)conn->transition(Connection::State::aborting);
    if (s == Connection::State::open) {
        handler_->on_close_request(conn->id(), reason);
    }
}

void ConnectionCoordinator::schedule_close(Connection* conn) {
    if (conn->state() == Connection::State::closing) {
        return; // 멱등
    }
    if (conn->transition(Connection::State::closing)) {
        pending_close_.push_back(conn->id());
    }
}

void ConnectionCoordinator::cancel_handshake(ConnectionId id) {
    auto const it = handshake_timer_id_.find(id);
    if (it == handshake_timer_id_.end()) {
        return; // 첫 프레임 이미 도착했거나 미등록 - 멱등
    }
    timers_.cancel(it->second);
    timer_slot_.erase(it->second);
    handshake_timer_id_.erase(it);
}

void ConnectionCoordinator::close_connections() {
    // 인덱스 순회: reap 중 on_disconnect 가 더 닫아 pending 이 자라도 안전(반복자 무효화 없음).
    for (std::size_t i = 0; i < pending_close_.size(); ++i) {
        ConnectionId const id = pending_close_[i];
        auto* conn = find(id);
        if (conn == nullptr) {
            continue;
        }
        // 남은 타이머(handshake/pw) 정리 - heap 에 죽은 타이머를 안 남긴다.
        cancel_handshake(id);
        if (auto const pit = pw_timer_id_.find(id); pit != pw_timer_id_.end()) {
            timers_.cancel(pit->second);
            timer_slot_.erase(pit->second);
            pw_timer_id_.erase(pit);
        }
        if (conn->in_epoll()) { // 매핑된 정상 연결만 (orphan 은 in_epoll == false)
            handler_->on_disconnect(id);
            epoll_del(conn);
        }
        connection_map_.erase(id); // 핸들 drop -> reset() -> fd close (무장 시 RST)
    }
    pending_close_.clear();
}

Connection* ConnectionCoordinator::find(ConnectionId id) {
    auto const it = connection_map_.find(id);
    return it == connection_map_.end() ? nullptr : it->second.get();
}

} // namespace ddcs::ctrl::infra::transport
