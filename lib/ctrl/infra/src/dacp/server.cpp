#include "ddcs/ctrl/infra/dacp/server.hpp"

#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/app/agent/port/connection_id.hpp"
#include "ddcs/ctrl/app/agent/port/connection_observer.hpp"
#include "ddcs/ctrl/app/agent/port/disconnect_reason.hpp"
#include "ddcs/ctrl/app/agent/port/disconnector.hpp"
#include "ddcs/ctrl/app/agent/port/message_sender.hpp"
#include "ddcs/ctrl/infra/dacp/acceptor.hpp"
#include "ddcs/ctrl/infra/dacp/connection.hpp"
#include "ddcs/dacp/frame/frame.hpp"
#include "ddcs/io/reactor.hpp"
#include "ddcs/logger/log.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <unordered_map>
#include <utility>

namespace ddcs::ctrl::infra::dacp {

namespace frame = ddcs::dacp::frame;

namespace {

constexpr std::size_t connection_pool_chunk_size{64};
constexpr std::size_t message_pool_chunk_size{64};
constexpr std::size_t message_buffer_capacity{frame::header_size + frame::max_length};
constexpr io::ChannelEvents base_connection_interests{io::ChannelEvents::readable | io::ChannelEvents::edge_triggered};

// tx: header 예약 뒤 남는 body 공간이 protocol 최대 길이와 일치해야 한다.
static_assert(message_buffer_capacity - frame::header_size == frame::max_length);
// rx: 최대 길이 frame이 ring에 통째로 들어가야 부분 frame 대기가 끝날 수 있다.
static_assert(frame::header_size + frame::max_length <= Connection::rx_buffer_capacity);

} // namespace

struct Server::Impl final : public port::MessageSender, public port::Disconnector {
    enum class State {
        idle,
        ready,
        active,
    };

    struct ReapEntry {
        port::ConnectionId id;
        port::DisconnectReason reason;
    };

    Impl(Server& owner_ref, io::Reactor& reactor_ref, std::uint16_t port, int backlog)
        : owner{owner_ref}, reactor{reactor_ref}, acceptor{owner_ref, port, backlog},
          connection_pool{common::make_object_pool<Connection>(0, connection_pool_chunk_size)},
          message_pool{common::make_object_pool<common::LinearBuffer>(0, message_pool_chunk_size, message_buffer_capacity)} {}

    [[nodiscard]] bool init(port::ConnectionObserver& observer_ref) noexcept {
        if (state != State::idle) {
            return false;
        }
        if (!acceptor.init()) {
            return false;
        }

        observer = &observer_ref;
        state = State::ready;
        return true;
    }

    [[nodiscard]] bool start() {
        if (state == State::active) {
            return true;
        }
        if (state != State::ready) {
            return false;
        }
        if (!reactor.add(acceptor.channel())) {
            return false;
        }

        state = State::active;
        return true;
    }

    void stop() noexcept {
        if (state != State::active) {
            return;
        }

        reactor.remove(acceptor.channel());
        begin_reap_all(port::DisconnectReason::local_drop);
        drain_reap_queue();
        state = State::ready;
    }

    void close() noexcept {
        if (state == State::idle) {
            return;
        }

        stop();
        acceptor.close();
        observer = nullptr;
        state = State::idle;
    }

    [[nodiscard]] bool active() const noexcept { return state == State::active; }

    [[nodiscard]] std::uint16_t port() const noexcept { return acceptor.port(); }

    void handle_accepted(common::Fd&& fd, PeerAddress peer) {
        auto conn = connection_pool.acquire();
        auto const id = issue_id();
        if (!conn->init(owner, id, peer, std::move(fd), base_connection_interests)) {
            return;
        }

        Connection* stored = conn.get();
        auto [it, inserted] = connections.try_emplace(id, std::move(conn));
        if (!inserted) {
            return;
        }
        bool const tracked = stored->mark_tracked();
        assert(tracked);
        (void)tracked;

        if (!reactor.add(stored->channel())) {
            connections.erase(it);
            return;
        }
        bool const activated = stored->mark_active();
        assert(activated);
        if (!activated) {
            reactor.remove(stored->channel());
            connections.erase(it);
            return;
        }

        notify_connected(id);
        drain_reap_queue();
    }

    void handle_accept_error(int err) noexcept { LOG_WARN("dacp.server.accept_error", logger::kv("errno", err)); }

    void handle_acceptor_failure(io::ChannelEvents events) noexcept {
        LOG_ERROR("dacp.server.acceptor_failure", logger::kv("events", io::to_underlying(events)));
        close(); // 리스닝 fd 고장
    }

    void handle_connection_ready(Connection& connection, io::ChannelEvents events) {
        if (connection.state() != Connection::State::active) {
            return;
        }
        auto const id = connection.id();

        if (io::contains(events, io::ChannelEvents::readable)) {
            handle_readable(connection);
        }

        // on_message 콜백이 disconnect/send로 재진입해 연결을 정리했을 수 있다 - 재조회.
        Connection* conn = find_active(id);
        if (conn == nullptr) {
            drain_reap_queue();
            return;
        }

        if (io::contains(events, io::ChannelEvents::error) || io::contains(events, io::ChannelEvents::hangup)) {
            begin_reap(*conn, port::DisconnectReason::io_error);
            drain_reap_queue();
            return;
        }

        if (io::contains(events, io::ChannelEvents::writable)) {
            handle_writable(*conn);
        }
        if (conn->state() == Connection::State::active) {
            update_interests(*conn);
        }
        drain_reap_queue();
    }

private: // port::MessageSender / port::Disconnector
    [[nodiscard]] port::MessageBuffer make_message_buffer() override {
        auto message = message_pool.acquire();
        bool const reserved = message->reserve_front(frame::header_size); // frame header 자리 미리 확보
        assert(reserved);
        (void)reserved;
        return message;
    }

    void send(port::ConnectionId id, std::uint8_t message_type, port::MessageBuffer message) override {
        if (!message) {
            return;
        }

        Connection* conn = find_active(id);
        if (conn == nullptr) {
            return;
        }

        auto const header = frame::encode(
            {.magic = frame::magic, .type = message_type, .length = static_cast<std::uint16_t>(message->size())}
        );
        if (!message->write_front(header)) {
            assert(false);
            return;
        }

        conn->tx_enqueue(std::move(message));
        update_interests(*conn);
        drain_reap_queue();
    }

    void disconnect(port::ConnectionId id) override {
        Connection* conn = find(id);
        if (conn == nullptr) {
            return;
        }

        begin_reap(*conn, port::DisconnectReason::local_drop);
        drain_reap_queue();
    }

public:
    [[nodiscard]] port::ConnectionId issue_id() noexcept {
        for (;;) {
            port::ConnectionId const id{next_connection_id++};
            if (id.valid() && connections.find(id) == connections.end()) {
                return id;
            }
        }
    }

    [[nodiscard]] Connection* find(port::ConnectionId id) noexcept {
        auto it = connections.find(id);
        return it == connections.end() ? nullptr : it->second.get();
    }

    [[nodiscard]] Connection* find_active(port::ConnectionId id) noexcept {
        Connection* conn = find(id);
        if (conn == nullptr || conn->state() != Connection::State::active) {
            return nullptr;
        }
        return conn;
    }

    void begin_reap_all(port::DisconnectReason reason) {
        for (auto& [id, conn] : connections) {
            (void)id;
            begin_reap(*conn, reason);
        }
    }

    void begin_reap(Connection& conn, port::DisconnectReason reason) {
        if (conn.state() != Connection::State::active) {
            return; // 이미 reap 대기 중(tracked)이거나 셋업 미완
        }

        auto const id = conn.id();
        if (conn.channel().registered()) {
            reactor.remove(conn.channel());
        }

        bool const tracked = conn.mark_tracked();
        assert(tracked);
        (void)tracked;

        reap_queue.push({id, reason});
    }

    void drain_reap_queue() noexcept {
        while (!reap_queue.empty()) {
            ReapEntry const entry = reap_queue.front();
            reap_queue.pop();

            auto it = connections.find(entry.id);
            if (it == connections.end()) {
                continue;
            }

            Connection& conn = *it->second;
            assert(!conn.channel().registered());
            reactor.remove(conn.channel());

            connections.erase(it);
            notify_disconnected(entry.id, entry.reason);
        }
    }

    void update_interests(Connection& conn) {
        if (conn.state() != Connection::State::active || !conn.channel().registered()) {
            return;
        }

        io::ChannelEvents interests = base_connection_interests;
        if (!conn.tx_empty()) {
            interests |= io::ChannelEvents::writable;
        }
        if (conn.channel().interests() == interests) {
            return;
        }
        if (!reactor.modify(conn.channel(), interests)) {
            begin_reap(conn, port::DisconnectReason::io_error);
        }
    }

    // receive와 frame dispatch를 반복: ring이 full이어도 dispatch로 공간을 비우고 이어 읽는다(edge-triggered).
    void handle_readable(Connection& connection) {
        auto const id = connection.id();
        for (;;) {
            Connection* conn = find_active(id);
            if (conn == nullptr) {
                return;
            }

            Connection::IoResult const result = conn->receive();
            dispatch_frames(id); // 결과 처리 전에 도착분 디스패치

            conn = find_active(id); // 콜백 재진입으로 정리됐을 수 있다
            if (conn == nullptr) {
                return;
            }

            switch (result) {
            case Connection::IoResult::ok:
            case Connection::IoResult::would_block:
                return;
            case Connection::IoResult::full:
                continue; // dispatch가 공간을 비웠으니 더 읽는다
            case Connection::IoResult::peer_closed:
                begin_reap(*conn, port::DisconnectReason::peer_closed);
                return;
            case Connection::IoResult::error:
                begin_reap(*conn, port::DisconnectReason::io_error);
                return;
            }
        }
    }

    void handle_writable(Connection& conn) {
        switch (conn.transmit()) {
        case Connection::IoResult::ok:
        case Connection::IoResult::would_block:
            return;
        case Connection::IoResult::full:
        case Connection::IoResult::peer_closed:
            assert(false && "transmit은 full/peer_closed를 반환하지 않는다");
            [[fallthrough]];
        case Connection::IoResult::error:
            begin_reap(conn, port::DisconnectReason::io_error);
            return;
        }
    }

    // rx ring의 완성된 frame을 모두 on_message로 올린다. 콜백 재진입에 대비해 매 반복 재조회.
    void dispatch_frames(port::ConnectionId id) {
        for (;;) {
            Connection* conn = find_active(id);
            if (conn == nullptr) {
                return;
            }
            if (conn->rx_size() < frame::header_size) {
                return; // header 미달
            }

            frame::HeaderBytes header_bytes{};
            conn->rx_peek(header_bytes);
            auto const header = frame::parse(header_bytes);
            if (!header) {
                begin_reap(*conn, port::DisconnectReason::dacp_error);
                return;
            }

            if (header->length > frame::max_length) {
                begin_reap(*conn, port::DisconnectReason::dacp_error); // protocol 한계 초과 - 손상/악성
                return;
            }
            std::size_t const total = frame::header_size + header->length;
            if (conn->rx_size() < total) {
                return; // 부분 frame - 더 기다림
            }

            conn->rx_consume(frame::header_size);
            auto message = message_pool.acquire();
            if (header->length > 0) {
                auto const dst = message->writable();
                conn->rx_read(dst.first(header->length));
                message->commit(header->length);
            }
            notify_message(id, header->type, std::move(message));
        }
    }

    void notify_connected(port::ConnectionId id) noexcept {
        if (observer == nullptr) {
            return;
        }
        try {
            observer->on_connected(id);
        } catch (...) {
            LOG_ERROR("dacp.server.observer_callback_failed", logger::kv("callback", "on_connected"));
            if (Connection* conn = find_active(id)) {
                begin_reap(*conn, port::DisconnectReason::local_drop);
            }
        }
    }

    void notify_message(port::ConnectionId id, std::uint8_t message_type, port::MessageBuffer body) noexcept {
        if (observer == nullptr) {
            return;
        }
        try {
            observer->on_message(id, message_type, std::move(body));
        } catch (...) {
            LOG_ERROR("dacp.server.observer_callback_failed", logger::kv("callback", "on_message"));
            if (Connection* conn = find_active(id)) {
                begin_reap(*conn, port::DisconnectReason::local_drop);
            }
        }
    }

    void notify_disconnected(port::ConnectionId id, port::DisconnectReason reason) noexcept {
        if (observer == nullptr) {
            return;
        }
        try {
            observer->on_disconnected(id, reason);
        } catch (...) {
            LOG_ERROR("dacp.server.observer_callback_failed", logger::kv("callback", "on_disconnected"));
        }
    }

    Server& owner;
    io::Reactor& reactor;
    port::ConnectionObserver* observer{nullptr};
    State state{State::idle};

    Acceptor acceptor;

    common::ObjectPool<Connection> connection_pool;
    common::ObjectPool<common::LinearBuffer> message_pool;

    std::unordered_map<port::ConnectionId, common::PoolHandle<Connection>> connections;
    std::queue<ReapEntry> reap_queue;

    std::uint64_t next_connection_id{1};
};

Server::Server(io::Reactor& reactor, std::uint16_t port, int backlog)
    : impl_{std::make_unique<Impl>(*this, reactor, port, backlog)} {}

Server::~Server() { close(); }

std::uint16_t Server::port() const noexcept { return impl_->port(); }

bool Server::active() const noexcept { return impl_->active(); }

bool Server::init(port::ConnectionObserver& observer) noexcept { return impl_->init(observer); }

bool Server::start() { return impl_->start(); }

void Server::stop() noexcept { impl_->stop(); }

void Server::close() noexcept { impl_->close(); }

port::MessageSender& Server::sender() noexcept { return *impl_; }

port::Disconnector& Server::disconnector() noexcept { return *impl_; }

void Server::handle_accepted(common::Fd&& fd, PeerAddress peer) { impl_->handle_accepted(std::move(fd), peer); }

void Server::handle_accept_error(int err) { impl_->handle_accept_error(err); }

void Server::handle_acceptor_failure(io::ChannelEvents events) { impl_->handle_acceptor_failure(events); }

void Server::handle_connection_ready(Connection& connection, io::ChannelEvents events) {
    impl_->handle_connection_ready(connection, events);
}

} // namespace ddcs::ctrl::infra::dacp
