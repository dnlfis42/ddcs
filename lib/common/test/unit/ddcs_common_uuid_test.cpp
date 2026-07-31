#include "ddcs/common/uuid.hpp"

#include <array>
#include <cstddef>
#include <type_traits>
#include <unordered_map>

#include <gtest/gtest.h>

namespace {

using ddcs::common::Uuid;

static_assert(std::is_trivially_copyable_v<Uuid>);
static_assert(sizeof(Uuid) == 16);
static_assert(Uuid{}.bytes() == Uuid::nil);
static_assert(!Uuid{}.valid());
static_assert(Uuid{} == Uuid{});

consteval bool clear_makes_uuid_invalid() {
    std::array<std::byte, 16> bytes{};
    bytes[0] = std::byte{0x01};

    Uuid uuid{bytes};
    uuid.clear();
    return uuid.bytes() == Uuid::nil && !uuid.valid();
}

static_assert(clear_makes_uuid_invalid());

TEST(UuidTest, StartsInvalid) {
    Uuid u;

    for (auto b : u.bytes()) {
        EXPECT_EQ(b, std::byte{0});
    }

    EXPECT_EQ(u.to_string(), "00000000-0000-0000-0000-000000000000");
    EXPECT_FALSE(u.valid());
}

TEST(UuidTest, ClearsValueToInvalid) {
    std::array<std::byte, 16> bytes{};
    bytes[0] = std::byte{0x01};
    Uuid uuid{bytes};

    uuid.clear();

    EXPECT_EQ(uuid.bytes(), Uuid::nil);
    EXPECT_FALSE(uuid.valid());
}

TEST(UuidTest, FormatsBytesAsCanonicalString) {
    std::array<std::byte, 16> const bytes{
        std::byte{0x55}, std::byte{0x0e}, std::byte{0x84}, std::byte{0x00},
        std::byte{0xe2}, std::byte{0x9b}, std::byte{0x41}, std::byte{0xd4},
        std::byte{0xa7}, std::byte{0x16}, std::byte{0x44}, std::byte{0x66},
        std::byte{0x55}, std::byte{0x44}, std::byte{0x00}, std::byte{0x00},
    };
    Uuid const u{bytes};

    EXPECT_TRUE(u.valid());
    EXPECT_EQ(u.to_string(), "550e8400-e29b-41d4-a716-446655440000");
}

TEST(UuidTest, ComparesLexicographically) {
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

TEST(UuidTest, HashesEqualValuesEqually) {
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

TEST(UuidTest, ParsesCanonicalAndHex32Forms) {
    auto const canonical = Uuid::parse("0feef128-d17f-1f55-8565-9a23ddb8c29d");
    ASSERT_TRUE(canonical.has_value());
    EXPECT_EQ(canonical->to_string(), "0feef128-d17f-1f55-8565-9a23ddb8c29d");

    auto const hex32 = Uuid::parse("0feef128d17f1f5585659a23ddb8c29d");
    ASSERT_TRUE(hex32.has_value());
    EXPECT_EQ(*hex32, *canonical);
}

TEST(UuidTest, ParsesNilAsNil) {
    auto const nil = Uuid::parse("00000000-0000-0000-0000-000000000000");
    ASSERT_TRUE(nil.has_value());
    EXPECT_TRUE(nil->is_nil());
}

TEST(UuidTest, RejectsMalformedText) {
    EXPECT_FALSE(Uuid::parse("").has_value());
    EXPECT_FALSE(Uuid::parse("0feef128").has_value());                             // 길이
    EXPECT_FALSE(Uuid::parse("0feef128d17f1f5585659a23ddb8c29z").has_value());     // 비hex
    EXPECT_FALSE(Uuid::parse("0feef128xd17f-1f55-8565-9a23ddb8c29d").has_value()); // dash 위치
    EXPECT_FALSE(Uuid::parse("0feef128-d17f-1f55-8565-9a23ddb8c2-d").has_value()); // hex 부족
}

TEST(UuidTest, GeneratesDistinctRandomUuids) {
    auto const a = Uuid::random();
    auto const b = Uuid::random();
    EXPECT_TRUE(a.valid());
    EXPECT_NE(a, b); // 128bit 충돌 확률은 무시 가능
}

} // namespace
