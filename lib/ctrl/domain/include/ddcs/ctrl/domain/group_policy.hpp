#pragma once

#include "ddcs/device/mode.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ddcs::ctrl::domain {

// Group 집계 load의 부하 상태. 밴드 위치이지 Mode가 아니며, 데드밴드에서는 직전 판정을 유지한다.
enum class Regime : std::uint8_t { unknown, busy, idle };

// 로그/진단용 이름. 어휘 밖 값은 빈 문자열로 노출한다.
constexpr std::string_view to_string(Regime regime) noexcept {
    switch (regime) {
    case Regime::unknown:
        return "unknown";
    case Regime::busy:
        return "busy";
    case Regime::idle:
        return "idle";
    }
    return {};
}

// Device별 온도 override 상태. cool_temp~hot_temp 데드밴드를 히스테리시스 latch로 쓴다.
enum class Thermal : std::uint8_t { cool, hot };

// 로그/진단용 이름. 어휘 밖 값은 빈 문자열로 노출한다.
constexpr std::string_view to_string(Thermal thermal) noexcept {
    switch (thermal) {
    case Thermal::cool:
        return "cool";
    case Thermal::hot:
        return "hot";
    }
    return {};
}

// 온도 override 룰(선택). load와 독립 축이며 thermal이 이긴다.
struct ThermalRule {
    double hot_temp;       // 트립 임계 (이 위로 가면 과열 -> hot)
    double cool_temp;      // 해제 임계 (이 아래로 식으면 Group Base Mode로 복귀; cool < hot)
    device::Mode hot_mode; // hot일 때 그 Device에만 강제할 모드
};

// 그룹 정책 룰. 히스테리시스(busy/idle) load 임계 + 전환 모드 + 선택적 온도 override
// 평균 load > busy_load면 busy_mode, < idle_load면 idle_mode, 그 사이면 유지 (밴드)
class GroupRule {
public:
    // 밴드 불변식 idle_load < busy_load를 강제한다. 위반 시 nullopt
    // 역전/동일 임계는 매 evaluate마다 busy와 idle 사이 발진을 만든다.
    // 두 임계 자체의 부호/범위는 정책 작성자 재량이다. load 도메인이 무경계 f64이기 때문이다.
    [[nodiscard]] static std::optional<GroupRule> create(
        double busy_load, double idle_load, device::Mode busy_mode, device::Mode idle_mode,
        std::optional<ThermalRule> thermal = std::nullopt
    ) noexcept {
        if (!(idle_load < busy_load)) {
            return std::nullopt;
        }
        if (thermal && !(thermal->cool_temp < thermal->hot_temp)) {
            return std::nullopt; // thermal 밴드도 cool < hot 강제
        }
        return GroupRule{busy_load, idle_load, busy_mode, idle_mode, thermal};
    }

    // busy 진입 임계 (이 위로 초과하면 busy)
    [[nodiscard]] double busy_load() const noexcept {
        return busy_load_;
    }

    // idle 진입 임계 (이 아래로 미만이면 idle)
    [[nodiscard]] double idle_load() const noexcept {
        return idle_load_;
    }

    // busy일 때 목표 모드
    [[nodiscard]] device::Mode busy_mode() const noexcept {
        return busy_mode_;
    }

    // idle일 때 목표 모드
    [[nodiscard]] device::Mode idle_mode() const noexcept {
        return idle_mode_;
    }

    // 온도 override 룰 (없으면 nullopt = thermal 비활성)
    [[nodiscard]] std::optional<ThermalRule> const& thermal() const noexcept {
        return thermal_;
    }

    // load 히스테리시스 전이. avg가 busy_load 초과면 busy, idle_load 미만이면 idle, 데드밴드(그
    // 사이)면 직전 판정을 유지한다. NaN avg는 두 비교가 모두 거짓이라 유지로 처리된다.
    [[nodiscard]] Regime next_regime(Regime prev, double avg) const noexcept {
        if (avg > busy_load_) {
            return Regime::busy;
        }
        if (avg < idle_load_) {
            return Regime::idle;
        }
        return prev;
    }

    // thermal 히스테리시스 전이. 룰에 thermal이 없으면(리로드로 제거된 경우 포함) 항상 cool을
    // 돌려줘 latch를 해제하고 load 제어로 복귀시킨다.
    [[nodiscard]] Thermal next_thermal(Thermal prev, double temp) const noexcept {
        if (!thermal_) {
            return Thermal::cool;
        }
        if (temp > thermal_->hot_temp) {
            return Thermal::hot;
        }
        if (temp < thermal_->cool_temp) {
            return Thermal::cool;
        }
        return prev;
    }

    // load와 thermal을 합성한 effective mode. hot이면 hot_mode, 아니면 regime의 base
    // mode(busy=busy_mode, idle=idle_mode)이다. regime 미확정(unknown)이면 직전 명령이
    // 비상모드일 때만 idle_mode로 latch를 풀고, 그 외에는 nullopt(아직 결정 없음)이다.
    // hot인데 thermal 룰이 없는 조합은 next_thermal이 만들지 않지만, 들어와도 regime 분기로
    // 강등해 전 입력에서 안전하다.
    [[nodiscard]] std::optional<device::Mode> effective_mode(
        Regime regime, Thermal thermal, std::optional<device::Mode> commanded
    ) const noexcept {
        if (thermal == Thermal::hot && thermal_) {
            return thermal_->hot_mode;
        }
        switch (regime) {
        case Regime::busy:
            return busy_mode_;
        case Regime::idle:
            return idle_mode_;
        case Regime::unknown:
            break;
        }
        if (thermal_ && commanded == thermal_->hot_mode) {
            return idle_mode_;
        }
        return std::nullopt;
    }

private:
    GroupRule(
        double busy_load, double idle_load, device::Mode busy_mode, device::Mode idle_mode,
        std::optional<ThermalRule> thermal
    ) noexcept
        : busy_load_(busy_load),
          idle_load_(idle_load),
          busy_mode_(busy_mode),
          idle_mode_(idle_mode),
          thermal_(thermal) {}

    double busy_load_;
    double idle_load_;
    device::Mode busy_mode_;
    device::Mode idle_mode_;
    std::optional<ThermalRule> thermal_;
};

// 그룹에서 룰로 가는 정책. 순수 값객체(json 무지)이며 set_policy로 통째 교체되는 핫스왑 단위다.
class GroupPolicy {
public:
    // 빌드 (있으면 갱신, 없으면 뒤에 추가, 삽입순 유지)
    void set(std::string group, GroupRule rule) {
        for (auto& [g, r] : rules_) {
            if (g == group) {
                r = rule;
                return;
            }
        }
        rules_.emplace_back(std::move(group), rule);
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return rules_.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        return rules_.empty();
    }

    // 알려진 그룹인지 확인
    [[nodiscard]] bool contains(std::string_view group) const noexcept {
        for (auto const& [g, r] : rules_) {
            if (g == group) {
                return true;
            }
        }
        return false;
    }

    // fn(std::string const& group, GroupRule const& rule)
    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (auto const& [group, rule] : rules_) {
            fn(group, rule);
        }
    }

private:
    std::vector<std::pair<std::string, GroupRule>> rules_;
};

} // namespace ddcs::ctrl::domain
