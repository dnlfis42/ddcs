#pragma once

#include "ddcs/profile/serializer.hpp"

#include <filesystem>
#include <string_view>
#include <system_error>

namespace ddcs::profile {

enum class DumpError {
    none,
    invalid_recording,
    invalid_metadata,
    temporary_file_create_failed,
    permission_failed,
    write_failed,
    sync_failed,
    close_failed,
    output_already_exists,
    publish_failed,
    temporary_cleanup_failed,
};

[[nodiscard]] constexpr std::string_view to_string(DumpError error) noexcept {
    switch (error) {
    case DumpError::none:
        return "none";
    case DumpError::invalid_recording:
        return "invalid_recording";
    case DumpError::invalid_metadata:
        return "invalid_metadata";
    case DumpError::temporary_file_create_failed:
        return "temporary_file_create_failed";
    case DumpError::permission_failed:
        return "permission_failed";
    case DumpError::write_failed:
        return "write_failed";
    case DumpError::sync_failed:
        return "sync_failed";
    case DumpError::close_failed:
        return "close_failed";
    case DumpError::output_already_exists:
        return "output_already_exists";
    case DumpError::publish_failed:
        return "publish_failed";
    case DumpError::temporary_cleanup_failed:
        return "temporary_cleanup_failed";
    }

    return "unknown";
}

struct DumpResult {
    DumpError error = DumpError::none;
    std::error_code system_error{};
    bool published = false;

    [[nodiscard]] bool succeeded() const noexcept {
        return error == DumpError::none;
    }
};

// output_path와 같은 디렉터리에 임시 파일을 완성한 뒤, 기존 파일을 교체하지 않고 게시한다.
// 완료 파일은 실행자뿐 아니라 host-side 분석기도 읽을 수 있도록 mode 0644로 게시한다.
[[nodiscard]] DumpResult dump_recording(
    RecordingView const& recording, RunMetadata const& metadata,
    std::filesystem::path const& output_path
);

} // namespace ddcs::profile
