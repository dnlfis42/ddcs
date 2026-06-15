#include "ddcs/agent/app/port/inbound.hpp"
#include "ddcs/agent/app/port/outbound.hpp"
#include "ddcs/agent/app/port/timer_id.hpp"

#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"

#include <gtest/gtest.h>

#include <chrono>

#include <cstdint>

namespace {

using ddcs::agent::app::port::Inbound;
using ddcs::agent::app::port::Outbound;
using ddcs::agent::app::port::TimerId;
using ddcs::common::LinearBuffer;
using ddcs::common::PoolHandle;

// 인터페이스가 구현 가능한지(순수가상 누락/시그니처 정합)를 컴파일로 검증.
class NoopInbound : public Inbound {
public:
    void on_connected() override {}
    void on_recv(PoolHandle<LinearBuffer>) override {}
    void on_disconnected() override {}
    void on_timer(TimerId) override {}
};

class NoopOutbound : public Outbound {
public:
    PoolHandle<LinearBuffer> payload_buffer() override { return {}; }
    void send(PoolHandle<LinearBuffer>) override {}
    void schedule_timer(TimerId, std::chrono::nanoseconds) override {}
    void cancel_timer(TimerId) override {}
    void close() override {}
};

TEST(AgentPortTest, InterfacesAreImplementable) {
    NoopInbound in;
    NoopOutbound out;
    Inbound* i = &in;
    Outbound* o = &out;
    EXPECT_NE(i, nullptr);
    EXPECT_NE(o, nullptr);
}

TEST(AgentPortTest, TimerIdHasThreeSlots) {
    EXPECT_EQ(static_cast<std::uint8_t>(TimerId::register_timeout), 0);
    EXPECT_EQ(static_cast<std::uint8_t>(TimerId::heartbeat), 1);
    EXPECT_EQ(static_cast<std::uint8_t>(TimerId::status), 2);
}

} // namespace
