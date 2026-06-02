#pragma once

#include "ddcs/common/fd.hpp"
#include "ddcs/io/io_handler.hpp"

#include <string>

#include <cstddef>
#include <cstdint>

namespace ddcs::ctrl::infra::metrics {

class Server; // on_io 위임 대상 (순환 의존 회피)

// 단일 스크레이프 연결. 순수 메커니즘: recv/send + 버퍼 + IoResult 보고.
// 요청 완료 판정/응답 빌드/reap 은 Server 가 구동(정책 없음). HTTP read -> respond -> close.
class Connection final : public io::IoHandler {
public:
    enum class State : std::uint8_t { idle, reading, writing, done };
    enum class IoResult : std::uint8_t { ok, would_block, peer_closed, error };

    Connection() = default;
    ~Connection() override = default;

    Connection(Connection const&) = delete;
    Connection& operator=(Connection const&) = delete;
    Connection(Connection&&) noexcept = delete;
    Connection& operator=(Connection&&) noexcept = delete;

    void on_io(std::uint32_t events) override; // 정책 없음 -> Server 로 위임

    void set_server(Server& server) noexcept { server_ = &server; }
    void assign(common::Fd fd) noexcept; // 풀에서 꺼내 자원 배정 (idle -> reading)
    void reset() noexcept;               // 풀 반납 (idle 로)

    int fd() const noexcept { return fd_.get(); }
    State state() const noexcept { return state_; }
    bool in_epoll() const noexcept { return in_epoll_; }
    void enter_epoll() noexcept { in_epoll_ = true; }
    void leave_epoll() noexcept { in_epoll_ = false; }

    IoResult receive();                     // ET 드레인. rx 에 누적(cap 까지).
    bool request_complete() const noexcept; // "\r\n\r\n" 또는 cap 도달
    void begin_response(std::string http);  // 완성된 HTTP 응답 설정 -> writing
    IoResult transmit();                    // tx 송신. 다 보내면 done.

private:
    Server* server_{nullptr};
    common::Fd fd_{};
    State state_{State::idle};
    bool in_epoll_{false};
    std::string rx_;        // 요청 누적(소량)
    std::string tx_;        // 응답(헤더+본문)
    std::size_t tx_pos_{0}; // 송신 커서
};

} // namespace ddcs::ctrl::infra::metrics
