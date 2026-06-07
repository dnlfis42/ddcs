#pragma once

#include <concepts>
#include <cstddef>
#include <functional>

namespace ddcs::common {

template <typename Tag, typename T>
    requires std::default_initializable<T> && std::equality_comparable<T>
class StrongValue {
public:
    constexpr StrongValue() = default;
    constexpr explicit StrongValue(T value) noexcept : value_{value} {}

public:
    constexpr T value() const noexcept { return value_; }
    constexpr bool valid() const noexcept { return value_ != T{}; }

public:
    constexpr void reset() noexcept { value_ = {}; }

public:
    constexpr bool operator==(StrongValue const&) const = default;

private:
    T value_{};
};

} // namespace ddcs::common

template <typename Tag, typename T>
struct std::hash<ddcs::common::StrongValue<Tag, T>> {
    std::size_t operator()(ddcs::common::StrongValue<Tag, T> const& value) const noexcept {
        return std::hash<T>{}(value.value());
    }
};
