#pragma once

#include <string>

namespace ddcs::ctrl::app::metrics::port {

// Prometheus text exposition 제공 포트
// - app이 구현하고 scrape 엔드포인트(infra)가 호출한다.
class PrometheusSource {
public:
    virtual ~PrometheusSource() = default;

    virtual std::string scrape() = 0;
};

} // namespace ddcs::ctrl::app::metrics::port
