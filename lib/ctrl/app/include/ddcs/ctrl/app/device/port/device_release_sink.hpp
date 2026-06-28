#pragma once

#include "ddcs/ctrl/domain/device_id.hpp"

namespace ddcs::ctrl::app::device::port {

// device 세션 종료를 device-control(정책)에 통지하는 포트
// - 연결 수명(session 계층)이 per-device 제어 상태(policy)의 폐기 시점을 알린다.
// - 구현(PolicyService)은 그 device의 명령/thermal belief를 폐기한다: 같은 id로 재접속(리부트)한
//   device가 stale한 명령 belief에 갇히지 않게 하고(재명령 보장), per-device 맵 증식도 막는다.
class DeviceReleaseSink {
public:
    virtual ~DeviceReleaseSink() = default;

    // device가 roster에서 빠질 때(정상 종료 / kick-old / liveness evict) 호출.
    virtual void on_device_left(domain::DeviceId device) = 0;
};

} // namespace ddcs::ctrl::app::device::port
