#pragma once

#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/infra/prometheus/connection.hpp"
#include "ddcs/io/channel.hpp"
#include "ddcs/io/channel_events.hpp"
#include "ddcs/io/channel_handler.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ddcs::io {

class Reactor;

} // namespace ddcs::io

namespace ddcs::ctrl::app::metrics::port {

class MetricsSource;

} // namespace ddcs::ctrl::app::metrics::port

namespace ddcs::ctrl::infra::prometheus {

namespace port = ddcs::ctrl::app::metrics::port;

// metrics 스크레이프 listen 엔드포인트 (reactor의 2nd guest)
// - HTTP read 후 MetricsSource::scrape, respond 후 close
// - 스크레이프는 저빈도 best-effort
class Server final : private io::ChannelHandler {
public:
    Server(
        io::Reactor& reactor, port::MetricsSource& source, std::uint16_t listen_port, int backlog
    );
    ~Server() override;

    Server(Server const&) = delete;
    Server& operator=(Server const&) = delete;
    Server(Server&&) noexcept = delete;
    Server& operator=(Server&&) noexcept = delete;

    // socket/bind/listen. 실패는 WARN + false
    [[nodiscard]] bool init() noexcept;
    // listen channel을 reactor에 등록
    [[nodiscard]] bool start();
    // reactor에서 제거 + 연결 전부 정리
    void stop() noexcept;
    void close() noexcept;

    // 실제 바인드 포트(ephemeral 확인용)
    [[nodiscard]] std::uint16_t port() const noexcept {
        return bound_port_;
    }

    [[nodiscard]] bool active() const noexcept {
        return state_ == State::active;
    }

    [[nodiscard]] std::size_t connection_count() const noexcept {
        return connections_.size();
    }

    void handle_connection_ready(Connection& conn, io::ChannelEvents events);

private:
    void on_ready(io::Channel& channel, io::ChannelEvents events) override;

    enum class State : std::uint8_t {
        idle,
        ready,
        active,
    };

    void drain_accepts();
    void dispatch(Connection& conn, io::ChannelEvents events);
    // source_.scrape()로 HTTP 응답 빌드
    void respond(Connection& conn);
    void schedule_close(Connection& conn);
    void reap();
    [[nodiscard]] Connection* find(int fd);

private:
    io::Reactor& reactor_;
    port::MetricsSource& source_;
    std::uint16_t listen_port_;
    std::uint16_t bound_port_ = 0;
    int const backlog_;
    State state_ = State::idle;
    io::Channel listen_channel_;
    common::ObjectPool<Connection> pool_;
    std::unordered_map<int, common::PoolHandle<Connection>> connections_; // conn fd 키
    std::vector<int> pending_close_;
};

} // namespace ddcs::ctrl::infra::prometheus
