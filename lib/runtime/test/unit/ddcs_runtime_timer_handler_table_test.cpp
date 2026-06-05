#include "ddcs/runtime/detail/timer_handler_table.hpp"

#include "ddcs/runtime/timer_handler.hpp"
#include "ddcs/runtime/timer_id.hpp"

#include <gtest/gtest.h>

namespace {

using ddcs::runtime::TimerHandler;
using ddcs::runtime::TimerId;
using ddcs::runtime::detail::TimerHandlerTable;

struct DummyHandler : TimerHandler {
    void on_timer_event(TimerId) override {}
};

} // namespace

TEST(TimerHandlerTableTest, InsertThenResolveReturnsHandler) {
    TimerHandlerTable table;
    DummyHandler h;

    TimerId const id = table.insert(&h);

    EXPECT_TRUE(id.valid());
    EXPECT_EQ(table.resolve(id), &h);
    EXPECT_TRUE(table.contains(id));
}

TEST(TimerHandlerTableTest, ConsumeReturnsHandlerAndInvalidatesId) {
    TimerHandlerTable table;
    DummyHandler h;

    TimerId const id = table.insert(&h);

    EXPECT_EQ(table.consume(id), &h);
    EXPECT_EQ(table.resolve(id), nullptr);
    EXPECT_FALSE(table.contains(id));
}

TEST(TimerHandlerTableTest, EraseInvalidatesId) {
    TimerHandlerTable table;
    DummyHandler h;

    TimerId const id = table.insert(&h);

    EXPECT_TRUE(table.erase(id));
    EXPECT_EQ(table.resolve(id), nullptr);
    EXPECT_FALSE(table.erase(id));
}

TEST(TimerHandlerTableTest, ReusesSlotWithNewGeneration) {
    TimerHandlerTable table;
    DummyHandler a;
    DummyHandler b;

    TimerId const old_id = table.insert(&a);
    EXPECT_TRUE(table.erase(old_id));

    TimerId const new_id = table.insert(&b);

    EXPECT_NE(old_id, new_id);
    EXPECT_EQ(table.resolve(old_id), nullptr);
    EXPECT_EQ(table.resolve(new_id), &b);
}

TEST(TimerHandlerTableTest, UnknownIdDoesNotResolve) {
    TimerHandlerTable table;

    EXPECT_EQ(table.resolve(TimerId{}), nullptr);
    EXPECT_EQ(table.resolve(TimerId{999}), nullptr);
    EXPECT_FALSE(table.erase(TimerId{999}));
    EXPECT_EQ(table.consume(TimerId{999}), nullptr);
}
