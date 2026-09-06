#include "ddcs/profile/analysis.hpp"
#include "ddcs/profile/tick_metrics.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

void print_usage(std::ostream& out) {
    out << "usage: profile-verify <tick-profile.json> <metrics-before-stop.prom>\n";
}

[[nodiscard]] bool read_file(std::filesystem::path const& path, std::string& text) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        std::cerr << "cannot open input: " << path << '\n';
        return false;
    }

    text.assign(std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{});
    if (input.bad()) {
        std::cerr << "cannot read input: " << path << '\n';
        return false;
    }
    return true;
}

void write_result(std::ostream& out, ddcs::profile::TickMetricsCrosscheckResult const& result) {
    out << "status=" << (result.succeeded() ? "pass" : "fail") << '\n';
    out << "metric_ticks_total=" << result.metrics.ticks_total << '\n';
    out << "raw_prefix_samples=" << result.raw_prefix_samples << '\n';
    out << "raw_completed_ticks=" << result.raw_completed_ticks << '\n';
    out << "metric_tick_duration_us_total=" << result.metrics.tick_duration_us_total << '\n';
    out << "raw_completed_tick_duration_us_total=" << result.raw_completed_tick_duration_us_total
        << '\n';
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        print_usage(std::cerr);
        return 2;
    }

    std::filesystem::path const profile_path{argv[1]};
    std::filesystem::path const metrics_path{argv[2]};
    std::string profile_text;
    std::string metrics_text;
    if (!read_file(profile_path, profile_text) || !read_file(metrics_path, metrics_text)) {
        return 1;
    }

    auto const metrics = ddcs::profile::parse_tick_metrics_snapshot(metrics_text);
    if (!metrics.succeeded()) {
        std::cerr << "cannot parse tick metrics snapshot: "
                  << ddcs::profile::to_string(metrics.error) << '\n';
        return 1;
    }

    auto const raw_prefix =
        ddcs::profile::analyze_recording_through_tick(profile_text, metrics.snapshot.ticks_total);
    if (!raw_prefix.succeeded()) {
        std::cerr << "cannot analyze profile input: " << ddcs::profile::to_string(raw_prefix.error)
                  << '\n';
        return 1;
    }

    auto const crosscheck =
        ddcs::profile::verify_tick_metrics_snapshot(raw_prefix.summary, metrics.snapshot);
    if (!crosscheck.succeeded()) {
        std::cerr << "tick metrics crosscheck failed: "
                  << ddcs::profile::to_string(crosscheck.error) << '\n';
        write_result(std::cerr, crosscheck);
        return 1;
    }

    write_result(std::cout, crosscheck);
    if (!std::cout) {
        std::cerr << "cannot write crosscheck output\n";
        return 1;
    }
    return 0;
}
