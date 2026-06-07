#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>

namespace ddcs::common {

namespace detail {

// PERF: compile-time N이 memcpy 크기로 전파되면 중간 크기에서 rep movsq 인라인이 발생한다.
// PERF: noinline은 인라인을, GCC noclone은 IPA-CP constprop 클론 생성을 막는다.
#if defined(__GNUC__) && !defined(__clang__)
#define DDCS_RINGBUF_NOINLINE_NOCLONE [[gnu::noinline, gnu::noclone]]
#elif defined(__GNUC__) || defined(__clang__)
#define DDCS_RINGBUF_NOINLINE_NOCLONE [[gnu::noinline]]
#else
#define DDCS_RINGBUF_NOINLINE_NOCLONE
#endif

DDCS_RINGBUF_NOINLINE_NOCLONE
inline void copy_bytes(void* dst, void const* src, std::size_t n) noexcept { std::memcpy(dst, src, n); }

#undef DDCS_RINGBUF_NOINLINE_NOCLONE

} // namespace detail

template <std::size_t N>
concept valid_ring_buffer_capacity = (N > 0) && ((N & (N - 1)) == 0);

template <std::size_t N>
    requires valid_ring_buffer_capacity<N>
class RingBuffer {
public:
    RingBuffer() : buffer_{new std::byte[N]} {}
    ~RingBuffer() = default;

    RingBuffer(RingBuffer const&) = delete;
    RingBuffer& operator=(RingBuffer const&) = delete;
    RingBuffer(RingBuffer&&) noexcept = delete;
    RingBuffer& operator=(RingBuffer&&) noexcept = delete;

public:
    static constexpr std::size_t capacity() noexcept { return N; }
    std::size_t size() const noexcept { return write_pos_ - read_pos_; }
    std::size_t available() const noexcept { return N - size(); }
    bool empty() const noexcept { return read_pos_ == write_pos_; }
    bool full() const noexcept { return size() == N; }

public:
    std::span<std::byte const> readable() const noexcept {
        std::size_t const idx = read_pos_ & (N - 1);
        return {buffer_.get() + idx, std::min(size(), N - idx)};
    }

    std::span<std::byte> writable() noexcept {
        std::size_t const idx = write_pos_ & (N - 1);
        return {buffer_.get() + idx, std::min(available(), N - idx)};
    }

public:
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
    }

    void reset() noexcept { clear(); }

public:
    bool peek(std::span<std::byte> dst) const noexcept {
        if (size() < dst.size()) {
            return false;
        }
        std::size_t const idx = read_pos_ & (N - 1);
        std::size_t const first = std::min(dst.size(), N - idx);
        detail::copy_bytes(dst.data(), buffer_.get() + idx, first);
        std::size_t const second = dst.size() - first;
        if (second > 0) {
            detail::copy_bytes(dst.data() + first, buffer_.get(), second);
        }
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
        std::size_t const idx = write_pos_ & (N - 1);
        std::size_t const first = std::min(src.size(), N - idx);
        detail::copy_bytes(buffer_.get() + idx, src.data(), first);
        std::size_t const second = src.size() - first;
        if (second > 0) {
            detail::copy_bytes(buffer_.get(), src.data() + first, second);
        }
        write_pos_ += src.size();
        return true;
    }

private:
    std::unique_ptr<std::byte[]> buffer_;
    std::size_t read_pos_{0};
    std::size_t write_pos_{0};
};

} // namespace ddcs::common
