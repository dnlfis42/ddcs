#include "ddcs/profile/dump.hpp"

#include "ddcs/profile/serializer.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>

#include <sys/stat.h>
#include <unistd.h>

namespace ddcs::profile {

namespace {

DumpResult failure(DumpError error, int system_errno = 0, bool published = false) noexcept {
    return {
        .error = error,
        .system_error = system_errno == 0 ? std::error_code{}
                                          : std::error_code{system_errno, std::generic_category()},
        .published = published,
    };
}

void remove_temporary_file(std::filesystem::path const& path) noexcept {
    (void)::unlink(path.c_str());
}

bool write_all(int fd, std::string_view data, int& system_errno) noexcept {
    std::size_t offset = 0;
    while (offset < data.size()) {
        auto const remaining = data.size() - offset;
        auto const chunk =
            std::min(remaining, static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
        auto const written = ::write(fd, data.data() + offset, chunk);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }

            system_errno = errno;
            return false;
        }
        if (written == 0) {
            system_errno = EIO;
            return false;
        }

        offset += static_cast<std::size_t>(written);
    }

    return true;
}

} // namespace

DumpResult dump_recording(
    RecordingView const& recording, RunMetadata const& metadata,
    std::filesystem::path const& output_path
) {
    if (metadata.run_id.empty()) {
        return failure(DumpError::invalid_metadata);
    }

    auto const serialized = serialize_recording(recording, metadata);
    if (!serialized) {
        return failure(DumpError::invalid_recording);
    }

    auto temporary_template = output_path;
    temporary_template += ".tmp.XXXXXX";
    auto temporary_name = temporary_template.string();
    temporary_name.push_back('\0');

    int fd = ::mkstemp(temporary_name.data());
    if (fd == -1) {
        return failure(DumpError::temporary_file_create_failed, errno);
    }

    std::filesystem::path const temporary_path{temporary_name.data()};
    int system_errno = 0;
    // Docker bind mount에서 Controller가 root여도 host user가 결과를 분석할 수 있어야 한다.
    // raw tick timing에는 비밀값이 없으므로 완료 artifact의 명시적 mode는 0644로 고정한다.
    constexpr mode_t published_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    if (::fchmod(fd, published_mode) != 0) {
        system_errno = errno;
        (void)::close(fd);
        remove_temporary_file(temporary_path);
        return failure(DumpError::permission_failed, system_errno);
    }
    if (!write_all(fd, *serialized, system_errno)) {
        (void)::close(fd);
        remove_temporary_file(temporary_path);
        return failure(DumpError::write_failed, system_errno);
    }

    if (::fsync(fd) != 0) {
        system_errno = errno;
        (void)::close(fd);
        remove_temporary_file(temporary_path);
        return failure(DumpError::sync_failed, system_errno);
    }

    if (::close(fd) != 0) {
        system_errno = errno;
        remove_temporary_file(temporary_path);
        return failure(DumpError::close_failed, system_errno);
    }

    if (::link(temporary_path.c_str(), output_path.c_str()) != 0) {
        system_errno = errno;
        remove_temporary_file(temporary_path);
        if (system_errno == EEXIST) {
            return failure(DumpError::output_already_exists, system_errno);
        }
        return failure(DumpError::publish_failed, system_errno);
    }

    if (::unlink(temporary_path.c_str()) != 0) {
        return failure(DumpError::temporary_cleanup_failed, errno, true);
    }

    return {
        .error = DumpError::none,
        .published = true,
    };
}

} // namespace ddcs::profile
