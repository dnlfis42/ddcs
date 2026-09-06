#pragma once

#include "ddcs/profile/recorder.hpp"
#include "ddcs/profile/recording_metadata.hpp"

#include <optional>
#include <string>

namespace ddcs::profile {

// recording metadata와 raw samples를 schema_version 1의 compact JSON으로 만든다.
// run_id·UTC bracket 또는 outcome·경계 계약을 지키지 않는 표본이 하나라도 있으면 nullopt를
// 반환한다.
[[nodiscard]] std::optional<std::string>
serialize_recording(RecordingView const& recording, RunMetadata const& metadata);

} // namespace ddcs::profile
