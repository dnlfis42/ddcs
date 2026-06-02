#pragma once

#include <unistd.h>

namespace ddcs::common {

// POSIX fd RAII. close(2) on destruction. 멱등 reset.
class Fd {
public:
    static constexpr int invalid{-1};

public:
    Fd() noexcept = default;
    explicit Fd(int fd) noexcept : fd_{fd} {}
    ~Fd() { reset(); }

    Fd(Fd const&) = delete;
    Fd& operator=(Fd const&) = delete;
    Fd(Fd&& other) noexcept : fd_{other.fd_} { other.fd_ = invalid; }
    Fd& operator=(Fd&& other) noexcept {
        if (this != &other) {
            reset();
            fd_ = other.fd_;
            other.fd_ = invalid;
        }
        return *this;
    }

public:
    int get() const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ != invalid; }
    explicit operator bool() const noexcept { return valid(); }

public:
    [[nodiscard]]
    int release() noexcept {
        int tmp = fd_;
        fd_ = invalid;
        return tmp;
    }

    void reset(int fd = invalid) noexcept {
        if (fd_ != fd) {
            ::close(fd_);
            fd_ = fd;
        }
    }

private:
    int fd_{invalid};
};

} // namespace ddcs::common
