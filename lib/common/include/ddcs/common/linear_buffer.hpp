#pragma once

#include <memory>
#include <span>
#include <type_traits>

#include <cstddef>
#include <cstring>

namespace ddcs::common {

class LinearBuffer {
public:
    explicit LinearBuffer(std::size_t capacity)
        : buf_{new std::byte[capacity]}, capacity_{capacity} {}
    ~LinearBuffer() = default;

    LinearBuffer(LinearBuffer const&) = delete;
    LinearBuffer& operator=(LinearBuffer const&) = delete;
    LinearBuffer(LinearBuffer&&) noexcept = delete;
    LinearBuffer& operator=(LinearBuffer&&) noexcept = delete;

public: // observer
    std::size_t capacity() const noexcept { return capacity_; }
    std::size_t size() const noexcept { return write_pos_ - read_pos_; }
    std::size_t available() const noexcept { return capacity_ - write_pos_; }
    bool empty() const noexcept { return read_pos_ == write_pos_; }

public: // stream state
    explicit operator bool() const noexcept { return !fail_; }
    void set_fail() noexcept { fail_ = true; }

public: // zero-copy region
    std::span<std::byte const> readable() const noexcept {
        return {buf_.get() + read_pos_, size()};
    }
    std::span<std::byte> writable() noexcept { return {buf_.get() + write_pos_, available()}; }

public: // cursor advance
    bool reserve(std::size_t n) noexcept {
        if (read_pos_ != 0 || write_pos_ != 0 || n > capacity_) {
            return false;
        }
        read_pos_ = n;
        write_pos_ = n;
        return true;
    }
    bool consume(std::size_t n) noexcept {
        if (size() < n) {
            return false;
        }
        read_pos_ += n;
        return true;
    }
    bool commit(std::size_t n) noexcept {
        if (available() < n) {
            return false;
        }
        write_pos_ += n;
        return true;
    }

    void clear() noexcept {
        read_pos_ = 0;
        write_pos_ = 0;
        fail_ = false;
    }
    void reset() noexcept { clear(); }

public: // copy I/O
    bool peek(std::span<std::byte> dst) const noexcept {
        if (size() < dst.size()) {
            return false;
        }
        std::memcpy(dst.data(), buf_.get() + read_pos_, dst.size());
        return true;
    }
    bool read(std::span<std::byte> dst) noexcept {
        if (!peek(dst)) {
            return false;
        }
        read_pos_ += dst.size();
        return true;
    }
    bool write(std::span<std::byte const> src) noexcept {
        if (available() < src.size()) {
            return false;
        }
        std::memcpy(buf_.get() + write_pos_, src.data(), src.size());
        write_pos_ += src.size();
        return true;
    }
    bool write_front(std::span<std::byte const> src) noexcept {
        if (read_pos_ < src.size()) {
            return false;
        }
        read_pos_ -= src.size();
        std::memcpy(buf_.get() + read_pos_, src.data(), src.size());
        return true;
    }

public: // stream serialization
    template <typename T>
        requires std::is_arithmetic_v<T>
    LinearBuffer& operator<<(T v) noexcept {
        if (fail_) {
            return *this;
        }
        if (available() < sizeof(T)) {
            fail_ = true;
            return *this;
        }
        std::memcpy(buf_.get() + write_pos_, &v, sizeof(T));
        write_pos_ += sizeof(T);
        return *this;
    }
    template <typename T>
        requires std::is_arithmetic_v<T>
    LinearBuffer& operator>>(T& out) noexcept {
        if (fail_) {
            return *this;
        }
        if (size() < sizeof(T)) {
            fail_ = true;
            return *this;
        }
        std::memcpy(&out, buf_.get() + read_pos_, sizeof(T));
        read_pos_ += sizeof(T);
        return *this;
    }

private:
    std::unique_ptr<std::byte[]> buf_;
    std::size_t capacity_;
    std::size_t read_pos_{0};
    std::size_t write_pos_{0};
    bool fail_{false};
};

} // namespace ddcs::common
