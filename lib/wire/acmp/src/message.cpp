#include "ddcs/wire/acmp/message.hpp"

#include "ddcs/common/endian.hpp"
#include "ddcs/common/uuid.hpp"

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>

namespace ddcs::wire::acmp {

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

[[nodiscard]] bool extract_string(std::span<std::byte const>& in, std::string_view& value) noexcept {
    std::uint16_t length{};
    if (!extract_le(in, length)) {
        return false;
    }
    if (in.size() < length) {
        return false;
    }
    value = std::string_view{reinterpret_cast<char const*>(in.data()), static_cast<std::size_t>(length)};
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
[[nodiscard]] bool append_le(T value, std::span<std::byte>& out) noexcept {
    if (out.size() < sizeof(T)) {
        return false;
    }
    T const le_value = common::to_le(value);
    std::memcpy(out.data(), &le_value, sizeof(T));
    out = out.subspan(sizeof(T));
    return true;
}

[[nodiscard]] bool append_f64(double value, std::span<std::byte>& out) noexcept {
    return append_le(std::bit_cast<std::uint64_t>(value), out);
}

// string field wire 형식: [len(u16le)][UTF-8 bytes]
[[nodiscard]] bool append_string(std::string_view value, std::span<std::byte>& out) noexcept {
    if (value.size() > max_string_field_size) {
        return false;
    }
    if (!append_le(static_cast<std::uint16_t>(value.size()), out)) {
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

[[nodiscard]] bool append_type(MessageType value, std::span<std::byte>& out) noexcept {
    return append_le(static_cast<std::uint8_t>(value), out);
}

[[nodiscard]] bool append_uuid(common::Uuid const& value, std::span<std::byte>& out) noexcept {
    auto const& bytes = value.bytes();
    if (out.size() < bytes.size()) {
        return false;
    }

    std::memcpy(out.data(), bytes.data(), bytes.size());
    out = out.subspan(bytes.size());
    return true;
}

} // namespace

MessageType peek_type(std::span<std::byte const> in) noexcept {
    MessageType type{MessageType::invalid};
    if (!extract_type(in, type)) {
        return MessageType::invalid;
    }
    return type;
}

// RegisterRequest body: [id: uuid(16)][group: str]
std::optional<std::size_t>
encode_register_request(common::Uuid const& id, std::string_view group, std::span<std::byte> out) noexcept {
    std::span<std::byte> cur = out;
    if (!append_type(MessageType::register_request, cur)) {
        return std::nullopt;
    }
    if (!append_uuid(id, cur)) {
        return std::nullopt;
    }
    if (!append_string(group, cur)) {
        return std::nullopt;
    }
    return out.size() - cur.size();
}
std::optional<RegisterRequest> decode_register_request(std::span<std::byte const> in) noexcept {
    RegisterRequest out{};
    if (!extract_uuid(in, out.id)) {
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

// RegisterOutcome body: [code(u8)]
std::optional<std::size_t> encode_register_outcome(std::uint8_t code, std::span<std::byte> out) noexcept {
    std::span<std::byte> cur = out;
    if (!append_type(MessageType::register_outcome, cur)) {
        return std::nullopt;
    }
    if (!append_le(code, cur)) {
        return std::nullopt;
    }
    return out.size() - cur.size();
}
std::optional<RegisterOutcome> decode_register_outcome(std::span<std::byte const> in) noexcept {
    RegisterOutcome out{};
    if (!extract_le(in, out.code)) {
        return std::nullopt;
    }
    if (!in.empty()) {
        return std::nullopt;
    }
    return out;
}

// RegisterAck body: empty
std::optional<std::size_t> encode_register_ack(std::span<std::byte> out) noexcept {
    std::span<std::byte> cur = out;
    if (!append_type(MessageType::register_ack, cur)) {
        return std::nullopt;
    }
    return out.size() - cur.size();
}
std::optional<RegisterAck> decode_register_ack(std::span<std::byte const> in) noexcept {
    if (!in.empty()) {
        return std::nullopt;
    }
    return RegisterAck{};
}

// Heartbeat body: empty
std::optional<std::size_t> encode_heartbeat(std::span<std::byte> out) noexcept {
    std::span<std::byte> cur = out;
    if (!append_type(MessageType::heartbeat, cur)) {
        return std::nullopt;
    }
    return out.size() - cur.size();
}
std::optional<Heartbeat> decode_heartbeat(std::span<std::byte const> in) noexcept {
    if (!in.empty()) {
        return std::nullopt;
    }
    return Heartbeat{};
}

// Status body: [mode(u8)][load(f64le)][temp(f64le)]
std::optional<std::size_t>
encode_status(std::uint8_t mode, double load, double temp, std::span<std::byte> out) noexcept {
    std::span<std::byte> cur = out;
    if (!append_type(MessageType::status, cur)) {
        return std::nullopt;
    }
    if (!append_le(mode, cur)) {
        return std::nullopt;
    }
    if (!append_f64(load, cur)) {
        return std::nullopt;
    }
    if (!append_f64(temp, cur)) {
        return std::nullopt;
    }
    return out.size() - cur.size();
}
std::optional<Status> decode_status(std::span<std::byte const> in) noexcept {
    Status out{};
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

// CommandRequest body: [command_id(u64le)][command_type(u8)][payload(rest)]
// payload는 호출측이 out 뒤에 직접 append한다. 여기선 header까지만 쓴다.
std::optional<std::size_t>
encode_command_request_header(std::uint64_t command_id, std::uint8_t command_type, std::span<std::byte> out) noexcept {
    std::span<std::byte> cur = out;
    if (!append_type(MessageType::command_request, cur)) {
        return std::nullopt;
    }
    if (!append_le(command_id, cur)) {
        return std::nullopt;
    }
    if (!append_le(command_type, cur)) {
        return std::nullopt;
    }
    return out.size() - cur.size();
}
std::optional<CommandRequest> decode_command_request(std::span<std::byte const> in) noexcept {
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

// CommandAck body: [command_id(u64le)]
std::optional<std::size_t> encode_command_ack(std::uint64_t command_id, std::span<std::byte> out) noexcept {
    std::span<std::byte> cur = out;
    if (!append_type(MessageType::command_ack, cur)) {
        return std::nullopt;
    }
    if (!append_le(command_id, cur)) {
        return std::nullopt;
    }
    return out.size() - cur.size();
}
std::optional<CommandAck> decode_command_ack(std::span<std::byte const> in) noexcept {
    CommandAck out{};
    if (!extract_le(in, out.command_id)) {
        return std::nullopt;
    }
    if (!in.empty()) {
        return std::nullopt;
    }
    return out;
}

// CommandOutcome body: [command_id(u64le)][code(u8)]
std::optional<std::size_t>
encode_command_outcome(std::uint64_t command_id, std::uint8_t code, std::span<std::byte> out) noexcept {
    std::span<std::byte> cur = out;
    if (!append_type(MessageType::command_outcome, cur)) {
        return std::nullopt;
    }
    if (!append_le(command_id, cur)) {
        return std::nullopt;
    }
    if (!append_le(code, cur)) {
        return std::nullopt;
    }
    return out.size() - cur.size();
}
std::optional<CommandOutcome> decode_command_outcome(std::span<std::byte const> in) noexcept {
    CommandOutcome out{};
    if (!extract_le(in, out.command_id)) {
        return std::nullopt;
    }
    if (!extract_le(in, out.code)) {
        return std::nullopt;
    }
    if (!in.empty()) {
        return std::nullopt;
    }
    return out;
}

} // namespace ddcs::wire::acmp
