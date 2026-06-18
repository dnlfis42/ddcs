#include "ddcs/ctrl/controller.hpp"
#include "ddcs/logger/log.hpp"

#include <cstdlib>

int main() {
    ddcs::ctrl::Controller::Config cfg{};
    cfg.listen_port = 8080;
    cfg.metrics_port = 9090;
    char const* policy_path = std::getenv("DDCS_POLICY_PATH");
    cfg.policy_path = policy_path != nullptr ? policy_path : "config/policy.json";
    if (char const* level_text = std::getenv("DDCS_LOG_LEVEL")) {
        if (auto const level = ddcs::logger::parse_level(level_text)) {
            cfg.log_level = *level;
        }
    }

    ddcs::ctrl::Controller controller{std::move(cfg)};
    controller.start();
    controller.run();
    return EXIT_SUCCESS;
}
