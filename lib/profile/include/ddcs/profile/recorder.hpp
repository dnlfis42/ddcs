#pragma once

#include "ddcs/profile/tick_sample.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace ddcs::profile {

// Recorder가 살아 있는 동안에만 유효한, 완료된 기록의 읽기 전용 view.
class RecordingView {
public:
    [[nodiscard]] std::span<TickSample const> samples() const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] std::size_t storage_bytes() const noexcept;
    [[nodiscard]] std::uint64_t dropped() const noexcept;

private:
    friend class Recorder;

    RecordingView(
        TickSample const* samples, std::size_t captured, std::size_t capacity,
        std::size_t storage_bytes, std::uint64_t dropped
    ) noexcept;

    TickSample const* samples_;
    std::size_t captured_;
    std::size_t capacity_;
    std::size_t storage_bytes_;
    std::uint64_t dropped_;
};

// 단일 writer용 고정 용량 tick recorder. 생성 뒤 record()와 finish()는 할당하지 않는다.
class Recorder {
public:
    static constexpr std::size_t default_capacity = 16'384;

    explicit Recorder(std::size_t capacity = default_capacity);
    ~Recorder() = default;

    Recorder(Recorder const&) = delete;
    Recorder& operator=(Recorder const&) = delete;
    Recorder(Recorder&&) = delete;
    Recorder& operator=(Recorder&&) = delete;

    void record(TickSample const& sample) noexcept;
    [[nodiscard]] RecordingView finish() noexcept;

    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] std::size_t storage_bytes() const noexcept;
    [[nodiscard]] bool finished() const noexcept;

private:
    [[nodiscard]] static std::size_t validated_capacity(std::size_t capacity);
    [[nodiscard]] static std::size_t checked_storage_bytes(std::size_t capacity);
    void touch_storage() noexcept;

    std::size_t const capacity_;
    std::size_t const storage_bytes_;
    std::unique_ptr<TickSample[]> storage_;
    std::size_t captured_ = 0;
    std::uint64_t dropped_ = 0;
    bool finished_ = false;
};

} // namespace ddcs::profile
