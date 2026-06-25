#pragma once

#include <system_error>

namespace ddcs::io {

[[noreturn]] inline void throw_errno(int err, char const* context) {
    throw std::system_error{err, std::system_category(), context};
}

} // namespace ddcs::io
