#include "ddcs/ctrl/app/device/status_service.hpp"

#include "ddcs/device/mode.hpp"
#include "ddcs/device/status.hpp"
#include "ddcs/logger/event.hpp"

#include <cmath>

namespace ddcs::ctrl::app::device {

using ddcs::device::Mode;

namespace {

Mode mode_of(std::uint8_t mode) noexcept {
    // 미지의 wire 값은 safe로 해석한다(status_service 계약).
    return ddcs::device::decode_mode(mode).value_or(Mode::safe);
}

} // namespace

void StatusService::update_status(
    domain::DeviceId id, std::uint8_t mode, double load, double temp
) {
    // NaN 비교는 항상 false라, 비유한 load 한 건이 group 평균을 조용히 오염시킨다. 여기서 버리고
    // 직전 Shadow를 보존한다.
    if (!std::isfinite(load) || !std::isfinite(temp)) {
        LOG_DEVICE_STATUS_NON_FINITE(id.to_string(), load, temp);
        return;
    }

    if (!devices_.update_status(
            id, ddcs::device::Status{.mode = mode_of(mode), .load = load, .temp = temp}
        )) {
        // 등록된 세션의 device만 여기로 오므로 정상 배선에서는 닿지 않는 경로다.
        LOG_DEVICE_ID_UNKNOWN(id.to_string());
        return;
    }

    LOG_DEVICE_STATUS_UPDATE(id.to_string(), static_cast<std::uint64_t>(mode), load, temp);
}

} // namespace ddcs::ctrl::app::device
