#include "ddcs/io/detail/timer_registration_table.hpp"

#include "ddcs/io/timer_handler.hpp"
#include "ddcs/io/timer_id.hpp"

#include <gtest/gtest.h>

namespace {

using ddcs::io::TimerHandler;
using ddcs::io::TimerId;
using ddcs::io::detail::TimerRegistrationTable;

class DummyHandler : public TimerHandler {
public:
    void on_expired(TimerId) override {}
};

} // namespace

TEST(TimerRegistrationTableTest, ResolvesInsertedHandler) {
    TimerRegistrationTable table;
    DummyHandler handler;

    TimerId const id = table.insert(handler);

    EXPECT_TRUE(id.valid());
    EXPECT_EQ(table.resolve(id), &handler);
    EXPECT_TRUE(table.contains(id));
}

TEST(TimerRegistrationTableTest, ConsumesHandlerAndInvalidatesId) {
    TimerRegistrationTable table;
    DummyHandler handler;

    TimerId const id = table.insert(handler);

    EXPECT_EQ(table.consume(id), &handler);
    EXPECT_EQ(table.resolve(id), nullptr);
    EXPECT_FALSE(table.contains(id));
    EXPECT_EQ(table.consume(id), nullptr);
}

TEST(TimerRegistrationTableTest, InvalidatesIdWhenErased) {
    TimerRegistrationTable table;
    DummyHandler handler;

    TimerId const id = table.insert(handler);

    EXPECT_TRUE(table.erase(id));
    EXPECT_EQ(table.resolve(id), nullptr);
    EXPECT_FALSE(table.contains(id));
    EXPECT_FALSE(table.erase(id));
}

TEST(TimerRegistrationTableTest, ReusesSlotWithNewGeneration) {
    TimerRegistrationTable table;
    DummyHandler first_handler;
    DummyHandler second_handler;

    TimerId const old_id = table.insert(first_handler);
    EXPECT_TRUE(table.erase(old_id));

    TimerId const new_id = table.insert(second_handler);

    EXPECT_NE(old_id, new_id);
    EXPECT_EQ(table.resolve(old_id), nullptr);
    EXPECT_EQ(table.resolve(new_id), &second_handler);
}

TEST(TimerRegistrationTableTest, RejectsUnknownId) {
    TimerRegistrationTable table;
    TimerId const unknown_id{999};

    EXPECT_EQ(table.resolve(TimerId{}), nullptr);
    EXPECT_FALSE(table.contains(TimerId{}));
    EXPECT_EQ(table.resolve(unknown_id), nullptr);
    EXPECT_FALSE(table.contains(unknown_id));
    EXPECT_FALSE(table.erase(unknown_id));
    EXPECT_EQ(table.consume(unknown_id), nullptr);
}
