#pragma once

#include <chrono>

namespace ddcs::common {

class Clock {
public:
    using time_point = std::chrono::steady_clock::time_point;
    using duration = std::chrono::steady_clock::duration;

    Clock() = default;
    virtual ~Clock() = default;

    Clock(Clock const&) = delete;
    Clock& operator=(Clock const&) = delete;
    Clock(Clock&&) = delete;
    Clock& operator=(Clock&&) = delete;

    [[nodiscard]] virtual time_point now() const noexcept = 0;
};

class SteadyClock final : public Clock {
public:
    [[nodiscard]] time_point now() const noexcept override {
        return std::chrono::steady_clock::now();
    }
};

class ManualClock final : public Clock {
public:
    explicit ManualClock(time_point t = time_point{}) noexcept
        : now_(t) {}

    [[nodiscard]] time_point now() const noexcept override {
        return now_;
    }

    void set(time_point t) noexcept {
        now_ = t;
    }

    void advance(duration d) noexcept {
        now_ += d;
    }

private:
    time_point now_;
};

} // namespace ddcs::common
