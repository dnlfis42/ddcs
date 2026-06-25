#include "ddcs/ctrl/app/device/registration_service.hpp"

#include <string>

namespace ddcs::ctrl::app::device {

domain::DeviceId RegistrationService::enroll(common::Uuid const& id, std::string_view group) {
    if (!id.valid()) {
        return {};
    }
    devices_.find_or_create(id);
    devices_.set_group(id, std::string{group});
    return id;
}

} // namespace ddcs::ctrl::app::device
