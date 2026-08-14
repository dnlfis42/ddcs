#include "ddcs/ctrl/infra/transport/server.hpp"

#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/app/transport/port/connection_id.hpp"
#include "ddcs/ctrl/app/transport/port/connection_listener.hpp"
#include "ddcs/ctrl/app/transport/port/disconnector.hpp"
#include "ddcs/ctrl/app/transport/port/message_receiver.hpp"
#include "ddcs/ctrl/app/transport/port/message_sender.hpp"
#include "ddcs/ctrl/app/transport/port/transport_stats.hpp"
#include "ddcs/ctrl/infra/transport/acceptor.hpp"
#include "ddcs/ctrl/infra/transport/connection.hpp"
#include "ddcs/io/reactor.hpp"
#include "ddcs/logger/event.hpp"
#include "ddcs/wire/frame/frame.hpp"

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

} // namespace

class Server::Impl final : public port::Disconnector,
                           public port::MessageSender,
                           public port::TransportStatsSource {
public:
    struct ReapEntry {
        port::ConnectionId id;
        port::DisconnectReason reason;
    };

    Impl(
        Server& owner, io::Reactor& reactor, std::uint16_t port, int backlog,
        std::size_t rx_buffer_size
    )
        : owner_(owner),
          reactor_(reactor),
          acceptor_(Acceptor{owner, port, backlog}),
          connection_pool_(common::ObjectPool<Connection>::create<connection_pool_chunk_size>(
              wire::frame::fit_rx_capacity(rx_buffer_size)
          )),
          message_pool_(common::ObjectPool<common::LinearBuffer>::create<message_pool_chunk_size>(
              wire::frame::max_frame_size
          )) {
        if (auto const fitted = wire::frame::fit_rx_capacity(rx_buffer_size);
            fitted != rx_buffer_size) {
            LOG_TRANSPORT_RX_BUFFER_ADJUST(rx_buffer_size, fitted);
        }
    }

    void disconnect(port::ConnectionId id, port::DisconnectReason reason) override {
        Connection* conn = find(id);
        if (conn == nullptr) {
            return;
        }

        schedule_reap(*conn, reason);
        reap_scheduled();
    }

    // scrape 시점 스냅샷. 연결 수에 비례하는 순회지만 scrape 주기(수 초~수십 초)에만 돈다.
    [[nodiscard]] port::TransportStats transport_stats() const override {
        port::TransportStats stats{};
        for (auto const& [id, conn] : connections_) {
            stats.tx_queued_messages += conn->tx_queued();
        }
        stats.connection_pool_capacity = connection_pool_.capacity();
        stats.connection_pool_acquired = connection_pool_.acquired_count();
        stats.message_pool_capacity = message_pool_.capacity();
        stats.message_pool_acquired = message_pool_.acquired_count();
        return stats;
    }

    [[nodiscard]] port::MessageBuffer make_message_buffer() override {
        auto message = message_pool_.acquire();
        // frame header 자리 확보. 실패해도 여기서는 알리지 않는다. 그 버퍼는 프레이밍이 안 되므로
        // send의 encode_frame이 같은 사실을 한 번 알린다.
        (void)message->set_headroom(wire::frame::header_size);
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

        if (!wire::frame::encode_frame(*message)) {
            // payload 상한 초과 또는 make_message_buffer()를 거치지 않은 버퍼(프로그래머 오류)
            LOG_TRANSPORT_FRAME_ENCODE_FAIL_CONN(id.get(), message->data_span().size());
            return;
        }

        conn->tx_enqueue(std::move(message));
        update_interests(*conn);
        reap_scheduled();
    }

    [[nodiscard]] io::SysResult
    init(port::ConnectionListener& listener, port::MessageReceiver& receiver) noexcept {
        if (acceptor_.valid()) {
            return io::SysResult::fail(); // 이중 init
        }
        if (auto const result = acceptor_.init(); !result) {
            return result;
        }

        listener_ = &listener;
        receiver_ = &receiver;
        return io::SysResult::success();
    }

    [[nodiscard]] io::SysResult start() {
        if (acceptor_.registered()) {
            return io::SysResult::success();
        }
        if (!acceptor_.valid()) {
            return io::SysResult::fail(); // init 전 start
        }

        return reactor_.add(acceptor_.channel());
    }

    void stop() noexcept {
        if (!acceptor_.registered()) {
            return;
        }

        reactor_.remove(acceptor_.channel());
        schedule_reap_all(port::DisconnectReason::shutdown);
        reap_scheduled();
    }

    void close() noexcept {
        if (!acceptor_.valid()) {
            return;
        }

        stop();
        acceptor_.close();
        listener_ = nullptr;
        receiver_ = nullptr;
    }

    [[nodiscard]] std::uint16_t port() const noexcept {
        return acceptor_.port();
    }

    [[nodiscard]] bool active() const noexcept {
        return acceptor_.registered();
    }

    // 연결 조립 중 예외(슬롯/노드 할당 실패)는 이 연결 하나를 포기하는 선에서 막는다.
    // 풀 핸들과 Fd의 RAII가 되돌리므로 중간에 빠져나가도 남는 상태가 없다.
    void on_accepted(io::Fd&& fd, PeerAddress peer) {
        try {
            auto conn = connection_pool_.acquire();
            auto const id = issue_id();
            conn->init(owner_, id, peer, std::move(fd), base_connection_interests);

            Connection* stored = conn.get();
            auto [it, inserted] = connections_.try_emplace(id, std::move(conn));
            if (!inserted) {
                LOG_TRANSPORT_CONNECTION_DUPLICATE(id.get());
                return;
            }
            if (auto const result = reactor_.add(stored->channel()); !result) {
                LOG_TRANSPORT_CONNECTION_REGISTER_FAIL(id.get(), result.err);
                connections_.erase(it); // 핸들 drop이 reset로 슬롯 반환
                return;
            }

            notify_connected(id);
        } catch (...) {
            LOG_TRANSPORT_CONNECTION_SETUP_FAIL();
        }

        reap_scheduled();
    }

    void on_accept_error(int err) noexcept {
        LOG_TRANSPORT_ACCEPT_FAIL(err);
    }

    void on_acceptor_failure(io::ChannelEvents events) noexcept {
        LOG_TRANSPORT_LISTEN_FAIL(io::to_underlying(events));
        close(); // 리스닝 fd 고장
    }

    void on_connection_event(Connection& connection, io::ChannelEvents events) {
        do {
            if (!connection.registered()) {
                break;
            }
            auto const id = connection.id();

            if (io::contains(events, io::ChannelEvents::readable)) {
                handle_readable(connection);
            }

            // on_message 콜백이 disconnect/send로 재진입해 연결을 정리했을 수 있어 재조회한다.
            Connection* conn = find_active(id);
            if (conn == nullptr) {
                break;
            }

            if (io::contains(events, io::ChannelEvents::error) ||
                io::contains(events, io::ChannelEvents::hangup)) {
                schedule_reap(*conn, port::DisconnectReason::io_error);
                break;
            }

            if (io::contains(events, io::ChannelEvents::writable)) {
                handle_writable(*conn);
            }
            if (conn->registered()) {
                update_interests(*conn);
            }
        } while (false);

        reap_scheduled();
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
        if (conn == nullptr || !conn->registered()) {
            return nullptr;
        }
        return conn;
    }

    void schedule_reap(Connection& conn, port::DisconnectReason reason) {
        // 이미 reap 대기 중이거나 셋업 미완
        if (!conn.registered()) {
            return;
        }

        auto const id = conn.id();
        reactor_.remove(conn.channel());
        reap_queue_.push({id, reason});
    }

    void schedule_reap_all(port::DisconnectReason reason) {
        for (auto& [id, conn] : connections_) {
            (void)id;
            schedule_reap(*conn, reason);
        }
    }

    void reap_scheduled() noexcept {
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
        if (!conn.registered()) {
            return;
        }

        io::ChannelEvents interests = base_connection_interests;
        if (!conn.tx_empty()) {
            interests |= io::ChannelEvents::writable;
        }
        if (conn.channel().interests() == interests) {
            return;
        }
        if (auto const result = reactor_.modify(conn.channel(), interests); !result) {
            LOG_TRANSPORT_REACTOR_MODIFY_FAIL_CONN(conn.id().get(), result.err);
            schedule_reap(conn, port::DisconnectReason::io_error);
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

            net::ReceiveResult const result = conn->receive();

            wire::frame::dispatch_frames(
                message_pool_,
                [&]() -> common::CircularBuffer* {
                    Connection* active = find_active(id);
                    return active != nullptr ? &active->rx_buffer() : nullptr;
                },
                [&](port::MessageBuffer payload) { notify_message(id, std::move(payload)); },
                [&](wire::frame::DecodeResult reason) {
                    // bad_magic/too_long은 상대가 잘못 보낸 것이고, read_error는 우리 ring
                    // 로직이 어긋난 것이라 레벨이 갈린다. 뭉개면 우리 버그가 상대 탓으로 남는다.
                    if (reason == wire::frame::DecodeResult::read_error) {
                        LOG_TRANSPORT_FRAME_DECODE_CORRUPT_CONN(id.get());
                    } else {
                        LOG_TRANSPORT_FRAME_DECODE_FAIL_CONN(
                            id.get(), wire::frame::to_string(reason)
                        );
                    }
                    if (Connection* active = find_active(id)) {
                        schedule_reap(*active, port::DisconnectReason::frame_error);
                    }
                }
            );

            conn = find_active(id); // 콜백 재진입으로 정리됐을 수 있다.
            if (conn == nullptr) {
                return;
            }

            switch (result.code) {
            case net::ReceiveResult::Code::would_block:
                return;
            case net::ReceiveResult::Code::full:
                continue; // dispatch가 공간을 비웠으니 더 읽는다.
            case net::ReceiveResult::Code::peer_closed:
                schedule_reap(*conn, port::DisconnectReason::peer_closed);
                return;
            case net::ReceiveResult::Code::error:
                LOG_TRANSPORT_RECEIVE_FAIL_CONN(id.get(), result.err);
                schedule_reap(*conn, port::DisconnectReason::io_error);
                return;
            }
        }
    }

    void handle_writable(Connection& conn) {
        auto const result = conn.transmit();
        switch (result.code) {
        case net::TransmitResult::Code::drained:
        case net::TransmitResult::Code::would_block:
            return;
        case net::TransmitResult::Code::error:
            LOG_TRANSPORT_SEND_FAIL_CONN(conn.id().get(), result.err);
            schedule_reap(conn, port::DisconnectReason::io_error);
            return;
        }
    }

    void notify_connected(port::ConnectionId id) noexcept {
        if (listener_ == nullptr) {
            return;
        }

        try {
            listener_->on_connected(id);
        } catch (...) {
            LOG_TRANSPORT_CONNECTION_NOTIFY_FAIL(id.get(), "connect");
            if (Connection* conn = find_active(id)) {
                schedule_reap(*conn, port::DisconnectReason::internal_error);
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
            LOG_TRANSPORT_CONNECTION_NOTIFY_FAIL(id.get(), "message");
            if (Connection* conn = find_active(id)) {
                schedule_reap(*conn, port::DisconnectReason::internal_error);
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
            LOG_TRANSPORT_CONNECTION_NOTIFY_FAIL(id.get(), "disconnect");
        }
    }

    Server& owner_;
    io::Reactor& reactor_;
    port::ConnectionListener* listener_ = nullptr;
    port::MessageReceiver* receiver_ = nullptr;

    Acceptor acceptor_;

    common::ObjectPool<Connection> connection_pool_;
    common::ObjectPool<common::LinearBuffer> message_pool_;

    std::unordered_map<port::ConnectionId, common::PoolHandle<Connection>> connections_;
    std::queue<ReapEntry> reap_queue_;

    std::uint64_t next_connection_id_ = 1;
};

Server::Server(io::Reactor& reactor, std::uint16_t port, int backlog, std::size_t rx_buffer_size)
    : impl_(std::make_unique<Impl>(*this, reactor, port, backlog, rx_buffer_size)) {}

Server::~Server() {
    close();
}

io::SysResult
Server::init(port::ConnectionListener& listener, port::MessageReceiver& receiver) noexcept {
    return impl_->init(listener, receiver);
}

io::SysResult Server::start() {
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

port::TransportStatsSource& Server::stats_source() noexcept {
    return *impl_;
}

std::uint16_t Server::port() const noexcept {
    return impl_->port();
}

bool Server::active() const noexcept {
    return impl_->active();
}

void Server::on_accepted(io::Fd&& fd, PeerAddress peer) {
    impl_->on_accepted(std::move(fd), peer);
}

void Server::on_accept_error(int err) {
    impl_->on_accept_error(err);
}

void Server::on_acceptor_failure(io::ChannelEvents events) {
    impl_->on_acceptor_failure(events);
}

void Server::on_connection_event(Connection& connection, io::ChannelEvents events) {
    impl_->on_connection_event(connection, events);
}

} // namespace ddcs::ctrl::infra::transport
