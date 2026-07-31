#include "ddcs/wire/message/message.hpp"

#include "ddcs/common/endian.hpp"
#include "ddcs/common/uuid.hpp"

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace ddcs::wire::message {

namespace {

template <std::unsigned_integral T>
[[nodiscard]] bool extract_le(std::span<std::byte const>& in, T& value) noexcept {
    if (in.size() < sizeof(T)) {
        return false;
    }

    T le_value{};
    std::memcpy(&le_value, in.data(), sizeof(T));
    value = common::from_le(le_value);
    in = in.subspan(sizeof(T));
    return true;
}

[[nodiscard]] bool extract_f64(std::span<std::byte const>& in, double& value) noexcept {
    std::uint64_t bits{};
    if (!extract_le(in, bits)) {
        return false;
    }

    value = std::bit_cast<double>(bits);
    return true;
}

[[nodiscard]] bool
extract_string(std::span<std::byte const>& in, std::string_view& value) noexcept {
    std::uint16_t length{};
    if (!extract_le(in, length)) {
        return false;
    }
    if (in.size() < length) {
        return false;
    }

    value = std::string_view{
        reinterpret_cast<char const*>(in.data()), static_cast<std::size_t>(length)
    };
    in = in.subspan(length);
    return true;
}

[[nodiscard]] bool extract_type(std::span<std::byte const>& in, MessageType& value) noexcept {
    std::uint8_t raw{};
    if (!extract_le(in, raw)) {
        return false;
    }

    value = static_cast<MessageType>(raw);
    return true;
}

[[nodiscard]] bool extract_uuid(std::span<std::byte const>& in, common::Uuid& value) noexcept {
    std::array<std::byte, 16> bytes{};
    if (in.size() < bytes.size()) {
        return false;
    }

    std::memcpy(bytes.data(), in.data(), bytes.size());
    value = common::Uuid{bytes};
    in = in.subspan(bytes.size());
    return true;
}

template <std::unsigned_integral T>
[[nodiscard]] bool append_le(std::span<std::byte>& out, T value) noexcept {
    if (out.size() < sizeof(T)) {
        return false;
    }

    T const le_value = common::to_le(value);
    std::memcpy(out.data(), &le_value, sizeof(T));
    out = out.subspan(sizeof(T));
    return true;
}

[[nodiscard]] bool append_f64(std::span<std::byte>& out, double value) noexcept {
    return append_le(out, std::bit_cast<std::uint64_t>(value));
}

// string field wire 형식: [len(u16le)][UTF-8 bytes]
[[nodiscard]] bool append_string(std::span<std::byte>& out, std::string_view value) noexcept {
    if (value.size() > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }
    if (!append_le(out, static_cast<std::uint16_t>(value.size()))) {
        return false;
    }
    if (value.empty()) {
        return true;
    }
    if (out.size() < value.size()) {
        return false;
    }

    std::memcpy(out.data(), value.data(), value.size());
    out = out.subspan(value.size());
    return true;
}

[[nodiscard]] bool append_type(std::span<std::byte>& out, MessageType value) noexcept {
    return append_le(out, static_cast<std::uint8_t>(value));
}

[[nodiscard]] bool append_uuid(std::span<std::byte>& out, common::Uuid const& value) noexcept {
    auto const& bytes = value.bytes();
    if (out.size() < bytes.size()) {
        return false;
    }

    std::memcpy(out.data(), bytes.data(), bytes.size());
    out = out.subspan(bytes.size());
    return true;
}

// decode_<X>: in(type 이후 body)을 푼다. 구조 불일치면 nullopt.
// wire 표면에는 decode_message만 노출한다.

[[nodiscard]] std::optional<RegisterRequest>
decode_register_request(std::span<std::byte const> in) noexcept {
    RegisterRequest out{};
    if (!extract_uuid(in, out.uuid)) {
        return std::nullopt;
    }
    if (!extract_string(in, out.group)) {
        return std::nullopt;
    }
    if (!in.empty()) {
        return std::nullopt;
    }
    return out;
}

[[nodiscard]] std::optional<RegisterOutcome>
decode_register_outcome(std::span<std::byte const> in) noexcept {
    std::uint8_t code{};
    if (!extract_le(in, code)) {
        return std::nullopt;
    }
    if (!in.empty()) {
        return std::nullopt;
    }
    return RegisterOutcome{.code = static_cast<RegisterOutcome::Code>(code)};
}

[[nodiscard]] std::optional<RegisterAck>
decode_register_ack(std::span<std::byte const> in) noexcept {
    if (!in.empty()) {
        return std::nullopt;
    }
    return RegisterAck{};
}

[[nodiscard]] std::optional<Heartbeat> decode_heartbeat(std::span<std::byte const> in) noexcept {
    if (!in.empty()) {
        return std::nullopt;
    }
    return Heartbeat{};
}

[[nodiscard]] std::optional<StatusReport> decode_status_report(std::span<std::byte const> in) noexcept {
    StatusReport out{};
    if (!extract_le(in, out.mode)) {
        return std::nullopt;
    }
    if (!extract_f64(in, out.load)) {
        return std::nullopt;
    }
    if (!extract_f64(in, out.temp)) {
        return std::nullopt;
    }
    if (!in.empty()) {
        return std::nullopt;
    }
    return out;
}

[[nodiscard]] std::optional<CommandRequest>
decode_command_request(std::span<std::byte const> in) noexcept {
    CommandRequest out{};
    if (!extract_le(in, out.command_id)) {
        return std::nullopt;
    }
    if (!extract_le(in, out.command_type)) {
        return std::nullopt;
    }

    // payload는 body 나머지 전부다. 입력 span을 차용하며 비어도 유효하다.
    out.payload = in;
    return out;
}

[[nodiscard]] std::optional<CommandAck> decode_command_ack(std::span<std::byte const> in) noexcept {
    CommandAck out{};
    if (!extract_le(in, out.command_id)) {
        return std::nullopt;
    }
    if (!in.empty()) {
        return std::nullopt;
    }
    return out;
}

[[nodiscard]] std::optional<CommandOutcome>
decode_command_outcome(std::span<std::byte const> in) noexcept {
    CommandOutcome out{};
    if (!extract_le(in, out.command_id)) {
        return std::nullopt;
    }
    std::uint8_t code{};
    if (!extract_le(in, code)) {
        return std::nullopt;
    }
    if (!in.empty()) {
        return std::nullopt;
    }
    out.code = static_cast<CommandOutcome::Code>(code);
    return out;
}

// decode_<X> 결과(optional<X>)를 optional<Message>로 올린다
template <typename T>
[[nodiscard]] std::optional<Message> to_message(std::optional<T> const& decoded) noexcept {
    if (!decoded) {
        return std::nullopt;
    }
    return Message{*decoded};
}

} // namespace

MessageType message_type(std::span<std::byte const> in) noexcept {
    MessageType type{MessageType::invalid};
    if (!extract_type(in, type)) {
        return MessageType::invalid;
    }
    return type;
}

// RegisterRequest body: [uuid(16)][group(str)]
std::optional<std::size_t> encode_register_request(
    std::span<std::byte> out, common::Uuid const& uuid, std::string_view group
) noexcept {
    std::span<std::byte> cur = out;
    if (!append_type(cur, MessageType::register_request)) {
        return std::nullopt;
    }
    if (!append_uuid(cur, uuid)) {
        return std::nullopt;
    }
    if (!append_string(cur, group)) {
        return std::nullopt;
    }
    return out.size() - cur.size();
}

// RegisterOutcome body: [code(u8)]
std::optional<std::size_t>
encode_register_outcome(std::span<std::byte> out, RegisterOutcome::Code code) noexcept {
    std::span<std::byte> cur = out;
    if (!append_type(cur, MessageType::register_outcome)) {
        return std::nullopt;
    }
    if (!append_le(cur, static_cast<std::uint8_t>(code))) {
        return std::nullopt;
    }
    return out.size() - cur.size();
}

// RegisterAck body: empty
std::optional<std::size_t> encode_register_ack(std::span<std::byte> out) noexcept {
    std::span<std::byte> cur = out;
    if (!append_type(cur, MessageType::register_ack)) {
        return std::nullopt;
    }
    return out.size() - cur.size();
}

// Heartbeat body: empty
std::optional<std::size_t> encode_heartbeat(std::span<std::byte> out) noexcept {
    std::span<std::byte> cur = out;
    if (!append_type(cur, MessageType::heartbeat)) {
        return std::nullopt;
    }
    return out.size() - cur.size();
}

// StatusReport body: [mode(u8)][load(f64le)][temp(f64le)]
std::optional<std::size_t>
encode_status_report(std::span<std::byte> out, std::uint8_t mode, double load, double temp) noexcept {
    std::span<std::byte> cur = out;
    if (!append_type(cur, MessageType::status_report)) {
        return std::nullopt;
    }
    if (!append_le(cur, mode)) {
        return std::nullopt;
    }
    if (!append_f64(cur, load)) {
        return std::nullopt;
    }
    if (!append_f64(cur, temp)) {
        return std::nullopt;
    }
    return out.size() - cur.size();
}

// CommandRequest body: [command_id(u64le)][command_type(u8)][payload(rest)]
// payload는 호출측이 out 뒤에 직접 append한다. 여기선 header까지만 쓴다.
std::optional<std::size_t> encode_command_request_header(
    std::span<std::byte> out, std::uint64_t command_id, std::uint8_t command_type
) noexcept {
    std::span<std::byte> cur = out;
    if (!append_type(cur, MessageType::command_request)) {
        return std::nullopt;
    }
    if (!append_le(cur, command_id)) {
        return std::nullopt;
    }
    if (!append_le(cur, command_type)) {
        return std::nullopt;
    }
    return out.size() - cur.size();
}

// CommandAck body: [command_id(u64le)]
std::optional<std::size_t>
encode_command_ack(std::span<std::byte> out, std::uint64_t command_id) noexcept {
    std::span<std::byte> cur = out;
    if (!append_type(cur, MessageType::command_ack)) {
        return std::nullopt;
    }
    if (!append_le(cur, command_id)) {
        return std::nullopt;
    }
    return out.size() - cur.size();
}

// CommandOutcome body: [command_id(u64le)][code(u8)]
std::optional<std::size_t> encode_command_outcome(
    std::span<std::byte> out, std::uint64_t command_id, CommandOutcome::Code code
) noexcept {
    std::span<std::byte> cur = out;
    if (!append_type(cur, MessageType::command_outcome)) {
        return std::nullopt;
    }
    if (!append_le(cur, command_id)) {
        return std::nullopt;
    }
    if (!append_le(cur, static_cast<std::uint8_t>(code))) {
        return std::nullopt;
    }
    return out.size() - cur.size();
}

std::optional<Message> decode_message(std::span<std::byte const> in) noexcept {
    if (in.empty()) {
        return std::nullopt; // 메시지는 최소 type 1바이트
    }
    auto const body = in.subspan(1);

    switch (message_type(in)) {
    case MessageType::invalid:
        break;
    case MessageType::register_request:
        return to_message(decode_register_request(body));
    case MessageType::register_outcome:
        return to_message(decode_register_outcome(body));
    case MessageType::register_ack:
        return to_message(decode_register_ack(body));
    case MessageType::heartbeat:
        return to_message(decode_heartbeat(body));
    case MessageType::status_report:
        return to_message(decode_status_report(body));
    case MessageType::command_request:
        return to_message(decode_command_request(body));
    case MessageType::command_ack:
        return to_message(decode_command_ack(body));
    case MessageType::command_outcome:
        return to_message(decode_command_outcome(body));
    }
    return std::nullopt; // 미지 type
}

} // namespace ddcs::wire::message
