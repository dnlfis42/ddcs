#pragma once

#include <string>

namespace ddcs::ctrl::port::metrics {

// inbound (driving) port: infra(metrics 스크레이프 엔드포인트) -> app.
// app 이 구현, infra(Server)가 스크레이프 요청 시 호출 - 현재 메트릭을 Prometheus text 로 반환.
class Inbound {
public:
    virtual ~Inbound() = default;
    virtual std::string scrape() = 0;
};

} // namespace ddcs::ctrl::port::metrics
