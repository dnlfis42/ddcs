#include "ddcs/runtime/fd_dispatch_table.hpp"

#include "ddcs/runtime/fd_handler.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

using ddcs::runtime::FdDispatchTable;
using ddcs::runtime::FdHandler;

struct DummyHandler : FdHandler {
    void on_io(std::uint32_t /*events*/) override {}
};

} // namespace

TEST(FdDispatchTableTest, InsertThenResolveReturnsHandler) {
    FdDispatchTable table;
    DummyHandler h;
    auto const tok = table.insert(5, &h);
    EXPECT_EQ(table.resolve(tok), &h);
}

TEST(FdDispatchTableTest, ResolveStaleTokenAfterEraseReturnsNull) {
    FdDispatchTable table;
    DummyHandler h;
    auto const tok = table.insert(5, &h);
    table.erase(5);
    EXPECT_EQ(table.resolve(tok), nullptr);
}

TEST(FdDispatchTableTest, DistinguishesReusedFdByGeneration) {
    FdDispatchTable table;
    DummyHandler a;
    DummyHandler b;
    auto const tok_a = table.insert(5, &a);
    table.erase(5);
    auto const tok_b = table.insert(5, &b); // 같은 fd 재사용
    EXPECT_NE(tok_a, tok_b);
    EXPECT_EQ(table.resolve(tok_a), nullptr); // 옛 incarnation 은 무효
    EXPECT_EQ(table.resolve(tok_b), &b);
}

TEST(FdDispatchTableTest, TokenReArmsSameSlot) {
    FdDispatchTable table;
    DummyHandler h;
    auto const tok = table.insert(7, &h);
    EXPECT_EQ(table.token(7), tok); // MOD 재무장 토큰은 동일
    EXPECT_EQ(table.resolve(table.token(7)), &h);
}

TEST(FdDispatchTableTest, ResolveUnknownTokenReturnsNull) {
    FdDispatchTable table;
    EXPECT_EQ(table.resolve(0), nullptr); // 빈 테이블 - 범위 밖
    DummyHandler h;
    table.insert(3, &h);
    EXPECT_EQ(table.resolve(0xABCDEF), nullptr); // 미발급 토큰(범위 밖 fd)
}

TEST(FdDispatchTableTest, EraseIsIdempotent) {
    FdDispatchTable table;
    table.erase(9); // 미등록
    DummyHandler h;
    auto const tok = table.insert(9, &h);
    table.erase(9);
    table.erase(9); // 두 번
    EXPECT_EQ(table.resolve(tok), nullptr);
}
