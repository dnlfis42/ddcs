#include "ddcs/config/env.hpp"
#include "ddcs/config/file.hpp"
#include "ddcs/ctrl/controller.hpp"
#include "ddcs/json/value.hpp"
#include "ddcs/logger/event.hpp"
#include "ddcs/logger/log.hpp"
#include "ddcs/profile/dump.hpp"
#include "ddcs/profile/recorder.hpp"
#include "ddcs/wire/frame/frame.hpp"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

struct ProfileRunOptions {
    std::size_t capacity;
    std::filesystem::path output_path;
    ddcs::profile::RunMetadata metadata;
};

[[nodiscard]] std::optional<std::uint64_t> utc_now_ns() noexcept {
    auto const elapsed = std::chrono::system_clock::now().time_since_epoch();
    auto const count = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    if (count < 0) {
        return std::nullopt;
    }

    return static_cast<std::uint64_t>(count);
}

[[nodiscard]] std::optional<bool> parse_bool(std::string_view text) noexcept {
    if (text == "true" || text == "1") {
        return true;
    }
    if (text == "false" || text == "0") {
        return false;
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::uint64_t> parse_uint64(std::string_view text) noexcept {
    if (text.empty()) {
        return std::nullopt;
    }

    std::uint64_t value = 0;
    auto const result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        return std::nullopt;
    }

    return value;
}

[[nodiscard]] std::optional<ProfileRunOptions> load_profile_options(ddcs::json::Value const& root) {
    auto const* profile = root.find("profile");
    if (profile != nullptr && !profile->is_object()) {
        throw std::runtime_error{"profile config must be an object"};
    }

    bool enabled = false;
    if (auto const env_enabled = ddcs::config::env::get("DDCS_PROFILE_ENABLED")) {
        auto const parsed = parse_bool(*env_enabled);
        if (!parsed) {
            throw std::runtime_error{"DDCS_PROFILE_ENABLED must be true, false, 1, or 0"};
        }
        enabled = *parsed;
    } else if (profile != nullptr) {
        auto const* enabled_value = profile->find("enabled");
        auto const file_enabled =
            enabled_value == nullptr ? std::optional<bool>{} : enabled_value->as_bool();
        if (!file_enabled) {
            throw std::runtime_error{"profile.enabled must be a boolean"};
        }
        enabled = *file_enabled;
    }
    if (!enabled) {
        return std::nullopt;
    }

    constexpr std::size_t max_capacity =
        std::numeric_limits<std::size_t>::max() / sizeof(ddcs::profile::TickSample);
    std::optional<std::uint64_t> capacity;
    if (auto const env_capacity = ddcs::config::env::get("DDCS_PROFILE_CAPACITY")) {
        capacity = parse_uint64(*env_capacity);
        if (!capacity) {
            throw std::runtime_error{"DDCS_PROFILE_CAPACITY must be an unsigned integer"};
        }
    } else if (profile != nullptr) {
        auto const* capacity_value = profile->find("capacity");
        auto const file_capacity =
            capacity_value == nullptr ? std::optional<std::int64_t>{} : capacity_value->as_int64();
        if (file_capacity && *file_capacity > 0) {
            capacity = static_cast<std::uint64_t>(*file_capacity);
        }
    }
    if (!capacity || *capacity == 0 || *capacity > static_cast<std::uint64_t>(max_capacity)) {
        throw std::runtime_error{"profile.capacity must fit a positive Recorder capacity"};
    }

    std::optional<std::string_view> output_text =
        ddcs::config::env::get("DDCS_PROFILE_OUTPUT_PATH");
    if (!output_text && profile != nullptr) {
        auto const* output_value = profile->find("output_path");
        output_text =
            output_value == nullptr ? std::optional<std::string_view>{} : output_value->as_string();
    }
    if (!output_text || output_text->empty() || output_text->find('\0') != std::string_view::npos) {
        throw std::runtime_error{"profile.output_path must be a non-empty path"};
    }
    std::filesystem::path const output_path{*output_text};

    std::optional<std::string_view> run_id = ddcs::config::env::get("DDCS_PROFILE_RUN_ID");
    if (!run_id && profile != nullptr) {
        auto const* run_id_value = profile->find("run_id");
        run_id =
            run_id_value == nullptr ? std::optional<std::string_view>{} : run_id_value->as_string();
    }
    if (!run_id || run_id->empty()) {
        throw std::runtime_error{"profile.run_id must be a non-empty string"};
    }

    auto parent = output_path.parent_path();
    if (parent.empty()) {
        parent = ".";
    }
    std::error_code error;
    auto const status = std::filesystem::status(parent, error);
    if (error) {
        throw std::system_error{
            error, "profile: cannot inspect output directory " + parent.string()
        };
    }
    if (!std::filesystem::is_directory(status)) {
        throw std::runtime_error{
            "profile.output_path parent is not a directory: " + parent.string()
        };
    }
    if (std::filesystem::exists(output_path, error)) {
        throw std::runtime_error{"profile.output_path already exists: " + output_path.string()};
    }
    if (error) {
        throw std::system_error{
            error, "profile: cannot inspect output path " + output_path.string()
        };
    }

    return ProfileRunOptions{
        .capacity = static_cast<std::size_t>(*capacity),
        .output_path = output_path,
        .metadata = {.run_id = std::string{*run_id}},
    };
}

[[nodiscard]] std::runtime_error dump_error(ddcs::profile::DumpResult const& result) {
    std::string message{"profile dump failed: "};
    message += ddcs::profile::to_string(result.error);
    if (result.system_error) {
        message += ": ";
        message += result.system_error.message();
    }
    if (result.published) {
        message += " (completed output was published, but temporary cleanup failed)";
    }
    return std::runtime_error{std::move(message)};
}

void report_secondary_error(std::exception_ptr error) noexcept {
    try {
        std::rethrow_exception(error);
    } catch (std::exception const& e) {
        std::fprintf(stderr, "profile dump error: %s\n", e.what());
    } catch (...) {
        std::fprintf(stderr, "profile dump error: unknown exception\n");
    }
}

} // namespace

int main() {
    ddcs::logger::StdoutSink sink;
    auto& lg = ddcs::logger::Logger::instance();
    lg.set_sink(sink);

    // 부팅 실패(config malformed, epoll/signalfd/timerfd 생성 실패 등)는 여기서 잡아 운영자용
    // 1줄 진단 + EXIT_FAILURE로 끝낸다.
    try {
        // 설정 파일
        std::filesystem::path const config_path{
            ddcs::config::env::get("DDCS_CONFIG_PATH").value_or("config/controller.json")
        };
        LOG_CONFIG_PATH("DDCS_CONFIG_PATH", config_path.string());

        ddcs::json::Value root; // 파일 없으면 null -> 전부 기본값
        if (auto loaded = ddcs::config::file::load(config_path)) {
            root = std::move(*loaded);
        } else {
            LOG_CONFIG_PATH_ABSENT(config_path.string());
        }

        // 로거 설정
        auto log_level = lg.level();
        auto const level_text = ddcs::config::file::get_string(root, "log.level", "info");
        if (auto const parsed = ddcs::logger::parse_level(level_text)) {
            log_level = *parsed;
        } else {
            LOG_CONFIG_VALUE_INVALID("file", "log.level", "log level", level_text);
        }
        if (auto const level = ddcs::config::env::get("DDCS_LOG_LEVEL")) {
            if (auto const parsed = ddcs::logger::parse_level(*level)) {
                log_level = *parsed;
            } else {
                LOG_CONFIG_VALUE_INVALID("env", "DDCS_LOG_LEVEL", "log level", *level);
            }
        }
        lg.set_level(log_level);

        // Controller 설정. 절 순서는 controller.json을 따른다.
        ddcs::ctrl::Controller::Config cfg{};

        // controller
        cfg.sweep_interval = ddcs::config::file::get_duration_ms(
            root, "controller.sweep_interval_ms", cfg.sweep_interval
        );

        // prometheus
        cfg.prometheus_port = ddcs::config::file::get_port(root, "prometheus.port", 9000);
        if (auto const port = ddcs::config::env::get_port("DDCS_PROMETHEUS_PORT")) {
            cfg.prometheus_port = *port;
        }

        // transport
        cfg.listen_port = ddcs::config::file::get_port(root, "transport.port", 8080);
        cfg.listen_port = ddcs::config::env::get_port("DDCS_TRANSPORT_PORT", cfg.listen_port);

        cfg.accept_backlog =
            ddcs::config::file::get_int(root, "transport.accept_backlog", cfg.accept_backlog);

        cfg.rx_buffer_size = ddcs::config::file::get_size(
            root, "transport.rx_buffer_size", cfg.rx_buffer_size, ddcs::wire::frame::max_rx_capacity
        );

        // session
        cfg.handshake_timeout = ddcs::config::file::get_duration_ms(
            root, "session.handshake_timeout_ms", cfg.handshake_timeout
        );
        cfg.liveness_timeout = ddcs::config::file::get_duration_ms(
            root, "session.liveness_timeout_ms", cfg.liveness_timeout
        );

        // command
        cfg.command_timeout =
            ddcs::config::file::get_duration_ms(root, "command.timeout_ms", cfg.command_timeout);
        cfg.command_max_attempts =
            ddcs::config::file::get_int(root, "command.max_attempts", cfg.command_max_attempts);
        cfg.command_backoff_base = ddcs::config::file::get_duration_ms(
            root, "command.backoff_base_ms", cfg.command_backoff_base
        );

        // policy
        cfg.policy_path = config_path; // 정책은 같은 파일의 "policy" 객체에 인라인

        // profile은 명시적으로 enabled일 때만 버퍼를 만들며, main이 Controller보다 오래 소유한다.
        auto const profile_options = load_profile_options(root);
        std::optional<ddcs::profile::Recorder> profile_recorder;
        std::optional<ddcs::profile::RunMetadata> profile_metadata;
        if (profile_options) {
            profile_recorder.emplace(profile_options->capacity);
            profile_metadata = profile_options->metadata;
        }

        std::exception_ptr controller_error;
        try {
            // run()이 반환하면 event loop가 멈췄으므로 이후에는 tick callback이 record하지 않는다.
            ddcs::ctrl::Controller controller{
                std::move(cfg), profile_recorder ? &*profile_recorder : nullptr
            };
            // Controller가 start() 안에서 monotonic origin을 만든다. 이 UTC bracket의 폭은
            // relative raw timestamp를 외부 측정 창에 맞출 때 초기 정렬 오차 상한이다.
            auto const origin_before = profile_recorder ? utc_now_ns() : std::nullopt;
            controller.start();
            if (origin_before) {
                if (auto const origin_after = utc_now_ns();
                    origin_after && *origin_before <= *origin_after) {
                    profile_metadata->recording_origin_utc = {
                        .before_unix_ns = *origin_before,
                        .after_unix_ns = *origin_after,
                    };
                }
            }
            controller.run();
        } catch (...) {
            controller_error = std::current_exception();
        }

        std::exception_ptr profile_dump_error;
        if (profile_recorder) {
            try {
                auto const result = ddcs::profile::dump_recording(
                    profile_recorder->finish(), *profile_metadata, profile_options->output_path
                );
                if (!result.succeeded()) {
                    throw dump_error(result);
                }
            } catch (...) {
                profile_dump_error = std::current_exception();
            }
        }

        // Controller의 원래 오류를 dump 오류가 가리지 않게 한다. 정상 실행에서만 dump 오류를
        // 프로세스 종료 오류로 승격한다.
        if (controller_error) {
            if (profile_dump_error) {
                report_secondary_error(profile_dump_error);
            }
            std::rethrow_exception(controller_error);
        }
        if (profile_dump_error) {
            std::rethrow_exception(profile_dump_error);
        }
    } catch (std::exception const& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return EXIT_FAILURE;
    } catch (...) {
        std::fprintf(stderr, "unknown exception\n");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
