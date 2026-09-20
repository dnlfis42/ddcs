#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace ddcs::profile {
class Recorder;
}

namespace ddcs::ctrl {

// Controller 구성과 실행을 담당한다. main과 통합 테스트에서 사용한다.
class Controller {
public:
    // 설정은 controller.json의 controller/prometheus/transport/session/command/
    // policy 순서로 선언한다. 로그 설정은 main에서 적용한다.
    struct Config {
        // 명령 재시도, 연결 상태 점검, 정책 평가 주기. 완료 시각까지 지난 예약은 건너뛴다.
        std::chrono::nanoseconds sweep_interval = std::chrono::seconds{1};

        // 값이 있으면 해당 포트에서 메트릭을 제공한다. 0이면 운영체제가 포트를 배정한다.
        std::optional<std::uint16_t> prometheus_port{};

        std::uint16_t listen_port = 0; // 0이면 운영체제가 포트를 배정한다.
        int accept_backlog = 128;
        // 연결별 수신 버퍼 크기(바이트). 최대 프레임 크기보다 작으면 생성 시 늘린다.
        std::size_t rx_buffer_size = 1 << 12;

        // 등록을 완료하기까지 허용하는 시간
        std::chrono::nanoseconds handshake_timeout = std::chrono::seconds{3};
        std::chrono::nanoseconds liveness_timeout = std::chrono::seconds{3};

        std::chrono::nanoseconds command_timeout = std::chrono::seconds{5};
        // 명령의 최대 전송 횟수. 1이면 재시도하지 않는다.
        int command_max_attempts = 3;
        // 재시도 대기 시간의 기준값
        std::chrono::nanoseconds command_backoff_base = std::chrono::milliseconds{500};

        // 값이 있으면 시작 시 설정 파일의 policy를 읽고 SIGHUP 수신 시 다시 읽는다.
        std::optional<std::filesystem::path> policy_path{};
    };

    // profile_recorder는 Controller보다 오래 유지되어야 하며, 기록은 한 스레드에서 수행한다.
    // nullptr이면 tick 프로파일을 기록하지 않는다.
    explicit Controller(Config cfg, ddcs::profile::Recorder* profile_recorder = nullptr);
    ~Controller();

    Controller(Controller const&) = delete;
    Controller& operator=(Controller const&) = delete;
    Controller(Controller&&) = delete;
    Controller& operator=(Controller&&) = delete;

    // 연결 수신을 시작하고 첫 tick을 예약한다.
    void start();
    // 이벤트 루프를 실행한다. 중지될 때까지 반환하지 않는다.
    void run();
    // 이벤트 루프를 한 번 실행한다.
    void run_once(std::chrono::milliseconds timeout);
    // 여러 번 호출해도 안전하다.
    void stop();

    std::uint16_t port() const;
    // 메트릭 서버의 포트. 비활성 상태이면 0을 반환한다.
    std::uint16_t prometheus_port() const;

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace ddcs::ctrl
