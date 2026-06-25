#pragma once

#include "ddcs/device/mode.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ddcs::ctrl::domain {

// 그룹 정책 룰. 히스테리시스(high/low) 임계 + 전환 모드
// 평균 load > high_load면 busy_mode, < low_load면 idle_mode, 그 사이면 유지 (밴드)
class GroupRule {
public:
    // 밴드 불변식 low_load < high_load를 강제한다. 위반 시 nullopt
    // 역전/동일 임계는 매 evaluate마다 busy와 idle 사이 발진을 만든다.
    // high/low 자체의 부호/범위는 정책 작성자 재량이다. load 도메인이 무경계 f64이기 때문이다.
    static std::optional<GroupRule> try_make(
        double high_load, double low_load, device::Mode busy_mode, device::Mode idle_mode
    ) noexcept {
        if (!(low_load < high_load)) {
            return std::nullopt;
        }
        return GroupRule{high_load, low_load, busy_mode, idle_mode};
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
    device::Mode busy_mode() const noexcept {
        return busy_mode_;
    }

    // 회복 시 목표 모드
    device::Mode idle_mode() const noexcept {
        return idle_mode_;
    }

private:
    GroupRule(
        double high_load, double low_load, device::Mode busy_mode, device::Mode idle_mode
    ) noexcept
        : high_load_(high_load),
          low_load_(low_load),
          busy_mode_(busy_mode),
          idle_mode_(idle_mode) {}

    double high_load_;
    double low_load_;
    device::Mode busy_mode_;
    device::Mode idle_mode_;
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
