#include "ddcs/profile/recorder.hpp"

#include <limits>
#include <stdexcept>

#include <unistd.h>

namespace ddcs::profile {

namespace {

constexpr std::size_t fallback_page_size = 4096;

[[nodiscard]] std::size_t page_size() noexcept {
    auto const queried = ::sysconf(_SC_PAGESIZE);
    if (queried <= 0) {
        return fallback_page_size;
    }

    return static_cast<std::size_t>(queried);
}

} // namespace

RecordingView::RecordingView(
    TickSample const* samples, std::size_t captured, std::size_t capacity,
    std::size_t storage_bytes, std::uint64_t dropped
) noexcept
    : samples_(samples),
      captured_(captured),
      capacity_(capacity),
      storage_bytes_(storage_bytes),
      dropped_(dropped) {}

std::span<TickSample const> RecordingView::samples() const noexcept {
    return {samples_, captured_};
}

std::size_t RecordingView::capacity() const noexcept {
    return capacity_;
}

std::size_t RecordingView::storage_bytes() const noexcept {
    return storage_bytes_;
}

std::uint64_t RecordingView::dropped() const noexcept {
    return dropped_;
}

Recorder::Recorder(std::size_t capacity)
    : capacity_(validated_capacity(capacity)),
      storage_bytes_(checked_storage_bytes(capacity_)),
      storage_(std::make_unique<TickSample[]>(capacity_)) {
    touch_storage();
}

void Recorder::record(TickSample const& sample) noexcept {
    if (finished_) {
        return;
    }

    if (captured_ == capacity_) {
        if (dropped_ != std::numeric_limits<std::uint64_t>::max()) {
            ++dropped_;
        }
        return;
    }

    storage_[captured_] = sample;
    ++captured_;
}

RecordingView Recorder::finish() noexcept {
    finished_ = true;
    return {storage_.get(), captured_, capacity_, storage_bytes_, dropped_};
}

std::size_t Recorder::capacity() const noexcept {
    return capacity_;
}

std::size_t Recorder::storage_bytes() const noexcept {
    return storage_bytes_;
}

bool Recorder::finished() const noexcept {
    return finished_;
}

std::size_t Recorder::validated_capacity(std::size_t capacity) {
    if (capacity == 0) {
        throw std::invalid_argument{"Recorder capacity must be greater than zero"};
    }

    return capacity;
}

std::size_t Recorder::checked_storage_bytes(std::size_t capacity) {
    if (capacity > std::numeric_limits<std::size_t>::max() / sizeof(TickSample)) {
        throw std::length_error{"Recorder storage size overflows size_t"};
    }

    return capacity * sizeof(TickSample);
}

void Recorder::touch_storage() noexcept {
    auto* const bytes = reinterpret_cast<volatile std::byte*>(storage_.get());
    auto const stride = page_size();

    for (std::size_t offset = 0; offset < storage_bytes_;) {
        bytes[offset] = std::byte{0};

        if (storage_bytes_ - offset <= stride) {
            break;
        }
        offset += stride;
    }

    bytes[storage_bytes_ - 1] = std::byte{0};
}

} // namespace ddcs::profile
