#include "ddcs/ctrl/infra/transport/server.hpp"

#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/app/transport/port/connection_id.hpp"
#include "ddcs/ctrl/app/transport/port/connection_listener.hpp"
#include "ddcs/ctrl/app/transport/port/disconnect_reason.hpp"
#include "ddcs/ctrl/app/transport/port/disconnector.hpp"
#include "ddcs/ctrl/app/transport/port/message_receiver.hpp"
#include "ddcs/ctrl/app/transport/port/message_sender.hpp"
#include "ddcs/ctrl/infra/transport/acceptor.hpp"
#include "ddcs/ctrl/infra/transport/connection.hpp"
#include "ddcs/io/reactor.hpp"
#include "ddcs/logger/log.hpp"
#include "ddcs/wire/frame/extract.hpp"
#include "ddcs/wire/frame/frame.hpp"
#include "ddcs/wire/frame/seal.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <unordered_map>
#include <utility>

namespace ddcs::ctrl::infra::transport {

namespace wire = ddcs::wire;

namespace {

constexpr std::size_t connection_pool_chunk_size = 64;
constexpr std::size_t message_pool_chunk_size = 64;
constexpr io::ChannelEvents base_connection_interests =
    io::ChannelEvents::readable | io::ChannelEvents::edge_triggered;

// 최대 frame(header + max payload)이 rx ring에 통째로 들어가야
// 부분 frame 대기가 끝난다(아니면 framing 교착).
static_assert(
    wire::frame::header_size + wire::frame::max_payload_length <= Connection::rx_buffer_capacity,
    "max frame (header + max_payload_length) must fit in the rx ring buffer"
);

} // namespace

class Server::Impl final : public port::Disconnector, public port::MessageSender {
public:
    enum class State {
        idle = 0x00,
        ready = 0x01,
        active = 0x02,
    };

    struct ReapEntry {
        port::ConnectionId id;
        port::DisconnectReason reason;
    };

    Impl(Server& owner, io::Reactor& reactor, std::uint16_t port, int backlog)
        : owner_(owner),
          reactor_(reactor),
          acceptor_(Acceptor{owner, port, backlog}),
          connection_pool_(common::ObjectPool<Connection>::create<connection_pool_chunk_size>()),
          message_pool_(
              common::ObjectPool<common::LinearBuffer>::create<message_pool_chunk_size>(
                  wire::frame::header_size + wire::frame::max_payload_length
              )
          ) {}

    void disconnect(port::ConnectionId id) override {
        Connection* conn = find(id);
        if (conn == nullptr) {
            return;
        }

        begin_reap(*conn, port::DisconnectReason::local_drop);
        drain_reap_queue();
    }

    [[nodiscard]] port::MessageBuffer make_message_buffer() override {
        auto message = message_pool_.acquire();
        bool const reserved =
            wire::frame::reserve_header_room(*message); // frame header 자리 미리 확보
        assert(reserved);
        (void)reserved;
        return message;
    }

    void send(port::ConnectionId id, port::MessageBuffer message) override {
        if (!message) {
            return;
        }

        Connection* conn = find_active(id);
        if (conn == nullptr) {
            return;
        }

        if (!wire::frame::seal(*message)) {
            assert(false); // make_message_buffer 용량/headroom 계약 위반. 버그 신호
            return;
        }

        conn->tx_enqueue(std::move(message));
        update_interests(*conn);
        drain_reap_queue();
    }

    [[nodiscard]] bool
    init(port::ConnectionListener& listener, port::MessageReceiver& receiver) noexcept {
        if (state_ != State::idle) {
            return false;
        }
        if (!acceptor_.init()) {
            return false;
        }

        listener_ = &listener;
        receiver_ = &receiver;
        state_ = State::ready;
        return true;
    }

    [[nodiscard]] bool start() {
        if (state_ == State::active) {
            return true;
        }
        if (state_ != State::ready) {
            return false;
        }
        if (!reactor_.add(acceptor_.channel())) {
            return false;
        }

        state_ = State::active;
        return true;
    }

    void stop() noexcept {
        if (state_ != State::active) {
            return;
        }

        reactor_.remove(acceptor_.channel());
        begin_reap_all(port::DisconnectReason::local_drop);
        drain_reap_queue();
        state_ = State::ready;
    }

    void close() noexcept {
        if (state_ == State::idle) {
            return;
        }

        stop();
        acceptor_.close();
        listener_ = nullptr;
        receiver_ = nullptr;
        state_ = State::idle;
    }

    [[nodiscard]] std::uint16_t port() const noexcept {
        return acceptor_.port();
    }

    [[nodiscard]] bool active() const noexcept {
        return state_ == State::active;
    }

    void handle_accepted(io::Fd&& fd, PeerAddress peer) {
        auto conn = connection_pool_.acquire();
        auto const id = issue_id();
        if (!conn->init(owner_, id, peer, std::move(fd), base_connection_interests)) {
            return;
        }

        Connection* stored = conn.get();
        auto [it, inserted] = connections_.try_emplace(id, std::move(conn));
        if (!inserted) {
            return;
        }
        bool const tracked = stored->mark_tracked();
        assert(tracked);
        (void)tracked;

        if (!reactor_.add(stored->channel())) {
            connections_.erase(it);
            return;
        }
        bool const activated = stored->mark_active();
        assert(activated);
        if (!activated) {
            reactor_.remove(stored->channel());
            connections_.erase(it);
            return;
        }

        notify_connected(id);
        drain_reap_queue();
    }

    void handle_accept_error(int err) noexcept {
        LOG_WARN("transport.server.accept_error", logger::kv("errno", err));
    }

    void handle_acceptor_failure(io::ChannelEvents events) noexcept {
        LOG_ERROR(
            "transport.server.acceptor_failure", logger::kv("events", io::to_underlying(events))
        );
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

        // on_message 콜백이 disconnect/send로 재진입해 연결을 정리했을 수 있어 재조회한다.
        Connection* conn = find_active(id);
        if (conn == nullptr) {
            drain_reap_queue();
            return;
        }

        if (io::contains(events, io::ChannelEvents::error) ||
            io::contains(events, io::ChannelEvents::hangup)) {
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

private:
    [[nodiscard]] port::ConnectionId issue_id() noexcept {
        for (;;) {
            port::ConnectionId const id{next_connection_id_++};
            if (id.valid() && connections_.find(id) == connections_.end()) {
                return id;
            }
        }
    }

    [[nodiscard]] Connection* find(port::ConnectionId id) noexcept {
        auto it = connections_.find(id);
        return it == connections_.end() ? nullptr : it->second.get();
    }

    [[nodiscard]] Connection* find_active(port::ConnectionId id) noexcept {
        Connection* conn = find(id);
        if (conn == nullptr || conn->state() != Connection::State::active) {
            return nullptr;
        }
        return conn;
    }

    void begin_reap_all(port::DisconnectReason reason) {
        for (auto& [id, conn] : connections_) {
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
            reactor_.remove(conn.channel());
        }

        bool const tracked = conn.mark_tracked();
        assert(tracked);
        (void)tracked;

        reap_queue_.push({id, reason});
    }

    void drain_reap_queue() noexcept {
        while (!reap_queue_.empty()) {
            ReapEntry const entry = reap_queue_.front();
            reap_queue_.pop();

            auto it = connections_.find(entry.id);
            if (it == connections_.end()) {
                continue;
            }

            Connection& conn = *it->second;
            assert(!conn.channel().registered());
            reactor_.remove(conn.channel());

            connections_.erase(it);
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
        if (!reactor_.modify(conn.channel(), interests)) {
            begin_reap(conn, port::DisconnectReason::io_error);
        }
    }

    // receive와 frame dispatch를 반복:
    // - ring이 full이어도 dispatch로 공간을 비우고 이어 읽는다(edge-triggered).
    void handle_readable(Connection& connection) {
        auto const id = connection.id();
        for (;;) {
            Connection* conn = find_active(id);
            if (conn == nullptr) {
                return;
            }

            Connection::IoResult const result = conn->receive();
            dispatch_frames(id); // 결과 처리 전에 도착분 디스패치

            conn = find_active(id); // 콜백 재진입으로 정리됐을 수 있다.
            if (conn == nullptr) {
                return;
            }

            switch (result) {
            case Connection::IoResult::ok:
            case Connection::IoResult::would_block:
                return;
            case Connection::IoResult::full:
                continue; // dispatch가 공간을 비웠으니 더 읽는다.
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

    // rx ring의 완성된 frame을 모두 on_message로 올린다(agent Connector::framing과 공유 루프).
    // get_rx가 매 반복 find_active로 재조회하므로 콜백(notify_message) 재진입에 안전하다.
    void dispatch_frames(port::ConnectionId id) {
        wire::frame::extract_frames(
            message_pool_,
            [&]() -> common::CircularBuffer* {
                Connection* conn = find_active(id);
                return conn != nullptr ? &conn->rx_buffer() : nullptr;
            },
            [&](port::MessageBuffer payload) { notify_message(id, std::move(payload)); },
            [&](wire::frame::PullResult) {
                if (Connection* conn = find_active(id)) {
                    begin_reap(*conn, port::DisconnectReason::protocol_error);
                }
            }
        );
    }

    void notify_connected(port::ConnectionId id) noexcept {
        if (listener_ == nullptr) {
            return;
        }
        try {
            listener_->on_connected(id);
        } catch (...) {
            LOG_ERROR(
                "transport.server.observer_callback_failed", logger::kv("callback", "on_connected")
            );
            if (Connection* conn = find_active(id)) {
                begin_reap(*conn, port::DisconnectReason::local_drop);
            }
        }
    }

    void notify_message(port::ConnectionId id, port::MessageBuffer payload) noexcept {
        if (receiver_ == nullptr) {
            return;
        }
        try {
            receiver_->on_message(id, std::move(payload));
        } catch (...) {
            LOG_ERROR(
                "transport.server.observer_callback_failed", logger::kv("callback", "on_message")
            );
            if (Connection* conn = find_active(id)) {
                begin_reap(*conn, port::DisconnectReason::local_drop);
            }
        }
    }

    void notify_disconnected(port::ConnectionId id, port::DisconnectReason reason) noexcept {
        if (listener_ == nullptr) {
            return;
        }
        try {
            listener_->on_disconnected(id, reason);
        } catch (...) {
            LOG_ERROR(
                "transport.server.observer_callback_failed",
                logger::kv("callback", "on_disconnected")
            );
        }
    }

    Server& owner_;
    io::Reactor& reactor_;
    port::ConnectionListener* listener_ = nullptr;
    port::MessageReceiver* receiver_ = nullptr;
    State state_ = State::idle;

    Acceptor acceptor_;

    common::ObjectPool<Connection> connection_pool_;
    common::ObjectPool<common::LinearBuffer> message_pool_;

    std::unordered_map<port::ConnectionId, common::PoolHandle<Connection>> connections_;
    std::queue<ReapEntry> reap_queue_;

    std::uint64_t next_connection_id_ = 1;
};

Server::Server(io::Reactor& reactor, std::uint16_t port, int backlog)
    : impl_(std::make_unique<Impl>(*this, reactor, port, backlog)) {}

Server::~Server() {
    close();
}

bool Server::init(port::ConnectionListener& listener, port::MessageReceiver& receiver) noexcept {
    return impl_->init(listener, receiver);
}

bool Server::start() {
    return impl_->start();
}

void Server::stop() noexcept {
    impl_->stop();
}

void Server::close() noexcept {
    impl_->close();
}

port::Disconnector& Server::disconnector() noexcept {
    return *impl_;
}

port::MessageSender& Server::sender() noexcept {
    return *impl_;
}

std::uint16_t Server::port() const noexcept {
    return impl_->port();
}

bool Server::active() const noexcept {
    return impl_->active();
}

void Server::handle_accepted(io::Fd&& fd, PeerAddress peer) {
    impl_->handle_accepted(std::move(fd), peer);
}

void Server::handle_accept_error(int err) {
    impl_->handle_accept_error(err);
}

void Server::handle_acceptor_failure(io::ChannelEvents events) {
    impl_->handle_acceptor_failure(events);
}

void Server::handle_connection_ready(Connection& connection, io::ChannelEvents events) {
    impl_->handle_connection_ready(connection, events);
}

} // namespace ddcs::ctrl::infra::transport
