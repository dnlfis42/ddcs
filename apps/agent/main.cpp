#include "ddcs/agent/agent.hpp"
#include "ddcs/agent/bootstrap.hpp"
#include "ddcs/common/parse.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/config/env.hpp"
#include "ddcs/logger/event.hpp"
#include "ddcs/logger/log.hpp"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

std::string_view trim(std::string_view s) noexcept {
    auto const first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

// DeviceId는 유효한 환경변수 값 > 파일 값 > 새 UUID 순으로 선택한다.
// DDCS_DEVICE_ID_FILE을 지정한 경우에만 새 UUID를 저장한다.
ddcs::common::Uuid load_device_uuid() {
    if (auto const value = ddcs::config::env::get("DDCS_DEVICE_ID")) {
        if (auto u = ddcs::common::parse_uuid(*value)) {
            LOG_DEVICE_ID(u->to_string(), "env");
            return *u;
        }

        LOG_CONFIG_VALUE_INVALID("env", "DDCS_DEVICE_ID", "uuid", *value);
    }

    auto const configured = ddcs::config::env::get("DDCS_DEVICE_ID_FILE");
    if (!configured) {
        // 기본 파일을 공유해 여러 Agent가 같은 DeviceId를 사용하는 상황을 피한다.
        ddcs::common::Uuid const fresh = ddcs::common::Uuid::random();
        LOG_DEVICE_ID(fresh.to_string(), "ephemeral");

        return fresh;
    }

    std::filesystem::path const path{*configured};
    LOG_CONFIG_PATH("DDCS_DEVICE_ID_FILE", path.string());

    if (std::ifstream in{path}) {
        std::string const text{
            std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}
        };

        auto const trimmed = trim(text);
        if (auto u = ddcs::common::parse_uuid(trimmed)) {
            LOG_DEVICE_ID(u->to_string(), "file");

            return *u;
        }

        // 파일 전체가 UUID 값이므로 오류 위치는 설정 키 대신 파일 경로로 기록한다.
        LOG_CONFIG_VALUE_INVALID("file", path.string(), "uuid", trimmed);
    }

    ddcs::common::Uuid const fresh = ddcs::common::Uuid::random();
    LOG_DEVICE_ID(fresh.to_string(), "generated");
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    if (std::ofstream out{path}) {
        out << fresh.to_string() << '\n';
        if (out) {
            return fresh;
        }
    }

    // 저장에 실패해도 기동하되, 다음 기동에서 같은 신원을 복원할 수 없음을 알린다.
    LOG_DEVICE_ID_NOT_PERSISTED(fresh.to_string());

    return fresh;
}

} // namespace

int main() {
    ddcs::logger::StdoutSink sink;
    auto& lg = ddcs::logger::Logger::instance();
    lg.set_sink(sink);

    try {
        auto const uuid = load_device_uuid();

        auto cfg = ddcs::agent::bootstrap::load_agent_config();
        std::mt19937_64 rng{std::random_device{}()};
        auto device = ddcs::agent::bootstrap::make_simulated_device(uuid, cfg, rng);

        ddcs::agent::Agent agent{std::move(cfg), std::move(device)};
        agent.start();
        agent.run();
    } catch (std::exception const& e) {
        std::fprintf(stderr, "%s\n", e.what());

        return EXIT_FAILURE;
    } catch (...) {
        std::fprintf(stderr, "unknown exception\n");

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
