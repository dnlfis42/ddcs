#include "ddcs/ctrl/app/agent/device_roster.hpp"

#include "ddcs/ctrl/app/agent/agent.hpp"

namespace ddcs::ctrl::app::agent {

void DeviceRoster::for_each_active(std::function<void(domain::DeviceId)> const& fn) {
    agents_.for_each([&](Agent const& agent) {
        if (agent.state() == Agent::State::active) {
            fn(agent.device());
        }
    });
}

} // namespace ddcs::ctrl::app::agent
