#pragma once

#include <concepts>
#include <cstddef>
#include <functional>

namespace ddcs::common {

template <typename Tag, typename T>
    requires std::default_initializable<T> && std::equality_comparable<T>
class StrongId {
public:
    static constexpr T invalid{};

    constexpr StrongId() = default;
    constexpr explicit StrongId(T value) noexcept
        : value_(value) {}

    [[nodiscard]] constexpr T get() const noexcept {
        return value_;
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return value_ != invalid;
    }

    constexpr void clear() noexcept {
        value_ = invalid;
    }

    constexpr bool operator==(StrongId const&) const = default;

private:
    T value_ = invalid;
};

} // namespace ddcs::common

template <typename Tag, typename T>
struct std::hash<ddcs::common::StrongId<Tag, T>> {
    std::size_t operator()(ddcs::common::StrongId<Tag, T> const& value) const noexcept {
        return std::hash<T>{}(value.get());
    }
};
