#include "ddcs/ctrl/app/device/group_aggregate.hpp"

#include "ddcs/ctrl/domain/device_shadow.hpp"
#include "ddcs/device/mode.hpp"

namespace ddcs::ctrl::app::device {

std::map<std::string, GroupAggregate> aggregate_groups(
    port::ActiveDevices& active_devices, domain::DeviceRegistry const& devices,
    domain::GroupPolicy const& policy
) {
    std::map<std::string, GroupAggregate> groups;
    active_devices.for_each_active([&](domain::DeviceId id) {
        auto const* shadow = devices.find(id);
        if (shadow == nullptr || !shadow->status || shadow->group.empty() ||
            !policy.contains(shadow->group)) {
            return; // 미관측이거나 정책 밖 group이면 제외 -- 기본값과 config 밖 label을 차단
        }

        auto& a = groups[shadow->group];
        a.device_count += 1;
        a.load_sum += shadow->status->load;
        a.temp_sum += shadow->status->temp;
        a.by_mode[ddcs::device::encode_mode(shadow->status->mode)] += 1;
    });
    return groups;
}

} // namespace ddcs::ctrl::app::device
