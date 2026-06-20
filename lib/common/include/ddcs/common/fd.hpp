#pragma once

#include <unistd.h>

namespace ddcs::common {

// POSIX fd의 RAII 소유자
//   소멸 시 close한다.
class Fd {
public:
    static constexpr int invalid = -1;

    Fd() noexcept = default;
    explicit Fd(int fd) noexcept
        : fd_(fd) {}

    ~Fd() noexcept {
        close();
    }

    Fd(Fd const&) = delete;
    Fd& operator=(Fd const&) = delete;

    Fd(Fd&& other) noexcept
        : fd_(other.release()) {}

    Fd& operator=(Fd&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept {
        return fd_;
    }

    [[nodiscard]] bool valid() const noexcept {
        return fd_ != invalid;
    }

    [[nodiscard]] int release() noexcept {
        int tmp = fd_;
        fd_ = invalid;
        return tmp;
    }

    void close() noexcept {
        if (valid()) {
            (void)::close(fd_);
            fd_ = invalid;
        }
    }

    void reset(int fd = invalid) noexcept {
        // 같은 fd로 reset하면 self-close를 피해 그대로 둔다.
        if (fd_ == fd) {
            return;
        }

        close();
        fd_ = fd;
    }

private:
    int fd_ = invalid;
};

} // namespace ddcs::common
