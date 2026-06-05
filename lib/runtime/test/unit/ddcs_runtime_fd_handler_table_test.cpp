#include "ddcs/runtime/detail/fd_handler_table.hpp"

#include "ddcs/runtime/fd_handler.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

using ddcs::runtime::detail::FdHandlerTable;
using ddcs::runtime::FdHandler;

struct DummyHandler : FdHandler {
    void on_fd_event(std::uint32_t) override {}
};

} // namespace

TEST(FdHandlerTableTest, InsertThenResolveReturnsHandler) {
    FdHandlerTable table;
    DummyHandler h;
    auto const tok = table.insert(5, &h);
    EXPECT_EQ(table.resolve(tok), &h);
}

TEST(FdHandlerTableTest, ResolveStaleTokenAfterEraseReturnsNull) {
    FdHandlerTable table;
    DummyHandler h;
    auto const tok = table.insert(5, &h);
    table.erase(5);
    EXPECT_EQ(table.resolve(tok), nullptr);
}

TEST(FdHandlerTableTest, DistinguishesReusedFdByGeneration) {
    FdHandlerTable table;
    DummyHandler a;
    DummyHandler b;
    auto const tok_a = table.insert(5, &a);
    table.erase(5);
    auto const tok_b = table.insert(5, &b);
    EXPECT_NE(tok_a, tok_b);
    EXPECT_EQ(table.resolve(tok_a), nullptr);
    EXPECT_EQ(table.resolve(tok_b), &b);
}

TEST(FdHandlerTableTest, TokenReArmsSameSlot) {
    FdHandlerTable table;
    DummyHandler h;
    auto const tok = table.insert(7, &h);
    EXPECT_EQ(table.token(7), tok);
    EXPECT_EQ(table.resolve(table.token(7)), &h);
}

TEST(FdHandlerTableTest, ResolveUnknownTokenReturnsNull) {
    FdHandlerTable table;
    EXPECT_EQ(table.resolve(0), nullptr);
    DummyHandler h;
    (void)table.insert(3, &h);
    EXPECT_EQ(table.resolve(0xABCDEF), nullptr);
}

TEST(FdHandlerTableTest, EraseIsIdempotent) {
    FdHandlerTable table;
    table.erase(9);
    DummyHandler h;
    auto const tok = table.insert(9, &h);
    table.erase(9);
    table.erase(9);
    EXPECT_EQ(table.resolve(tok), nullptr);
}
