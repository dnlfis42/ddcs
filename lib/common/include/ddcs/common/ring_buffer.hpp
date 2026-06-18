#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>

#if defined(__GNUC__) && !defined(__clang__)
#define DDCS_RING_BUFFER_COPY_ATTR [[gnu::noinline, gnu::noclone]]
#elif defined(__GNUC__) || defined(__clang__)
#define DDCS_RING_BUFFER_COPY_ATTR [[gnu::noinline]]
#else
#define DDCS_RING_BUFFER_COPY_ATTR
#endif

namespace ddcs::common {

namespace detail::ring_buffer {

// 2의 거듭제곱 용량만 허용한다. index를 seq & (N - 1) 마스크로 계산하기 때문이다.
template <std::size_t N>
concept valid_capacity = (N > 0) && ((N & (N - 1)) == 0);

// PERF: 복사 크기가 상수 전파되면 큰 인라인 memcpy 시퀀스가 생성될 수 있다.
// - noinline : 함수 인라인을 막는다.
// - noclone  : 상수 전파 클론 생성을 막는다.
DDCS_RING_BUFFER_COPY_ATTR inline void
copy_bytes(void* dst, void const* src, std::size_t n) noexcept {
    std::memcpy(dst, src, n);
}

#undef DDCS_RING_BUFFER_COPY_ATTR

} // namespace detail::ring_buffer

template <std::size_t N>
    requires detail::ring_buffer::valid_capacity<N>
class RingBuffer {
public:
    RingBuffer()
        : storage_(new std::byte[N]) {}
    ~RingBuffer() = default;

    RingBuffer(RingBuffer const&) = delete;
    RingBuffer& operator=(RingBuffer const&) = delete;
    RingBuffer(RingBuffer&&) noexcept = delete;
    RingBuffer& operator=(RingBuffer&&) noexcept = delete;

    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return N;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return write_seq_ - read_seq_;
    }

    [[nodiscard]] bool empty() const noexcept {
        return read_seq_ == write_seq_;
    }

    [[nodiscard]] bool full() const noexcept {
        return size() == N;
    }

    [[nodiscard]] std::size_t writable_size() const noexcept {
        return N - size();
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
        std::size_t const first = std::min(dst.size(), N - idx);

        if (first > 0) {
            detail::ring_buffer::copy_bytes(dst.data(), storage_.get() + idx, first);

            std::size_t const second = dst.size() - first;
            if (second > 0) {
                detail::ring_buffer::copy_bytes(dst.data() + first, storage_.get(), second);
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
        std::size_t const first = std::min(src.size(), N - idx);

        if (first > 0) {
            detail::ring_buffer::copy_bytes(storage_.get() + idx, src.data(), first);

            std::size_t const second = src.size() - first;
            if (second > 0) {
                detail::ring_buffer::copy_bytes(storage_.get(), src.data() + first, second);
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
    static constexpr std::size_t index_mask = N - 1;

    static constexpr std::size_t index_of(std::size_t seq) noexcept {
        return seq & index_mask;
    }

    std::size_t read_index() const noexcept {
        return index_of(read_seq_);
    }

    std::size_t write_index() const noexcept {
        return index_of(write_seq_);
    }

    std::byte const* read_ptr() const noexcept {
        return storage_.get() + read_index();
    }

    std::byte* write_ptr() noexcept {
        return storage_.get() + write_index();
    }

    std::size_t contiguous_readable_size() const noexcept {
        return std::min(size(), N - read_index());
    }

    std::size_t contiguous_writable_size() const noexcept {
        return std::min(writable_size(), N - write_index());
    }

    std::unique_ptr<std::byte[]> storage_;
    std::size_t read_seq_ = 0;
    std::size_t write_seq_ = 0;
};

} // namespace ddcs::common
