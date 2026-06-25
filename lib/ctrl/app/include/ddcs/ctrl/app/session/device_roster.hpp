#pragma once

#include "ddcs/ctrl/app/device/port/device_roster.hpp"
#include "ddcs/ctrl/app/session/session_registry.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"

#include <functional>

namespace ddcs::ctrl::app::session {

// device::port::DeviceRoster 어댑터
// - SessionRegistry에서 active 바인딩만 추려 device를 내준다.
class DeviceRoster final : public device::port::DeviceRoster {
public:
    explicit DeviceRoster(SessionRegistry& sessions) noexcept
        : sessions_(sessions) {}

    void for_each_active(std::function<void(domain::DeviceId)> const& fn) override;

private:
    SessionRegistry& sessions_;
};

} // namespace ddcs::ctrl::app::session
