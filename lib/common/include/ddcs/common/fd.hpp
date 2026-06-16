#pragma once

#include <unistd.h>

namespace ddcs::common {

// POSIX fd 래퍼
class Fd {
public:
    static constexpr int invalid{-1};

public:
    Fd() noexcept = default;
    explicit Fd(int fd) noexcept
        : fd_{fd} {}

    ~Fd() noexcept {
        close();
    }

    Fd(Fd const&) = delete;
    Fd& operator=(Fd const&) = delete;

    Fd(Fd&& other) noexcept
        : fd_{other.release()} {}

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
        if (fd_ == fd) {
            return;
        }
        close();
        fd_ = fd;
    }

private:
    int fd_{invalid};
};

} // namespace ddcs::common
