#pragma once

#include <functional>
#include <type_traits>

#include <cstddef>

namespace ddcs::common {

template <typename Tag, typename T>
    requires std::is_default_constructible_v<T>
class StrongId {
public:
    StrongId() = default;
    explicit StrongId(T v) noexcept : value_{v} {}

    T value() const noexcept { return value_; }
    bool valid() const noexcept { return value_ != T{}; }
    bool operator==(StrongId const&) const = default;
    void reset() { value_ = {}; }

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
