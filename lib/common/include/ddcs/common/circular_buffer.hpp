#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>

namespace ddcs::common {

class CircularBuffer {
public:
    explicit CircularBuffer(std::size_t capacity)
        : storage_(new std::byte[checked_capacity(capacity)]),
          capacity_(capacity),
          index_mask_(capacity - 1) {}
    ~CircularBuffer() = default;

    CircularBuffer(CircularBuffer const&) = delete;
    CircularBuffer& operator=(CircularBuffer const&) = delete;
    CircularBuffer(CircularBuffer&&) noexcept = delete;
    CircularBuffer& operator=(CircularBuffer&&) noexcept = delete;

    [[nodiscard]] static constexpr bool valid_capacity(std::size_t capacity) noexcept {
        return capacity > 0 && (capacity & (capacity - 1)) == 0;
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return capacity_;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return write_seq_ - read_seq_;
    }

    [[nodiscard]] bool empty() const noexcept {
        return read_seq_ == write_seq_;
    }

    [[nodiscard]] bool full() const noexcept {
        return size() == capacity_;
    }

    [[nodiscard]] std::size_t writable_size() const noexcept {
        return capacity_ - size();
    }

    [[nodiscard]] std::span<std::byte const> readable_span() const noexcept {
        return {read_ptr(), contiguous_readable_size()};
    }

    [[nodiscard]] std::span<std::byte> writable_span() noexcept {
        return {write_ptr(), contiguous_writable_size()};
    }

    [[nodiscard]] bool try_peek(std::span<std::byte> dst) const noexcept {
        if (size() < dst.size()) {
            return false;
        }

        std::size_t const idx = read_index();
        std::size_t const first = std::min(dst.size(), capacity_ - idx);

        if (first > 0) {
            std::memcpy(dst.data(), storage_.get() + idx, first);

            std::size_t const second = dst.size() - first;
            if (second > 0) {
                std::memcpy(dst.data() + first, storage_.get(), second);
            }
        }

        return true;
    }

    [[nodiscard]] bool try_read(std::span<std::byte> dst) noexcept {
        if (!try_peek(dst)) {
            return false;
        }

        read_seq_ += dst.size();
        return true;
    }

    [[nodiscard]] bool try_consume(std::size_t n) noexcept {
        if (size() < n) {
            return false;
        }

        read_seq_ += n;
        return true;
    }

    [[nodiscard]] bool try_write(std::span<std::byte const> src) noexcept {
        if (writable_size() < src.size()) {
            return false;
        }

        std::size_t const idx = write_index();
        std::size_t const first = std::min(src.size(), capacity_ - idx);

        if (first > 0) {
            std::memcpy(storage_.get() + idx, src.data(), first);

            std::size_t const second = src.size() - first;
            if (second > 0) {
                std::memcpy(storage_.get(), src.data() + first, second);
            }

            write_seq_ += src.size();
        }

        return true;
    }

    [[nodiscard]] bool try_commit(std::size_t n) noexcept {
        if (writable_size() < n) {
            return false;
        }

        write_seq_ += n;
        return true;
    }

    void clear() noexcept {
        read_seq_ = 0;
        write_seq_ = 0;
    }

    void reset() noexcept {
        clear();
    }

private:
    [[nodiscard]] static std::size_t checked_capacity(std::size_t capacity) {
        if (!valid_capacity(capacity)) {
            throw std::invalid_argument{"CircularBuffer capacity must be a power of two"};
        }
        return capacity;
    }

    [[nodiscard]] std::size_t index_of(std::size_t seq) const noexcept {
        return seq & index_mask_;
    }

    [[nodiscard]] std::size_t read_index() const noexcept {
        return index_of(read_seq_);
    }

    [[nodiscard]] std::size_t write_index() const noexcept {
        return index_of(write_seq_);
    }

    [[nodiscard]] std::byte const* read_ptr() const noexcept {
        return storage_.get() + read_index();
    }

    [[nodiscard]] std::byte* write_ptr() noexcept {
        return storage_.get() + write_index();
    }

    [[nodiscard]] std::size_t contiguous_readable_size() const noexcept {
        return std::min(size(), capacity_ - read_index());
    }

    [[nodiscard]] std::size_t contiguous_writable_size() const noexcept {
        return std::min(writable_size(), capacity_ - write_index());
    }

    const std::unique_ptr<std::byte[]> storage_;
    const std::size_t capacity_;
    const std::size_t index_mask_;
    std::size_t read_seq_ = 0;
    std::size_t write_seq_ = 0;
};

} // namespace ddcs::common
