#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace ddcs::ctrl {

// Controller 조립 루트 facade. 외부(main, 통합 테스트)는 이 클래스만 다룬다.
class Controller {
public:
    // 멤버 순서는 controller.json 절 순서(controller/prometheus/transport/session/command/
    // policy)를 따른다. log 절은 프로세스 로깅 부트스트랩이라 main 몫이다.
    struct Config {
        // command/liveness/policy sweep 주기
        std::chrono::nanoseconds sweep_interval = std::chrono::seconds{1};

        // nullopt = metrics 엔드포인트 비활성. 값이 있으면 그 포트로 바인드 (0 = ephemeral)
        std::optional<std::uint16_t> prometheus_port{};

        std::uint16_t listen_port = 0; // 0 = ephemeral
        int accept_backlog = 128;
        // per-connection rx ring 용량(byte). frame 최대 크기 미만이면 조립 시 하한으로 보정
        std::size_t rx_buffer_size = 1 << 12;

        // 등록 미완 (handshaking/confirming) 시한
        std::chrono::nanoseconds handshake_timeout = std::chrono::seconds{3};
        std::chrono::nanoseconds liveness_timeout = std::chrono::seconds{3};

        std::chrono::nanoseconds command_timeout = std::chrono::seconds{5};
        // 부분실패 재시도 (1 = 재시도 없음)
        int command_max_attempts = 3;
        // 지수 backoff 기준
        std::chrono::nanoseconds command_backoff_base = std::chrono::milliseconds{500};

        // nullopt = 정책 비활성. 값이 있으면 부팅 시 그 policy.json을 load-once
        std::optional<std::filesystem::path> policy_path{};
    };

    explicit Controller(Config cfg);
    ~Controller();

    Controller(Controller const&) = delete;
    Controller& operator=(Controller const&) = delete;
    Controller(Controller&&) = delete;
    Controller& operator=(Controller&&) = delete;

    // listen 개시 + sweep 타이머 예약
    void start();
    // 이벤트 루프 (블로킹)
    void run();
    // 1회 디스패치
    void run_once(std::chrono::milliseconds timeout);
    // 멱등
    void stop();

    std::uint16_t port() const;
    // metrics 엔드포인트 바인드 포트. 비활성이면 0
    std::uint16_t prometheus_port() const;

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace ddcs::ctrl
