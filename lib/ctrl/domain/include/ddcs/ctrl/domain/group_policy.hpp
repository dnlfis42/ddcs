#pragma once

#include "ddcs/device/mode.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ddcs::ctrl::domain {

// 온도 override 룰(선택). max_temp > high_temp면 high_temp_mode 강제, < resume_temp면 해제.
// 단방향 트립(과열)이고 resume_temp는 그 해제 임계(데드밴드)다. load와 독립 축이며 thermal이 이긴다.
struct ThermalRule {
    double high_temp;            // 트립 임계 (이 위로 가면 과열)
    double resume_temp;          // 해제 임계 (이 아래로 식으면 복귀; resume < high)
    device::Mode high_temp_mode; // 과열 시 강제 모드
};

// 그룹 정책 룰. 히스테리시스(high/low) load 임계 + 전환 모드 + 선택적 온도 override
// 평균 load > high_load면 high_load_mode, < low_load면 low_load_mode, 그 사이면 유지 (밴드)
class GroupRule {
public:
    // 밴드 불변식 low_load < high_load를 강제한다. 위반 시 nullopt
    // 역전/동일 임계는 매 evaluate마다 busy와 idle 사이 발진을 만든다.
    // high/low 자체의 부호/범위는 정책 작성자 재량이다. load 도메인이 무경계 f64이기 때문이다.
    static std::optional<GroupRule> try_make(
        double high_load, double low_load, device::Mode high_load_mode, device::Mode low_load_mode,
        std::optional<ThermalRule> thermal = std::nullopt
    ) noexcept {
        if (!(low_load < high_load)) {
            return std::nullopt;
        }
        if (thermal && !(thermal->resume_temp < thermal->high_temp)) {
            return std::nullopt; // thermal 밴드도 resume < high 강제
        }
        return GroupRule{high_load, low_load, high_load_mode, low_load_mode, thermal};
    }

    // 초과 임계
    double high_load() const noexcept {
        return high_load_;
    }

    // 복귀 임계
    double low_load() const noexcept {
        return low_load_;
    }

    // 초과 시 목표 모드
    device::Mode high_load_mode() const noexcept {
        return high_load_mode_;
    }

    // 회복 시 목표 모드
    device::Mode low_load_mode() const noexcept {
        return low_load_mode_;
    }

    // 온도 override 룰 (없으면 nullopt = thermal 비활성)
    std::optional<ThermalRule> const& thermal() const noexcept {
        return thermal_;
    }

private:
    GroupRule(
        double high_load, double low_load, device::Mode high_load_mode, device::Mode low_load_mode,
        std::optional<ThermalRule> thermal
    ) noexcept
        : high_load_(high_load),
          low_load_(low_load),
          high_load_mode_(high_load_mode),
          low_load_mode_(low_load_mode),
          thermal_(thermal) {}

    double high_load_;
    double low_load_;
    device::Mode high_load_mode_;
    device::Mode low_load_mode_;
    std::optional<ThermalRule> thermal_;
};

// 그룹에서 룰로 가는 정책
// - 부팅 시 policy.json에서 빌드(app의 parse_policy), PolicyService가 평가에 사용한다.
// - 순수 값객체 (json 무지)
// - 핫스왑 단위 (set_policy로 통째 교체)
class GroupPolicy {
public:
    // 빌드 (있으면 갱신, 없으면 append, 삽입순 유지)
    void set(std::string group, GroupRule rule) {
        for (auto& [g, r] : rules_) {
            if (g == group) {
                r = rule;
                return;
            }
        }
        rules_.emplace_back(std::move(group), rule);
    }

    bool empty() const noexcept {
        return rules_.empty();
    }
    std::size_t size() const noexcept {
        return rules_.size();
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
