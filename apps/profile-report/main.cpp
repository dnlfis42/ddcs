#include "ddcs/profile/analysis.hpp"

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace {

void print_usage(std::ostream& out) {
    out << "usage: profile-report <input.json> [--from-ns <ns>] [--to-ns <ns>]\n"
           "       profile-report <input.json> [--from-unix-ns <ns>] [--to-unix-ns <ns>]\n";
}

[[nodiscard]] std::optional<std::uint64_t> parse_uint64(std::string_view text) {
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

void write_csv_string(std::ostream& out, std::string_view value) {
    out << '"';
    for (char ch : value) {
        if (ch == '"') {
            out << '"';
        }
        out << ch;
    }
    out << '"';
}

void write_optional_uint(std::ostream& out, std::optional<std::uint64_t> value) {
    if (value) {
        out << *value;
    }
}

void write_distribution(std::ostream& out, ddcs::profile::Distribution const& distribution) {
    out << ',' << distribution.count << ',';
    write_optional_uint(out, distribution.mean_ns);
    out << ',';
    write_optional_uint(out, distribution.p95_ns);
    out << ',';
    write_optional_uint(out, distribution.max_ns);
}

void write_csv(
    std::ostream& out, ddcs::profile::ProfileSummary const& summary,
    ddcs::profile::AnalysisWindow const& window
) {
    out << "run_id,window_from_ns,window_to_ns,capacity,captured,dropped,recording_complete,"
           "selected_ticks,completed_ticks,command_sweep_failures,session_sweep_failures,"
           "policy_evaluate_failures,tick_count,tick_mean_ns,tick_p95_ns,tick_max_ns,"
           "command_sweep_count,command_sweep_mean_ns,command_sweep_p95_ns,"
           "command_sweep_max_ns,session_sweep_count,session_sweep_mean_ns,"
           "session_sweep_p95_ns,session_sweep_max_ns,policy_evaluate_count,"
           "policy_evaluate_mean_ns,policy_evaluate_p95_ns,policy_evaluate_max_ns,"
           "start_interval_count,start_interval_mean_ns,start_interval_p95_ns,"
           "start_interval_max_ns,start_interval_gaps\n";

    write_csv_string(out, summary.run_id);
    out << ',';
    write_optional_uint(out, window.from_ns);
    out << ',';
    write_optional_uint(out, window.to_ns);
    out << ',' << summary.capacity << ',' << summary.captured << ',' << summary.dropped << ','
        << (summary.recording_complete ? "true" : "false") << ',' << summary.selected_ticks << ','
        << summary.completed_ticks << ',' << summary.command_sweep_failures << ','
        << summary.session_sweep_failures << ',' << summary.policy_evaluate_failures;
    write_distribution(out, summary.tick_duration);
    write_distribution(out, summary.command_sweep_duration);
    write_distribution(out, summary.session_sweep_duration);
    write_distribution(out, summary.policy_evaluate_duration);
    write_distribution(out, summary.start_interval);
    out << ',' << summary.start_interval_gaps << '\n';
}

struct UnixWindow {
    std::optional<std::uint64_t> from_ns;
    std::optional<std::uint64_t> to_ns;
};

[[nodiscard]] std::uint64_t subtract_or_zero(std::uint64_t value, std::uint64_t subtrahend) {
    return value > subtrahend ? value - subtrahend : 0;
}

[[nodiscard]] std::optional<ddcs::profile::AnalysisWindow>
map_unix_window(UnixWindow const& window, ddcs::profile::UtcClockBracket const& origin) {
    ddcs::profile::AnalysisWindow raw;
    // origin은 [before, after] 안에 있다. 하한에는 before, 상한에는 after를 써야
    // 선택된 raw tick이 외부 UTC 창 안에 확실히 포함된다.
    if (window.from_ns) {
        raw.from_ns = subtract_or_zero(*window.from_ns, origin.before_unix_ns);
    }
    if (window.to_ns) {
        raw.to_ns = subtract_or_zero(*window.to_ns, origin.after_unix_ns);
    }
    if (raw.from_ns && raw.to_ns && *raw.from_ns > *raw.to_ns) {
        return std::nullopt;
    }

    return raw;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(std::cerr);
        return 2;
    }

    std::filesystem::path const input_path{argv[1]};
    ddcs::profile::AnalysisWindow window;
    UnixWindow unix_window;
    bool has_raw_window_option = false;
    bool has_unix_window_option = false;
    for (int index = 2; index < argc; ++index) {
        std::string_view const option{argv[index]};
        if ((option != "--from-ns" && option != "--to-ns" && option != "--from-unix-ns" &&
             option != "--to-unix-ns") ||
            ++index >= argc) {
            print_usage(std::cerr);
            return 2;
        }

        auto const value = parse_uint64(argv[index]);
        if (!value || (option == "--from-ns" && window.from_ns) ||
            (option == "--to-ns" && window.to_ns) ||
            (option == "--from-unix-ns" && unix_window.from_ns) ||
            (option == "--to-unix-ns" && unix_window.to_ns)) {
            print_usage(std::cerr);
            return 2;
        }

        if (option == "--from-ns") {
            window.from_ns = *value;
            has_raw_window_option = true;
        } else if (option == "--to-ns") {
            window.to_ns = *value;
            has_raw_window_option = true;
        } else if (option == "--from-unix-ns") {
            unix_window.from_ns = *value;
            has_unix_window_option = true;
        } else {
            unix_window.to_ns = *value;
            has_unix_window_option = true;
        }
    }
    if (has_raw_window_option && has_unix_window_option) {
        print_usage(std::cerr);
        return 2;
    }

    std::ifstream input{input_path, std::ios::binary};
    if (!input) {
        std::cerr << "cannot open profile input: " << input_path << '\n';
        return 1;
    }
    std::string const text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (input.bad()) {
        std::cerr << "cannot read profile input: " << input_path << '\n';
        return 1;
    }

    if (has_unix_window_option) {
        auto const metadata = ddcs::profile::analyze_recording(text);
        if (!metadata.succeeded()) {
            std::cerr << "cannot analyze profile input: "
                      << ddcs::profile::to_string(metadata.error) << '\n';
            return 1;
        }
        if (!metadata.summary.recording_origin_utc) {
            std::cerr << "cannot map UTC window: profile has no recording_origin_utc\n";
            return 1;
        }
        auto const mapped = map_unix_window(unix_window, *metadata.summary.recording_origin_utc);
        if (!mapped) {
            std::cerr << "cannot map UTC window: no raw interval is definitely inside it\n";
            return 1;
        }
        window = *mapped;
    }

    auto const result = ddcs::profile::analyze_recording(text, window);
    if (!result.succeeded()) {
        std::cerr << "cannot analyze profile input: " << ddcs::profile::to_string(result.error)
                  << '\n';
        return 1;
    }

    write_csv(std::cout, result.summary, window);
    if (!std::cout) {
        std::cerr << "cannot write CSV output\n";
        return 1;
    }
    return 0;
}
