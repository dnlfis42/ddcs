#include "ddcs/ctrl/app/device/group_aggregate.hpp"

#include "ddcs/ctrl/domain/device_shadow.hpp"
#include "ddcs/device/mode.hpp"

namespace ddcs::ctrl::app::device {

std::map<std::string, GroupAggregate> aggregate_groups(
    port::DeviceRoster& roster, domain::DeviceRegistry const& devices,
    domain::GroupPolicy const& policy
) {
    std::map<std::string, GroupAggregate> groups;
    roster.for_each_active([&](domain::DeviceId id) {
        auto const* shadow = devices.find(id);
        if (shadow == nullptr || shadow->group.empty() || !policy.contains(shadow->group)) {
            return; // 정책 밖 group은 제외 -- label cardinality를 config group으로 한정
        }
        auto& a = groups[shadow->group];
        a.devices += 1;
        a.load_sum += shadow->status.load;
        a.temp_sum += shadow->status.temp;
        a.by_mode[ddcs::device::encode_mode(shadow->status.mode)] += 1;
    });
    return groups;
}

} // namespace ddcs::ctrl::app::device
