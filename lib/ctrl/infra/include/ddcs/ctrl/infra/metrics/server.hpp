#pragma once

#include "ddcs/common/fd.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/ctrl/infra/metrics/connection.hpp"
#include "ddcs/io/io_handler.hpp"

#include <unordered_map>
#include <vector>

#include <cstddef>
#include <cstdint>

namespace ddcs::io {
class Reactor;
}
namespace ddcs::ctrl::port::metrics {
class Inbound;
}

namespace ddcs::ctrl::infra::metrics {

using ddcs::ctrl::port::metrics::Inbound;

// metrics 스크레이프 listen 엔드포인트 - reactor 의 두 번째 guest.
// listen fd 의 IoHandler(accept) + per-conn Connection 오케스트레이션. HTTP read -> respond -> close.
// gen-token reactor 가 디스패치 안전을 보장; self-close 는 entry-point 끝 reap 으로(coordinator 미러).
class Server final : public io::IoHandler {
public:
    Server(io::Reactor& reactor, Inbound& provider, std::uint16_t port, int backlog);
    ~Server() override;

    Server(Server const&) = delete;
    Server& operator=(Server const&) = delete;
    Server(Server&&) noexcept = delete;
    Server& operator=(Server&&) noexcept = delete;

    void start();               // socket/bind/listen + reactor.add(listen_fd)
    std::uint16_t port() const; // 실제 바인드 포트(ephemeral 확인용)

    void on_io(std::uint32_t events) override;             // listen fd: accept + reap
    void on_event(Connection& conn, std::uint32_t events); // conn fd(Connection::on_io 위임): dispatch + reap

    std::size_t size() const noexcept { return conns_.size(); }

private:
    void accept_loop();
    void dispatch_event(Connection& conn, std::uint32_t events);
    void respond(Connection& conn); // provider_.scrape() -> HTTP 응답
    void schedule_close(Connection& conn);
    void reap();
    Connection* find(int fd);

    io::Reactor& reactor_;
    Inbound& provider_;
    std::uint16_t listen_port_;
    int const backlog_;
    common::Fd listen_fd_{};
    common::ObjectPool<Connection> pool_;
    std::unordered_map<int, common::PoolHandle<Connection>> conns_; // fd -> conn
    std::vector<int> pending_close_;
};

} // namespace ddcs::ctrl::infra::metrics
