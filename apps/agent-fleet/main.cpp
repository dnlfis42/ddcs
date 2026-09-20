#include "ddcs/agent/bootstrap.hpp"

#include "ddcs/agent/agent_fleet.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/logger/log.hpp"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/resource.h>

namespace {

std::size_t parse_count(std::string_view text) {
    std::size_t count = 0;

    auto const [end, error] = std::from_chars(text.data(), text.data() + text.size(), count);
    if (error != std::errc{} || end != text.data() + text.size() || count == 0) {
        throw std::invalid_argument{"agent-fleet: N must be a positive integer"};
    }

    return count;
}

// 공유 이벤트 루프에서 DNS 조회로 멈추지 않도록 기동 시 한 번만 해석한다.
std::string resolve_host(std::string const& host) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* raw = nullptr;
    int const result = ::getaddrinfo(host.c_str(), nullptr, &hints, &raw);
    if (result != 0) {
        throw std::runtime_error{
            std::string{"agent-fleet: resolve host: "} + ::gai_strerror(result)
        };
    }

    std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> addresses{raw, ::freeaddrinfo};
    if (!addresses) {
        throw std::runtime_error{"agent-fleet: host has no IPv4 address"};
    }

    char text[INET_ADDRSTRLEN]{};
    auto const* address = reinterpret_cast<sockaddr_in const*>(addresses->ai_addr);
    if (::inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text)) == nullptr) {
        throw std::runtime_error{"agent-fleet: cannot format controller address"};
    }

    return text;
}

void check_fd_limit(std::size_t count) {
    // Agent별 TCP 소켓 외에 표준 입출력과 이벤트 루프용 fd 여유를 확보한다.
    constexpr std::size_t reserve = 32;
    rlimit limit{};

    if (::getrlimit(RLIMIT_NOFILE, &limit) != 0) {
        throw std::runtime_error{"agent-fleet: cannot read nofile limit"};
    }
    if (count > std::numeric_limits<std::size_t>::max() - reserve ||
        (limit.rlim_cur != RLIM_INFINITY && count + reserve > limit.rlim_cur)) {
        throw std::runtime_error{
            "agent-fleet: nofile limit too low; raise ulimit -n to at least N + 32"
        };
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view{argv[1]} == "--help") {
        std::puts(
            "Usage: agent-fleet N group\nRuns N agents in one process, all in the specified group."
        );

        return EXIT_SUCCESS;
    }

    if (argc != 3) {
        std::fputs("Usage: agent-fleet N group\n", stderr);

        return EXIT_FAILURE;
    }

    ddcs::logger::StdoutSink sink;
    ddcs::logger::Logger::instance().set_sink(sink);

    try {
        auto const count = parse_count(argv[1]);

        std::string_view const group{argv[2]};
        if (group.empty()) {
            throw std::invalid_argument{"agent-fleet: group must not be empty"};
        }

        check_fd_limit(count);

        ddcs::agent::AgentFleet::Config cfg;
        cfg.agent = ddcs::agent::bootstrap::load_agent_config();
        cfg.agent.session.group = std::string{group};
        cfg.agent.controller_host = resolve_host(cfg.agent.controller_host);

        std::mt19937_64 rng{std::random_device{}()};
        std::vector<std::unique_ptr<ddcs::agent::domain::Device>> devices;
        devices.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            devices.push_back(
                ddcs::agent::bootstrap::make_simulated_device(
                    ddcs::common::Uuid::random(), cfg.agent, rng
                )
            );
        }

        ddcs::agent::AgentFleet fleet{std::move(cfg), std::move(devices)};
        fleet.start();
        fleet.run();
    } catch (std::exception const& e) {
        std::fprintf(stderr, "%s\n", e.what());

        return EXIT_FAILURE;
    } catch (...) {
        std::fputs("unknown exception\n", stderr);

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
