#pragma once

#include "ddcs/common/fd.hpp"
#include "ddcs/io/channel.hpp"
#include "ddcs/io/channel_events.hpp"
#include "ddcs/io/channel_handler.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace ddcs::ctrl::infra::prometheus {

class Server; // on_ready 위임 대상 (순환 의존 회피)

// 단일 스크레이프 연결. 순수 메커니즘: recv/send + 버퍼 + IoResult 보고.
// 요청 완료 판정/응답 빌드/reap은 Server가 구동(정책 없음). HTTP read -> respond -> close.
class Connection final : private io::ChannelHandler {
public:
    enum class State : std::uint8_t { idle, reading, writing, done };
    enum class IoResult : std::uint8_t { ok, would_block, peer_closed, error };

    Connection() = default;
    ~Connection() override = default;

    Connection(Connection const&) = delete;
    Connection& operator=(Connection const&) = delete;
    Connection(Connection&&) noexcept = delete;
    Connection& operator=(Connection&&) noexcept = delete;

    // 풀에서 꺼내 자원 배정 (idle -> reading). fd 무효/이미 사용 중이면 false.
    [[nodiscard]] bool assign(Server& server, common::Fd fd, io::ChannelEvents interests) noexcept;
    // 풀 반납 (idle로). CAUTION: channel이 reactor에서 제거된 뒤에만 호출할 것.
    void reset() noexcept;

    [[nodiscard]] io::Channel& channel() noexcept { return channel_; }
    [[nodiscard]] int fd() const noexcept { return channel_.fd(); }
    [[nodiscard]] State state() const noexcept { return state_; }

    IoResult receive();                                   // ET 드레인. rx에 누적(cap까지)
    [[nodiscard]] bool request_complete() const noexcept; // "\r\n\r\n" 또는 cap 도달
    void begin_response(std::string http);                // 완성된 HTTP 응답 설정 -> writing
    IoResult transmit();                                  // tx 송신. 다 보내면 done

private:                                                                    // io::ChannelHandler
    void on_ready(io::Channel& channel, io::ChannelEvents events) override; // 정책 없음 -> Server로 위임

private:
    Server* server_{nullptr};
    io::Channel channel_{};
    State state_{State::idle};
    std::string rx_;        // 요청 누적(소량)
    std::string tx_;        // 응답(헤더 + 본문)
    std::size_t tx_pos_{0}; // 송신 커서
};

} // namespace ddcs::ctrl::infra::prometheus
