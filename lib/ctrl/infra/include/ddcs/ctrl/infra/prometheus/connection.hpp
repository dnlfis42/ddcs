#pragma once

#include "ddcs/io/channel.hpp"
#include "ddcs/io/channel_events.hpp"
#include "ddcs/io/channel_handler.hpp"
#include "ddcs/io/fd.hpp"
#include "ddcs/net/stream_io.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace ddcs::ctrl::infra::prometheus {

class Server; // on_ready 위임 대상 (순환 의존 회피)

// 단일 스크레이프 연결. recv/send + 버퍼만 갖는 순수 메커니즘.
// 요청 완료 판정/응답 빌드/reap은 Server가 구동한다.
class Connection final : private io::ChannelHandler {
public:
    enum class State : std::uint8_t {
        idle,
        reading,
        writing,
    };

    Connection() = default;
    ~Connection() override = default;

    Connection(Connection const&) = delete;
    Connection& operator=(Connection const&) = delete;
    Connection(Connection&&) noexcept = delete;
    Connection& operator=(Connection&&) noexcept = delete;

    [[nodiscard]] io::Channel& channel() noexcept {
        return channel_;
    }

    [[nodiscard]] int fd() const noexcept {
        return channel_.fd();
    }

    [[nodiscard]] State state() const noexcept {
        return state_;
    }

    // 풀에서 꺼내 자원 배정(idle에서 reading으로)
    // 전제조건: idle(reset된) connection + 유효한 fd
    void init(Server& server, io::Fd fd, io::ChannelEvents interests) noexcept;

    // "\r\n\r\n" 또는 cap 도달
    [[nodiscard]] bool request_complete() const noexcept;

    // 완성된 HTTP 응답 설정 후 writing
    void begin_response(std::string http);

    // ET 드레인. rx에 누적(cap까지). 결과 어휘는 transport Connection과 공유한다.
    [[nodiscard]] net::ReceiveResult receive();
    // tx 송신. 다 보내면 drained 반환
    [[nodiscard]] net::TransmitResult transmit();

    // CAUTION: channel이 reactor에서 제거된 뒤에만 호출할 것
    void reset() noexcept;

private:
    // 정책 없음, Server로 위임
    void on_ready(io::Channel& channel, io::ChannelEvents events) override;

    Server* server_ = nullptr;
    io::Channel channel_;
    State state_ = State::idle;
    std::string rx_;         // 요청 누적(소량)
    std::string tx_;         // 응답(헤더 + 본문)
    std::size_t tx_pos_ = 0; // 송신 커서
};

} // namespace ddcs::ctrl::infra::prometheus
