#include "ddcs/common/uuid.hpp"

#include <gtest/gtest.h>

#include <array>
#include <type_traits>
#include <unordered_map>

#include <cstddef>

namespace ddcs::common {

static_assert(std::is_trivially_copyable_v<Uuid>);

TEST(Uuid, NilDefault) {
    Uuid u;
    for (auto b : u.bytes()) {
        EXPECT_EQ(b, std::byte{0});
    }
    EXPECT_EQ(u.to_string(), "00000000-0000-0000-0000-000000000000");
}

TEST(Uuid, BytesToString) {
    std::array<std::byte, 16> const bytes{
        std::byte{0x55}, std::byte{0x0e}, std::byte{0x84}, std::byte{0x00}, std::byte{0xe2}, std::byte{0x9b},
        std::byte{0x41}, std::byte{0xd4}, std::byte{0xa7}, std::byte{0x16}, std::byte{0x44}, std::byte{0x66},
        std::byte{0x55}, std::byte{0x44}, std::byte{0x00}, std::byte{0x00},
    };
    Uuid const u{bytes};
    EXPECT_EQ(u.to_string(), "550e8400-e29b-41d4-a716-446655440000");
}

TEST(Uuid, Comparison) {
    std::array<std::byte, 16> a{};
    std::array<std::byte, 16> b{};
    b[0] = std::byte{0x01};

    Uuid const u1{a};
    Uuid const u2{b};
    Uuid const u3{a};

    EXPECT_TRUE(u1 == u3);
    EXPECT_FALSE(u1 == u2);
    EXPECT_TRUE(u1 < u2);
    EXPECT_TRUE(u2 > u1);
}

TEST(Uuid, Hash) {
    std::array<std::byte, 16> a{};
    a[0] = std::byte{0x01};
    std::array<std::byte, 16> b{};
    b[0] = std::byte{0x02};

    Uuid const u1{a};
    Uuid const u2{a};
    Uuid const u3{b};

    EXPECT_EQ(std::hash<Uuid>{}(u1), std::hash<Uuid>{}(u2));

    std::unordered_map<Uuid, int> m;
    m[u1] = 1;
    m[u3] = 2;
    EXPECT_EQ(m.size(), 2u);
    EXPECT_EQ(m[u1], 1);
    EXPECT_EQ(m[u3], 2);
}

} // namespace ddcs::common
