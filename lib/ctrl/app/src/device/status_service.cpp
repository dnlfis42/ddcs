#include "ddcs/ctrl/app/device/status_service.hpp"

#include "ddcs/device/mode.hpp"
#include "ddcs/device/status.hpp"
#include "ddcs/logger/log.hpp"

#include <cmath>

namespace ddcs::ctrl::app::device {

using ddcs::device::Mode;

namespace {

Mode mode_of(std::uint8_t mode) noexcept {
    switch (static_cast<Mode>(mode)) {
    case Mode::safe:
        return Mode::safe;
    case Mode::normal:
        return Mode::normal;
    case Mode::performance:
        return Mode::performance;
    }
    return Mode::safe;
}

} // namespace

void StatusService::update_status(
    domain::DeviceId id, std::uint8_t mode, double load, double temp
) {
    // 수신 텔레메트리 수치 경계 검증
    //  NaN 비교는 항상 false라,
    // 비유한 load 한 건이 그 group의 load 평균을 오염시켜 정책 전이를 조용히 마비시킨다.
    // 비유한 load/temp는 버리고 직전 트윈을 보존한다.
    if (!std::isfinite(load) || !std::isfinite(temp)) {
        LOG_WARN(
            "device.status.non_finite", logger::kv("device", id.to_string()),
            logger::kv("load", load), logger::kv("temp", temp)
        );
        return;
    }

    devices_.update_status(
        id, ddcs::device::Status{.mode = mode_of(mode), .load = load, .temp = temp}
    );

    LOG_DEBUG(
        "device.status", logger::kv("device", id.to_string()),
        logger::kv("mode", static_cast<std::uint64_t>(mode)), logger::kv("load", load),
        logger::kv("temp", temp)
    );
}

} // namespace ddcs::ctrl::app::device
