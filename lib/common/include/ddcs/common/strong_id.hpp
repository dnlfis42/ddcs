#pragma once

#include <concepts>
#include <functional>

#include <cstddef>

namespace ddcs::common {

template <typename Tag, typename T>
    requires std::default_initializable<T> && std::equality_comparable<T>
class StrongId {
public:
    constexpr StrongId() = default;
    constexpr explicit StrongId(T v) noexcept : value_{v} {}

    constexpr T value() const noexcept { return value_; }
    constexpr bool valid() const noexcept { return value_ != T{}; }

    constexpr void reset() noexcept { value_ = {}; }

    constexpr bool operator==(StrongId const&) const = default;

private:
    T value_{};
};

} // namespace ddcs::common

template <typename Tag, typename T>
struct std::hash<ddcs::common::StrongId<Tag, T>> {
    std::size_t operator()(ddcs::common::StrongId<Tag, T> const& id) const noexcept {
        return std::hash<T>{}(id.value());
    }
};
