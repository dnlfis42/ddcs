#pragma once

#include <unistd.h>

namespace ddcs::common {

// POSIX fd 소유권을 표현한다. reset과 소멸자는 보유 fd를 close(2) 한다.
class Fd {
public:
    static constexpr int invalid{-1};

public:
    Fd() noexcept = default;
    explicit Fd(int fd) noexcept : fd_{fd} {}
    ~Fd() noexcept { reset(); }

    Fd(Fd const&) = delete;
    Fd& operator=(Fd const&) = delete;
    Fd(Fd&& other) noexcept : fd_{other.release()} {}
    Fd& operator=(Fd&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

public:
    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ != invalid; }
    explicit operator bool() const noexcept { return valid(); }

public:
    [[nodiscard]] int release() noexcept {
        int tmp = fd_;
        fd_ = invalid;
        return tmp;
    }

    void reset(int fd = invalid) noexcept {
        if (fd_ == fd) {
            return;
        }
        if (fd_ != invalid) {
            (void)::close(fd_);
        }
        fd_ = fd;
    }

private:
    int fd_{invalid};
};

} // namespace ddcs::common
