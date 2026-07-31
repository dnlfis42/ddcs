#pragma once

#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/infra/prometheus/connection.hpp"
#include "ddcs/io/channel.hpp"
#include "ddcs/io/channel_events.hpp"
#include "ddcs/io/channel_handler.hpp"
#include "ddcs/io/sys_result.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ddcs::io {

class Reactor;

} // namespace ddcs::io

namespace ddcs::ctrl::app::metrics::port {

class PrometheusSource;

} // namespace ddcs::ctrl::app::metrics::port

namespace ddcs::ctrl::infra::prometheus {

namespace port = ddcs::ctrl::app::metrics::port;

// metrics 스크레이프 listen 엔드포인트
//
// 스크랩은 저빈도 best-effort
class Server final : private io::ChannelHandler {
public:
    Server(
        io::Reactor& reactor, port::PrometheusSource& source, std::uint16_t listen_port, int backlog
    );
    ~Server() override;

    Server(Server const&) = delete;
    Server& operator=(Server const&) = delete;
    Server(Server&&) noexcept = delete;
    Server& operator=(Server&&) noexcept = delete;

    // socket/bind/listen. 실패는 WARN + errno를 실은 실패 반환
    [[nodiscard]] io::SysResult init() noexcept;
    // listen channel을 reactor에 등록
    [[nodiscard]] io::SysResult start();
    // reactor에서 제거 + 연결 전부 정리
    void stop() noexcept;
    void close() noexcept;

    // 실제 바인드 포트(ephemeral 확인용)
    [[nodiscard]] std::uint16_t port() const noexcept {
        return bound_port_;
    }

    [[nodiscard]] bool active() const noexcept {
        return listen_channel_.registered();
    }

    [[nodiscard]] std::size_t connection_count() const noexcept {
        return connections_.size();
    }

    void on_connection_event(Connection& conn, io::ChannelEvents events);

private:
    void on_ready(io::Channel& channel, io::ChannelEvents events) override;
    void accept_connections();

    void schedule_reap(Connection& conn);
    void reap_scheduled();

private:
    io::Reactor& reactor_;

    port::PrometheusSource& source_;

    std::uint16_t listen_port_;
    std::uint16_t bound_port_ = 0;
    int const backlog_;

    io::Channel listen_channel_;

    common::ObjectPool<Connection> connection_pool_;
    std::unordered_map<int, common::PoolHandle<Connection>> connections_; // conn fd 키
    std::vector<int> reap_queue_;
};

} // namespace ddcs::ctrl::infra::prometheus
