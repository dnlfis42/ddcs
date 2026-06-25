#include "ddcs/ctrl/app/device/status_service.hpp"

#include "ddcs/ctrl/domain/status.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/logger/log.hpp"

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
    // 수신 텔레메트리 수치 경계 검증
    // - NaN 비교는 항상 false:
    //   비유한 load 한 건이 그 group의 load 평균을 오염시켜 정책 전이를 조용히 마비시킨다.
    //   비유한 load/temp는 버리고 직전 Shadow를 보존한다.
    if (!std::isfinite(load) || !std::isfinite(temp)) {
        LOG_WARN(
            "device.status.non_finite", logger::kv("device", id.to_string()),
            logger::kv("load", load), logger::kv("temp", temp)
        );
        return;
    }

    devices_.update_status(
        id, ddcs::ctrl::domain::Status{.mode = mode_of(mode), .load = load, .temp = temp}
    );

    LOG_DEBUG(
        "device.status", logger::kv("device", id.to_string()),
        logger::kv("mode", static_cast<std::uint64_t>(mode)), logger::kv("load", load),
        logger::kv("temp", temp)
    );
}

} // namespace ddcs::ctrl::app::device
