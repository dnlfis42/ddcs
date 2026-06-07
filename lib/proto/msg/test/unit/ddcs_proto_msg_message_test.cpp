#include "ddcs/proto/msg/message.hpp"

#include "ddcs/proto/msg/type.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string>

#include <cstddef>
#include <cstdint>

namespace {

using namespace ddcs::proto::msg;
using ddcs::common::LinearBuffer;
using ddcs::common::Uuid;

constexpr std::size_t buf_capacity{1024};

template <typename T>
bool roundtrip(T const& in, T& out) {
    LinearBuffer buf{buf_capacity};
    if (!encode(in, buf)) {
        return false;
    }
    return decode(buf.readable(), out);
}

} // namespace

TEST(MessageCodecTest, RoundTripsRegisterRequest) {
    std::array<std::byte, 16> tag{};
    for (std::size_t i = 0; i < tag.size(); ++i) {
        tag[i] = std::byte{static_cast<std::uint8_t>(i + 1)};
    }
    RegisterRequest const in{.agent_uuid = Uuid{tag}, .group = "sensors", .version = "1.2.3"};
    RegisterRequest out{};
    ASSERT_TRUE(roundtrip(in, out));
    EXPECT_EQ(in, out);
}

TEST(MessageCodecTest, RoundTripsRegisterRequestEmptyAttributes) {
    RegisterRequest const in{.agent_uuid = Uuid{}, .group = "", .version = ""};
    RegisterRequest out{};
    ASSERT_TRUE(roundtrip(in, out));
    EXPECT_EQ(in, out);
}

TEST(MessageCodecTest, RoundTripsRegisterResponseEmptyReason) {
    RegisterResponse const in{.result = RegisterResult::success, .reason = ""};
    RegisterResponse out{};
    ASSERT_TRUE(roundtrip(in, out));
    EXPECT_EQ(in, out);
}

TEST(MessageCodecTest, RoundTripsRegisterResponseWithReason) {
    RegisterResponse const in{.result = RegisterResult::failed, .reason = "kicked by new agent"};
    RegisterResponse out{};
    ASSERT_TRUE(roundtrip(in, out));
    EXPECT_EQ(in, out);
}

TEST(MessageCodecTest, RoundTripsHeartbeat) {
    Heartbeat const in{.timestamp_ms = 1'700'000'000'123ULL};
    Heartbeat out{};
    ASSERT_TRUE(roundtrip(in, out));
    EXPECT_EQ(in, out);
}

TEST(MessageCodecTest, RoundTripsStatusWithJson) {
    Status const in{.timestamp_ms = 1'700'000'000'456ULL, .status_json = R"({"mode":"performance"})"};
    Status out{};
    ASSERT_TRUE(roundtrip(in, out));
    EXPECT_EQ(in, out);
}

TEST(MessageCodecTest, RoundTripsStatusEmptyJson) {
    Status const in{.timestamp_ms = 1, .status_json = ""};
    Status out{};
    ASSERT_TRUE(roundtrip(in, out));
    EXPECT_EQ(in, out);
}

TEST(MessageCodecTest, RoundTripsCommandWithPayload) {
    Command const in{.command_id = 99, .type = 0x01, .payload = std::string{"\x02", 1}};
    Command out{};
    ASSERT_TRUE(roundtrip(in, out));
    EXPECT_EQ(in, out);
}

TEST(MessageCodecTest, RoundTripsCommandEmptyPayload) {
    Command const in{.command_id = 1, .type = 0x01, .payload = ""};
    Command out{};
    ASSERT_TRUE(roundtrip(in, out));
    EXPECT_EQ(in, out);
}

TEST(MessageCodecTest, RoundTripsCommandAck) {
    CommandAck const in{.command_id = 7};
    CommandAck out{};
    ASSERT_TRUE(roundtrip(in, out));
    EXPECT_EQ(in, out);
}

TEST(MessageCodecTest, RoundTripsCommandOutcome) {
    CommandOutcome const in{.command_id = 12, .result = CommandResult::failed, .reason = "device offline"};
    CommandOutcome out{};
    ASSERT_TRUE(roundtrip(in, out));
    EXPECT_EQ(in, out);
}

TEST(MessageCodecTest, RejectsTrailingBytes) {
    LinearBuffer buf{buf_capacity};
    Heartbeat const in{.timestamp_ms = 42};
    ASSERT_TRUE(encode(in, buf));
    std::byte const extra{0xAB};
    ASSERT_TRUE(buf.write({&extra, 1}));
    Heartbeat out{};
    EXPECT_FALSE(decode(buf.readable(), out));
}

TEST(MessageCodecTest, RejectsInsufficientBytes) {
    LinearBuffer buf{buf_capacity};
    std::array<std::byte, 4> too_short{}; // u64 도 안 됨
    ASSERT_TRUE(buf.write({too_short.data(), too_short.size()}));
    Heartbeat out{};
    EXPECT_FALSE(decode(buf.readable(), out));
}

TEST(MessageCodecTest, MapsTypeOfForEachMessage) {
    EXPECT_EQ(type_of<RegisterRequest>, MessageType::register_request);
    EXPECT_EQ(type_of<RegisterResponse>, MessageType::register_response);
    EXPECT_EQ(type_of<Heartbeat>, MessageType::heartbeat);
    EXPECT_EQ(type_of<Status>, MessageType::status);
    EXPECT_EQ(type_of<Command>, MessageType::command);
    EXPECT_EQ(type_of<CommandAck>, MessageType::command_ack);
    EXPECT_EQ(type_of<CommandOutcome>, MessageType::command_outcome);
}
