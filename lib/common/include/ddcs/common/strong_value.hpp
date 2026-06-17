#pragma once

#include <concepts>
#include <cstddef>
#include <functional>

namespace ddcs::common {

template <typename Tag, typename T>
    requires std::default_initializable<T> && std::equality_comparable<T>
class StrongValue {
public:
    static constexpr T invalid{};

    constexpr StrongValue() = default;
    constexpr explicit StrongValue(T value) noexcept
        : value_(value) {}

    [[nodiscard]] constexpr T value() const noexcept {
        return value_;
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value_ != invalid;
    }

    constexpr void clear() noexcept {
        value_ = invalid;
    }

    constexpr bool operator==(StrongValue const&) const = default;

private:
    T value_ = invalid;
};

} // namespace ddcs::common

template <typename Tag, typename T>
struct std::hash<ddcs::common::StrongValue<Tag, T>> {
    std::size_t operator()(ddcs::common::StrongValue<Tag, T> const& value) const noexcept {
        return std::hash<T>{}(value.value());
    }
};
