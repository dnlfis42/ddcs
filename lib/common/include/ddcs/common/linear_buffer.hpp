#pragma once

#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>

namespace ddcs::common {

// 앞뒤로 여유 공간을 둔 선형 바이트 버퍼. 저장 공간은 headroom [0, data_offset_),
// data [data_offset_, tail_offset_), tailroom [tail_offset_, capacity_) 세 구역으로 나뉜다.
class LinearBuffer {
public:
    explicit LinearBuffer(std::size_t capacity)
        : storage_(new std::byte[capacity]),
          capacity_(capacity) {}
    ~LinearBuffer() = default;

    LinearBuffer(LinearBuffer const&) = delete;
    LinearBuffer& operator=(LinearBuffer const&) = delete;
    LinearBuffer(LinearBuffer&&) noexcept = delete;
    LinearBuffer& operator=(LinearBuffer&&) noexcept = delete;

    [[nodiscard]] std::size_t capacity() const noexcept {
        return capacity_;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return tail_offset_ - data_offset_;
    }

    [[nodiscard]] bool empty() const noexcept {
        return data_offset_ == tail_offset_;
    }

    [[nodiscard]] std::size_t headroom_size() const noexcept {
        return data_offset_;
    }

    [[nodiscard]] std::size_t tailroom_size() const noexcept {
        return capacity_ - tail_offset_;
    }

    [[nodiscard]] std::span<std::byte const> data_span() const noexcept {
        return {data_ptr(), size()};
    }

    [[nodiscard]] std::span<std::byte> tailroom_span() noexcept {
        return {tailroom_ptr(), tailroom_size()};
    }

    // 앞쪽 data 영역의 n 바이트를 소비한다. 복사 없이 offset만 전진한다.
    [[nodiscard]] bool consume(std::size_t n) noexcept {
        if (size() < n) {
            return false;
        }

        data_offset_ += n;
        assert_invariant();
        return true;
    }

    // 뒤쪽 tailroom의 n 바이트를 data 영역으로 확정한다.
    [[nodiscard]] bool commit(std::size_t n) noexcept {
        if (tailroom_size() < n) {
            return false;
        }

        tail_offset_ += n;
        assert_invariant();
        return true;
    }

    [[nodiscard]] bool peek(std::span<std::byte> dst) const noexcept {
        if (size() < dst.size()) {
            return false;
        }

        if (!dst.empty()) {
            std::memcpy(dst.data(), data_ptr(), dst.size());
        }
        return true;
    }

    [[nodiscard]] bool extract(std::span<std::byte> dst) noexcept {
        if (!peek(dst)) {
            return false;
        }

        data_offset_ += dst.size();
        assert_invariant();
        return true;
    }

    [[nodiscard]] bool append(std::span<std::byte const> src) noexcept {
        if (tailroom_size() < src.size()) {
            return false;
        }

        if (!src.empty()) {
            std::memcpy(storage_.get() + tail_offset_, src.data(), src.size());
            tail_offset_ += src.size();
            assert_invariant();
        }
        return true;
    }

    // 앞쪽 headroom에 src를 복사해 data 영역 앞에 붙인다.
    [[nodiscard]] bool prepend(std::span<std::byte const> src) noexcept {
        if (headroom_size() < src.size()) {
            return false;
        }

        if (!src.empty()) {
            data_offset_ -= src.size();
            assert_invariant();
            std::memcpy(storage_.get() + data_offset_, src.data(), src.size());
        }
        return true;
    }

    // headroom 크기를 n 바이트로 설정한다. 버퍼가 비어 있을 때만 성공한다.
    [[nodiscard]] bool set_headroom(std::size_t n) noexcept {
        if (!empty() || n > capacity()) {
            return false;
        }

        data_offset_ = n;
        tail_offset_ = n;
        assert_invariant();
        return true;
    }

    // headroom 크기를 n 바이트만큼 늘린다. 버퍼가 비어 있을 때만 성공한다.
    [[nodiscard]] bool grow_headroom(std::size_t n) noexcept {
        if (!empty() || n > tailroom_size()) {
            return false;
        }

        data_offset_ += n;
        tail_offset_ += n;
        assert_invariant();
        return true;
    }

    void clear() noexcept {
        data_offset_ = 0;
        tail_offset_ = 0;
    }

    void reset() noexcept {
        clear();
    }

private:
    std::byte const* data_ptr() const noexcept {
        return storage_.get() + data_offset_;
    }

    std::byte* tailroom_ptr() noexcept {
        return storage_.get() + tail_offset_;
    }

    // 불변식: data_offset_ <= tail_offset_ <= capacity_
    void assert_invariant() const noexcept {
        assert(data_offset_ <= tail_offset_);
        assert(tail_offset_ <= capacity_);
    }

    std::unique_ptr<std::byte[]> storage_;
    std::size_t capacity_;
    std::size_t data_offset_ = 0;
    std::size_t tail_offset_ = 0;
};

} // namespace ddcs::common
