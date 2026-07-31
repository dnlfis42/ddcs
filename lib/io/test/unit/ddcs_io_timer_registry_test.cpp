#include "ddcs/io/detail/timer_registry.hpp"

#include "ddcs/io/timer_handler.hpp"
#include "ddcs/io/timer_token.hpp"

#include <gtest/gtest.h>

namespace {

using ddcs::io::TimerHandler;
using ddcs::io::TimerToken;
using ddcs::io::detail::TimerRegistry;

class DummyHandler : public TimerHandler {
public:
    void on_expired(TimerToken) override {}
};

TEST(TimerRegistryTest, ResolvesInsertedHandler) {
    TimerRegistry table;
    DummyHandler handler;

    TimerToken const id = table.insert(handler);

    EXPECT_TRUE(id.valid());
    EXPECT_EQ(table.resolve(id), &handler);
    EXPECT_TRUE(table.contains(id));
}

TEST(TimerRegistryTest, ConsumesHandlerAndInvalidatesId) {
    TimerRegistry table;
    DummyHandler handler;

    TimerToken const id = table.insert(handler);

    EXPECT_EQ(table.consume(id), &handler);
    EXPECT_EQ(table.resolve(id), nullptr);
    EXPECT_FALSE(table.contains(id));
    EXPECT_EQ(table.consume(id), nullptr);
}

TEST(TimerRegistryTest, InvalidatesIdWhenErased) {
    TimerRegistry table;
    DummyHandler handler;

    TimerToken const id = table.insert(handler);

    EXPECT_TRUE(table.erase(id));
    EXPECT_EQ(table.resolve(id), nullptr);
    EXPECT_FALSE(table.contains(id));
    EXPECT_FALSE(table.erase(id));
}

TEST(TimerRegistryTest, ReusesSlotWithNewGeneration) {
    TimerRegistry table;
    DummyHandler first_handler;
    DummyHandler second_handler;

    TimerToken const old_id = table.insert(first_handler);
    EXPECT_TRUE(table.erase(old_id));

    TimerToken const new_id = table.insert(second_handler);

    EXPECT_NE(old_id, new_id);
    EXPECT_EQ(table.resolve(old_id), nullptr);
    EXPECT_EQ(table.resolve(new_id), &second_handler);
}

TEST(TimerRegistryTest, RejectsUnknownId) {
    TimerRegistry table;
    TimerToken const unknown_id{999};

    EXPECT_EQ(table.resolve(TimerToken{}), nullptr);
    EXPECT_FALSE(table.contains(TimerToken{}));
    EXPECT_EQ(table.resolve(unknown_id), nullptr);
    EXPECT_FALSE(table.contains(unknown_id));
    EXPECT_FALSE(table.erase(unknown_id));
    EXPECT_EQ(table.consume(unknown_id), nullptr);
}

} // namespace
