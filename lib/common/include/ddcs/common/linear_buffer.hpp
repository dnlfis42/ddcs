#pragma once

#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <type_traits>

namespace ddcs::common {

class LinearBuffer {
public:
    explicit LinearBuffer(std::size_t capacity) : buffer_{new std::byte[capacity]}, capacity_{capacity} {}
    ~LinearBuffer() = default;

    LinearBuffer(LinearBuffer const&) = delete;
    LinearBuffer& operator=(LinearBuffer const&) = delete;
    LinearBuffer(LinearBuffer&&) noexcept = delete;
    LinearBuffer& operator=(LinearBuffer&&) noexcept = delete;

    std::size_t capacity() const noexcept { return capacity_; }
    std::size_t size() const noexcept { return write_pos_ - read_pos_; }
    std::size_t available() const noexcept { return capacity_ - write_pos_; }
    bool empty() const noexcept { return read_pos_ == write_pos_; }
    [[nodiscard]] bool stream_failed() const noexcept { return stream_failed_; }

    std::span<std::byte const> readable() const noexcept { return {buffer_.get() + read_pos_, size()}; }
    std::span<std::byte> writable() noexcept { return {buffer_.get() + write_pos_, available()}; }

    void set_stream_failed() noexcept { stream_failed_ = true; }

    bool commit(std::size_t n) noexcept {
        if (available() < n) {
            return false;
        }
        write_pos_ += n;
        return true;
    }

    bool consume(std::size_t n) noexcept {
        if (size() < n) {
            return false;
        }
        read_pos_ += n;
        return true;
    }

    // NOTE: 버퍼가 빈 경우에만 예약 가능
    bool reserve_front(std::size_t n) noexcept {
        if (!empty() || read_pos_ + n > capacity_) {
            return false;
        }
        read_pos_ += n;
        write_pos_ = read_pos_;
        return true;
    }

    bool write_front(std::span<std::byte const> src) noexcept {
        if (read_pos_ < src.size()) {
            return false;
        }
        read_pos_ -= src.size();
        std::memcpy(buffer_.get() + read_pos_, src.data(), src.size());
        return true;
    }

    bool write(std::span<std::byte const> src) noexcept {
        if (available() < src.size()) {
            return false;
        }
        std::memcpy(buffer_.get() + write_pos_, src.data(), src.size());
        write_pos_ += src.size();
        return true;
    }

    bool read(std::span<std::byte> dst) noexcept {
        if (!peek(dst)) {
            return false;
        }
        read_pos_ += dst.size();
        return true;
    }

    bool peek(std::span<std::byte> dst) const noexcept {
        if (size() < dst.size()) {
            return false;
        }
        std::memcpy(dst.data(), buffer_.get() + read_pos_, dst.size());
        return true;
    }

    template <typename T>
        requires std::is_arithmetic_v<T>
    LinearBuffer& operator<<(T v) noexcept {
        if (stream_failed_) {
            return *this;
        }
        if (available() < sizeof(T)) {
            stream_failed_ = true;
            return *this;
        }
        std::memcpy(buffer_.get() + write_pos_, &v, sizeof(T));
        write_pos_ += sizeof(T);
        return *this;
    }

    template <typename T>
        requires std::is_arithmetic_v<T>
    LinearBuffer& operator>>(T& out) noexcept {
        if (stream_failed_) {
            return *this;
        }
        if (size() < sizeof(T)) {
            stream_failed_ = true;
            return *this;
        }
        std::memcpy(&out, buffer_.get() + read_pos_, sizeof(T));
        read_pos_ += sizeof(T);
        return *this;
    }

    void clear() noexcept {
        read_pos_ = 0;
        write_pos_ = 0;
        stream_failed_ = false;
    }

    void reset() noexcept { clear(); }

private:
    std::unique_ptr<std::byte[]> buffer_;
    std::size_t capacity_;
    std::size_t read_pos_{0};
    std::size_t write_pos_{0};
    bool stream_failed_{false};
};

} // namespace ddcs::common
