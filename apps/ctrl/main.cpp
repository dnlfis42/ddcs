#include "ddcs/ctrl/controller.hpp"
#include "ddcs/logger/log.hpp"

#include <cstdlib>

int main() {
    ddcs::ctrl::Controller::Config cfg{};
    cfg.listen_port = 8080;
    cfg.metrics_port = 9090;
    char const* policy_path = std::getenv("DDCS_POLICY_PATH");
    cfg.policy_path = policy_path != nullptr ? policy_path : "config/policy.json";
    if (char const* lvl = std::getenv("DDCS_LOG_LEVEL")) {
        cfg.log_level = ddcs::logger::level_from_string(lvl, cfg.log_level);
    }

    ddcs::ctrl::Controller controller{std::move(cfg)};
    controller.start();
    controller.run();
    return EXIT_SUCCESS;
}
