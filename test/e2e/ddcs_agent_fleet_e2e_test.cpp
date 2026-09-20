#include "ddcs/agent/agent_fleet.hpp"
#include "ddcs/agent/domain/dummy_device.hpp"
#include "ddcs/ctrl/controller.hpp"
#include "ddcs/logger/log.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <vector>

#include <gtest/gtest.h>

namespace {

using namespace std::chrono_literals;

class NullSink : public ddcs::logger::Sink {
public:
    void write(std::string_view) noexcept override {}
};

class AgentFleetE2eTest : public testing::Test {
protected:
    void SetUp() override {
        // 테스트 종료 후에도 전역 logger에 유효한 sink 참조를 남긴다.
        static NullSink sink;
        ddcs::logger::Logger::instance().set_sink(sink);
    }

    static ddcs::ctrl::Controller::Config controller_config() {
        ddcs::ctrl::Controller::Config cfg;
        cfg.sweep_interval = 5ms;
        cfg.liveness_timeout = 300ms;
        cfg.policy_path = DDCS_TEST_CONTROLLER_POLICY_PATH;

        return cfg;
    }

    static ddcs::agent::AgentFleet::Config fleet_config(std::uint16_t port) {
        ddcs::agent::AgentFleet::Config cfg;
        cfg.agent.controller_port = port;
        cfg.agent.session.group = "zone_a";
        cfg.agent.session.heartbeat = 20ms;
        cfg.agent.session.status_report = 20ms;
        cfg.agent.session.register_timeout = 500ms;
        cfg.agent.reconnect_base_delay = 10ms;
        cfg.agent.reconnect_max_delay = 30ms;

        return cfg;
    }

    std::vector<std::unique_ptr<ddcs::agent::domain::Device>> devices(std::size_t count) {
        std::vector<std::unique_ptr<ddcs::agent::domain::Device>> result;

        for (std::size_t i = 0; i < count; ++i) {
            auto device = std::make_unique<ddcs::agent::domain::DummyDevice>(
                ddcs::common::Uuid::random(), ddcs::device::Mode::normal
            );
            device->set_load(90.0);
            device->set_temp(90.0); // zone_a 정책의 hot 전환 -> safe 명령
            observed_.push_back(device.get());
            result.push_back(std::move(device));
        }

        return result;
    }

    template <typename Predicate>
    static bool
    pump(ddcs::ctrl::Controller& controller, ddcs::agent::AgentFleet& fleet, Predicate predicate) {
        auto const deadline = std::chrono::steady_clock::now() + 5s;

        while (std::chrono::steady_clock::now() < deadline) {
            controller.run_once(1ms);
            fleet.run_once(1ms);

            if (predicate()) {
                return true;
            }
        }

        return predicate();
    }

    std::vector<ddcs::agent::domain::DummyDevice*> observed_;
};

TEST_F(AgentFleetE2eTest, AllDevicesRegisterReportAndReceivePolicyCommands) {
    ddcs::ctrl::Controller controller{controller_config()};
    controller.start();

    constexpr std::size_t count = 32;
    ddcs::agent::AgentFleet fleet{fleet_config(controller.port()), devices(count)};

    for (std::size_t i = 1; i < count; i += 2) {
        observed_[i]->set_temp(40.0); // 같은 Group에서도 과열 보호는 Device별로 다르다.
    }

    fleet.start();
    fleet.start(); // start 멱등

    ASSERT_TRUE(pump(controller, fleet, [&] {
        if (fleet.active_count() != count) {
            return false;
        }

        for (std::size_t i = 0; i < count; ++i) {
            auto const expected =
                i % 2 == 0 ? ddcs::device::Mode::safe : ddcs::device::Mode::performance;
            if (observed_[i]->mode() != expected) {
                return false;
            }
        }

        return true;
    }));

    // liveness 시한을 지나도 Heartbeat가 모든 세션을 유지한다.
    auto const until = std::chrono::steady_clock::now() + 700ms;

    ASSERT_TRUE(pump(controller, fleet, [&] { return std::chrono::steady_clock::now() >= until; }));
    EXPECT_EQ(fleet.active_count(), count);
    EXPECT_EQ(fleet.size(), count);

    fleet.stop();
    fleet.stop();

    EXPECT_EQ(fleet.active_count(), 0u);
    EXPECT_THROW(fleet.start(), std::logic_error);
}

TEST_F(AgentFleetE2eTest, ReconnectsAfterControllerRestartWithDeviceStatePreserved) {
    auto cfg = controller_config();
    auto controller = std::make_unique<ddcs::ctrl::Controller>(cfg);
    controller->start();
    cfg.listen_port = controller->port();

    ddcs::agent::AgentFleet fleet{fleet_config(controller->port()), devices(8)};
    fleet.start();

    ASSERT_TRUE(pump(*controller, fleet, [&] { return fleet.active_count() == 8; }));

    std::vector<ddcs::common::Uuid> ids;
    for (auto* device : observed_) {
        ids.push_back(device->id());
    }

    controller.reset();

    auto const deadline = std::chrono::steady_clock::now() + 2s;
    while (fleet.active_count() != 0 && std::chrono::steady_clock::now() < deadline) {
        fleet.run_once(1ms);
    }

    ASSERT_EQ(fleet.active_count(), 0u);

    controller = std::make_unique<ddcs::ctrl::Controller>(cfg);
    controller->start();

    ASSERT_TRUE(pump(*controller, fleet, [&] { return fleet.active_count() == 8; }));

    for (std::size_t i = 0; i < ids.size(); ++i) {
        EXPECT_EQ(observed_[i]->id(), ids[i]);
    }

    controller->stop();
    fleet.stop();
}

TEST_F(AgentFleetE2eTest, StopCancelsStaggeredStartupAndClosesConnections) {
    ddcs::ctrl::Controller controller{controller_config()};
    controller.start();

    auto cfg = fleet_config(controller.port());
    cfg.start_interval = 1s;

    ddcs::agent::AgentFleet fleet{cfg, devices(8)};
    fleet.start();

    ASSERT_TRUE(pump(controller, fleet, [&] { return fleet.active_count() == 1; }));

    fleet.stop();
    fleet.run_once(0ms);

    EXPECT_EQ(fleet.active_count(), 0u);
}

TEST_F(AgentFleetE2eTest, RejectsEmptyNullNilAndDuplicateDevices) {
    auto cfg = fleet_config(8080);

    EXPECT_THROW((ddcs::agent::AgentFleet{cfg, {}}), std::invalid_argument);

    std::vector<std::unique_ptr<ddcs::agent::domain::Device>> nulls(1);

    EXPECT_THROW((ddcs::agent::AgentFleet{cfg, std::move(nulls)}), std::invalid_argument);

    std::vector<std::unique_ptr<ddcs::agent::domain::Device>> nil;
    nil.push_back(std::make_unique<ddcs::agent::domain::DummyDevice>());

    EXPECT_THROW((ddcs::agent::AgentFleet{cfg, std::move(nil)}), std::invalid_argument);

    auto duplicates = devices(1);
    duplicates.push_back(std::make_unique<ddcs::agent::domain::DummyDevice>(duplicates[0]->id()));

    EXPECT_THROW((ddcs::agent::AgentFleet{cfg, std::move(duplicates)}), std::invalid_argument);
}

} // namespace
