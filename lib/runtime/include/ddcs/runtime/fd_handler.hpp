#pragma once

#include <cstdint>

namespace ddcs::runtime {

// Reactor에 등록된 fd 준비 이벤트를 받는다.
class FdHandler {
public:
    virtual ~FdHandler() = default;
    virtual void on_fd_event(std::uint32_t events) = 0;
};

} // namespace ddcs::runtime
