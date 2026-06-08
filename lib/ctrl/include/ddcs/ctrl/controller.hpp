#pragma once

#include "ddcs/common/uuid.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/logger/log.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>

#include <cstdint>

namespace ddcs::ctrl {

// Controller 조립 루트 facade. 외부(main, 통합 테스트)는 이 클래스만 다룬다.
class Controller {
public:
    struct Config {
        std::uint16_t listen_port{0}; // 0 = ephemeral
        int accept_backlog{128};
        // nullopt = metrics 엔드포인트 비활성. 값이 있으면 그 포트로 바인드(0 = ephemeral).
        std::optional<std::uint16_t> metrics_port{};
        std::chrono::nanoseconds liveness_timeout{std::chrono::seconds{3}};
        std::chrono::nanoseconds command_timeout{std::chrono::seconds{5}};
        int command_max_attempts{3}; // 부분실패 재시도(1 = 재시도 없음)
        std::chrono::nanoseconds command_backoff_base{std::chrono::milliseconds{500}}; // 지수 backoff 기준
        std::chrono::nanoseconds sweep_interval{std::chrono::seconds{1}}; // command/liveness/policy sweep 주기
        // nullopt = 정책 비활성. 값이 있으면 부팅 시 그 policy.json을 load-once.
        std::optional<std::filesystem::path> policy_path{};

        logger::Level log_level{logger::Level::Info};
        // nullptr 이면 내부 기본 StdoutSink 설치. 직접 주입 시 그 sink 우선.
        logger::Sink* log_sink{nullptr};
    };

    explicit Controller(Config cfg);
    ~Controller();

    Controller(Controller const&) = delete;
    Controller& operator=(Controller const&) = delete;
    Controller(Controller&&) = delete;
    Controller& operator=(Controller&&) = delete;

    void start();                                     // listen 개시 + sweep 타이머 예약
    void run();                                       // 이벤트 루프 (블로킹)
    void run_once(std::chrono::milliseconds timeout); // 1회 디스패치
    void stop();                                      // 멱등

    std::uint16_t port() const;
    // metrics 엔드포인트 바인드 포트. 비활성이면 0.
    std::uint16_t metrics_port() const;

    // operator API (driving): agent에게 SetMode 명령 발신. 반환 command_id(미지/미연결 0).
    std::uint64_t set_mode(common::Uuid const& agent_uuid, device::Mode mode);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ddcs::ctrl
