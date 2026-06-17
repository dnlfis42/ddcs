#pragma once

#include <system_error>

namespace ddcs::common {

[[noreturn]] inline void throw_errno(int err, char const* context) {
    throw std::system_error{err, std::system_category(), context};
}

} // namespace ddcs::common
