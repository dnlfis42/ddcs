#pragma once

#include "ddcs/device/mode.hpp"

#include <string>
#include <utility>
#include <vector>

#include <cstddef>

namespace ddcs::ctrl::domain {

// 그룹 정책 룰 - 히스테리시스(high/low) 임계 + 전환 모드.
// 평균 load > high_load -> busy_mode, < low_load -> idle_mode, 그 사이 -> 유지(밴드).
struct GroupRule {
    double high_load{};       // 초과 임계
    double low_load{};        // 복귀 임계
    device::Mode busy_mode{}; // 초과 시 목표 모드
    device::Mode idle_mode{}; // 회복 시 목표 모드
};

// 그룹->룰 정책. 부팅 시 policy.json에서 빌드(app 의 parse_policy), PolicyService가 평가에 사용.
// 순수 값객체(json 무지). 핫스왑 단위(set_policy로 통째 교체).
class GroupPolicy {
public:
    void set(std::string group, GroupRule rule) { // 빌드(있으면 갱신, 없으면 append - 삽입순 유지)
        for (auto& [g, r] : rules_) {
            if (g == group) {
                r = rule;
                return;
            }
        }
        rules_.emplace_back(std::move(group), rule);
    }

    bool empty() const noexcept { return rules_.empty(); }
    std::size_t size() const noexcept { return rules_.size(); }

    template <typename Fn>
    void for_each(Fn&& fn) const { // fn(std::string const& group, GroupRule const& rule)
        for (auto const& [group, rule] : rules_) {
            fn(group, rule);
        }
    }

private:
    std::vector<std::pair<std::string, GroupRule>> rules_;
};

} // namespace ddcs::ctrl::domain
