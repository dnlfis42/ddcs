#include "ddcs/ctrl/app/session/device_roster.hpp"

#include "ddcs/ctrl/app/session/session.hpp"

namespace ddcs::ctrl::app::session {

void DeviceRoster::for_each_active(std::function<void(domain::DeviceId)> const& fn) {
    sessions_.for_each([&](Session const& session) {
        if (session.state() == Session::State::active) {
            fn(session.device());
        }
    });
}

} // namespace ddcs::ctrl::app::session
