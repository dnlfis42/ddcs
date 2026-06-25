#include "ddcs/ctrl/app/session/session.hpp"

#include "ddcs/common/clock.hpp"
#include "ddcs/ctrl/app/transport/port/connection_id.hpp"
#include "ddcs/ctrl/domain/device_id.hpp"

#include <array>
#include <cstddef>

#include <gtest/gtest.h>

namespace {

using ddcs::common::ManualClock;
using ddcs::ctrl::app::session::Session;
using ddcs::ctrl::app::transport::port::ConnectionId;
using ddcs::ctrl::domain::DeviceId;
using namespace std::chrono_literals;

DeviceId make_device_id(std::uint8_t seed) {
    std::array<std::byte, 16> bytes{};
    bytes[0] = std::byte{seed};
    return DeviceId{bytes};
}

TEST(SessionTest, DefaultConstructedIsIdle) {
    Session const session;

    EXPECT_FALSE(session.valid());
    EXPECT_EQ(session.state(), Session::State::idle);
}

TEST(SessionTest, ConstructionStartsHandshaking) {
    ManualClock clock;
    clock.advance(10s);
    Session const session{ConnectionId{7}, clock.now()};

    EXPECT_TRUE(session.valid());
    EXPECT_EQ(session.state(), Session::State::handshaking);
    EXPECT_EQ(session.conn(), ConnectionId{7});
    EXPECT_EQ(session.last_seen(), clock.now());
    EXPECT_FALSE(session.device().valid()); // 등록 전엔 바인딩 없음
}

TEST(SessionTest, BindTransitionsToConfirmingAndUpdatesSeen) {
    ManualClock clock;
    Session session{ConnectionId{7}, clock.now()};

    clock.advance(3s);
    ASSERT_TRUE(session.bind(make_device_id(0xAB), clock.now()));

    EXPECT_EQ(session.state(), Session::State::confirming); // active는 RegisterAck 이후
    EXPECT_EQ(session.device(), make_device_id(0xAB));
    EXPECT_EQ(session.last_seen(), clock.now()); // ack 대기 구간이 새 budget을 받는다
}

TEST(SessionTest, ConfirmTransitionsToActiveAndUpdatesSeen) {
    ManualClock clock;
    Session session{ConnectionId{7}, clock.now()};
    ASSERT_TRUE(session.bind(make_device_id(0xAB), clock.now()));

    clock.advance(2s);
    ASSERT_TRUE(session.confirm(clock.now()));

    EXPECT_EQ(session.state(), Session::State::active);
    EXPECT_EQ(session.last_seen(), clock.now()); // liveness 측정 시작점 = ack 수신
}

TEST(SessionTest, ConfirmRejectsWhenNotConfirming) {
    ManualClock clock;

    Session idle_agent;
    EXPECT_FALSE(idle_agent.confirm(clock.now()));

    Session session{ConnectionId{7}, clock.now()};
    EXPECT_FALSE(session.confirm(clock.now())); // handshaking: bind 전 ack는 무효

    ASSERT_TRUE(session.bind(make_device_id(0x01), clock.now()));
    ASSERT_TRUE(session.confirm(clock.now()));
    EXPECT_FALSE(session.confirm(clock.now())); // active: 중복 ack는 무효
}

TEST(SessionTest, BindRejectsNilDevice) {
    ManualClock clock;
    Session session{ConnectionId{7}, clock.now()};

    EXPECT_FALSE(session.bind(DeviceId{}, clock.now()));
    EXPECT_EQ(session.state(), Session::State::handshaking);
}

TEST(SessionTest, BindRejectsWhenNotHandshaking) {
    ManualClock clock;

    Session idle_agent;
    EXPECT_FALSE(idle_agent.bind(make_device_id(0x01), clock.now()));

    Session session{ConnectionId{7}, clock.now()};
    ASSERT_TRUE(session.bind(make_device_id(0x01), clock.now()));
    EXPECT_FALSE(session.bind(make_device_id(0x02), clock.now())); // 재바인딩 금지
    EXPECT_EQ(session.device(), make_device_id(0x01));
}

TEST(SessionTest, UpdateSeenRefreshesLastSeen) {
    ManualClock clock;
    Session session{ConnectionId{7}, clock.now()};

    clock.advance(5s);
    session.update_seen(clock.now());

    EXPECT_EQ(session.last_seen(), clock.now());
}

} // namespace
