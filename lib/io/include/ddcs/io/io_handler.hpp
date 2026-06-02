#pragma once

#include <cstdint>

namespace ddcs::io {

/**
 * @brief I/O 이벤트를 처리하는 핸들러 인터페이스
 *
 */
class IoHandler {
public:
    virtual ~IoHandler() = default;
    virtual void on_io(std::uint32_t events) = 0;
};

} // namespace ddcs::io
