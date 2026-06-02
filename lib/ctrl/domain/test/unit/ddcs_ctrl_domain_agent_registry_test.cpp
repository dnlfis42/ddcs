#include "ddcs/ctrl/domain/agent/agent_registry.hpp"

#include "ddcs/ctrl/domain/agent/agent_uuid.hpp"

#include <gtest/gtest.h>

#include <array>

#include <cstddef>
#include <cstdint>

namespace {

using namespace ddcs::ctrl::domain::agent;
using ddcs::common::Uuid;

Uuid make_uuid(std::uint8_t seed) {
    std::array<std::byte, 16> b{};
    for (auto& x : b) {
        x = std::byte{seed};
    }
    return Uuid{b};
}

} // namespace

TEST(AgentRegistryTest, CreatesNewAgentForUnknownUuid) {
    AgentRegistry reg;
    auto const u = make_uuid(1);
    Agent const& a = reg.find_or_create(u);
    EXPECT_EQ(a.uuid, u);
    EXPECT_TRUE(a.id.valid());
    EXPECT_EQ(reg.size(), 1u);
}

TEST(AgentRegistryTest, ReturnsSameIdForSameUuid) {
    AgentRegistry reg;
    auto const u = make_uuid(2);
    AgentId const id1 = reg.find_or_create(u).id;
    AgentId const id2 = reg.find_or_create(u).id; // 재등록 -> 동일 id (identity persistence)
    EXPECT_EQ(id1, id2);
    EXPECT_EQ(reg.size(), 1u);
}

TEST(AgentRegistryTest, AssignsDistinctIdsToDistinctUuids) {
    AgentRegistry reg;
    AgentId const a = reg.find_or_create(make_uuid(3)).id;
    AgentId const b = reg.find_or_create(make_uuid(4)).id;
    EXPECT_NE(a, b);
    EXPECT_EQ(reg.size(), 2u);
}

TEST(AgentRegistryTest, FindsByUuid) {
    AgentRegistry reg;
    auto const u = make_uuid(5);
    reg.find_or_create(u);
    Agent const* found = reg.find_by_uuid(u);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->uuid, u);
    EXPECT_EQ(reg.find_by_uuid(make_uuid(6)), nullptr);
}

TEST(AgentRegistryTest, FindsById) {
    AgentRegistry reg;
    AgentId const id = reg.find_or_create(make_uuid(7)).id;
    Agent const* found = reg.find_by_id(id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, id);
    EXPECT_EQ(reg.find_by_id(AgentId{9999}), nullptr);
}

TEST(AgentRegistryTest, SetAttributesUpdatesGroupAndVersion) {
    AgentRegistry reg;
    auto const u = make_uuid(8);
    AgentId const id = reg.find_or_create(u).id;
    reg.set_attributes(id, "sensors", "1.2.3");
    Agent const* a = reg.find_by_uuid(u);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->group, "sensors");
    EXPECT_EQ(a->version, "1.2.3");
}

TEST(AgentRegistryTest, SetAttributesRefreshesOnReRegister) {
    AgentRegistry reg;
    auto const u = make_uuid(9);
    AgentId const id = reg.find_or_create(u).id;
    reg.set_attributes(id, "g1", "v1");
    reg.set_attributes(id, "g2", "v2"); // 재등록 -> 갱신
    Agent const* a = reg.find_by_uuid(u);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->group, "g2");
    EXPECT_EQ(a->version, "v2");
}

TEST(AgentRegistryTest, SetAttributesIgnoresUnknownId) {
    AgentRegistry reg;
    reg.set_attributes(AgentId{4242}, "g", "v"); // 미지 id -> no-op (크래시 없음)
    EXPECT_EQ(reg.size(), 0u);
}
