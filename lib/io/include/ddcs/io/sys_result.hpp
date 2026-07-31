#pragma once

namespace ddcs::io {

/// @brief syscall 결과
///
/// - 성공: ok = true, err = 0
/// - 실패: ok = false, err = 0
/// - 에러: ok = false, err > 0
struct [[nodiscard]] SysResult {
    bool ok = false;
    int err = 0; ///< 실패 시점에 캡처한 errno

    explicit operator bool() const noexcept {
        return ok;
    }

    static SysResult success() noexcept {
        return {true, 0};
    }

    static SysResult fail(int err = 0) noexcept {
        return {false, err};
    }
};

} // namespace ddcs::io
