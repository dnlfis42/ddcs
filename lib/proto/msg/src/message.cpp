#include "ddcs/proto/msg/message.hpp"

#include "ddcs/common/endian.hpp"

#include <array>
#include <concepts>
#include <string>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace ddcs::proto::msg {

namespace {

template <std::unsigned_integral T>
bool write_le(common::LinearBuffer& buf, T v) noexcept {
    T const le = common::to_le(v);
    return buf.write({reinterpret_cast<std::byte const*>(&le), sizeof(T)});
}

template <std::unsigned_integral T>
bool read_le(std::span<std::byte const>& in, T& out) noexcept {
    if (in.size() < sizeof(T)) {
        return false;
    }
    T raw{};
    std::memcpy(&raw, in.data(), sizeof(T));
    out = common::from_le(raw);
    in = in.subspan(sizeof(T));
    return true;
}

bool read_bytes(std::span<std::byte const>& in, std::byte* dst, std::size_t n) noexcept {
    if (in.size() < n) {
        return false;
    }
    std::memcpy(dst, in.data(), n);
    in = in.subspan(n);
    return true;
}

// string: [len u16le][utf8 N]
bool write_string(common::LinearBuffer& out, std::string const& s) noexcept {
    if (s.size() > 0xFFFFu) {
        return false;
    }
    if (!write_le<std::uint16_t>(out, static_cast<std::uint16_t>(s.size()))) {
        return false;
    }
    if (s.empty()) {
        return true;
    }
    return out.write({reinterpret_cast<std::byte const*>(s.data()), s.size()});
}

bool read_string(std::span<std::byte const>& in, std::string& out) {
    std::uint16_t len{};
    if (!read_le(in, len)) {
        return false;
    }
    if (in.size() < len) {
        return false;
    }
    out.assign(reinterpret_cast<char const*>(in.data()), len);
    in = in.subspan(len);
    return true;
}

} // namespace

// --- RegisterRequest: [uuid(16)][group: str][version: str] ---
bool encode(RegisterRequest const& m, common::LinearBuffer& out) noexcept {
    auto const& b = m.agent_uuid.bytes();
    if (!out.write({b.data(), b.size()})) {
        return false;
    }
    if (!write_string(out, m.group)) {
        return false;
    }
    return write_string(out, m.version);
}
bool decode(std::span<std::byte const> in, RegisterRequest& out) {
    std::array<std::byte, 16> b{};
    if (!read_bytes(in, b.data(), 16)) {
        return false;
    }
    out.agent_uuid = common::Uuid{b};
    if (!read_string(in, out.group)) {
        return false;
    }
    if (!read_string(in, out.version)) {
        return false;
    }
    return in.empty();
}

// --- RegisterResponse: [result(u8)][reason: str] ---
bool encode(RegisterResponse const& m, common::LinearBuffer& out) noexcept {
    std::byte const result_b{static_cast<std::uint8_t>(m.result)};
    if (!out.write({&result_b, 1})) {
        return false;
    }
    return write_string(out, m.reason);
}
bool decode(std::span<std::byte const> in, RegisterResponse& out) {
    if (in.empty()) {
        return false;
    }
    out.result = static_cast<RegisterResult>(static_cast<std::uint8_t>(in[0]));
    in = in.subspan(1);
    if (!read_string(in, out.reason)) {
        return false;
    }
    return in.empty();
}

// --- Heartbeat: [timestamp_ms(u64le)] ---
bool encode(Heartbeat const& m, common::LinearBuffer& out) noexcept {
    return write_le<std::uint64_t>(out, m.timestamp_ms);
}
bool decode(std::span<std::byte const> in, Heartbeat& out) noexcept {
    if (!read_le(in, out.timestamp_ms)) {
        return false;
    }
    return in.empty();
}

// --- Status: [timestamp_ms(u64le)][status_json: str] ---
bool encode(Status const& m, common::LinearBuffer& out) noexcept {
    if (!write_le<std::uint64_t>(out, m.timestamp_ms)) {
        return false;
    }
    return write_string(out, m.status_json);
}
bool decode(std::span<std::byte const> in, Status& out) {
    if (!read_le(in, out.timestamp_ms)) {
        return false;
    }
    if (!read_string(in, out.status_json)) {
        return false;
    }
    return in.empty();
}

// --- Command: [command_id(u64le)][type(u8)][payload(rest)] ---
//   type/payload 는 opaque - 의미는 proto::cmd 가 해석. payload 는 길이 prefix 없이 body 의 나머지.
bool encode(Command const& m, common::LinearBuffer& out) noexcept {
    if (!write_le<std::uint64_t>(out, m.command_id)) {
        return false;
    }
    std::byte const type_b{m.type};
    if (!out.write({&type_b, 1})) {
        return false;
    }
    if (m.payload.empty()) {
        return true;
    }
    return out.write({reinterpret_cast<std::byte const*>(m.payload.data()), m.payload.size()});
}
bool decode(std::span<std::byte const> in, Command& out) {
    if (!read_le(in, out.command_id)) {
        return false;
    }
    if (in.empty()) {
        return false;
    }
    out.type = static_cast<std::uint8_t>(in[0]);
    in = in.subspan(1);
    out.payload.assign(reinterpret_cast<char const*>(in.data()), in.size()); // 나머지 전부
    return true;
}

// --- CommandAck: [command_id(u64le)] ---
bool encode(CommandAck const& m, common::LinearBuffer& out) noexcept {
    return write_le<std::uint64_t>(out, m.command_id);
}
bool decode(std::span<std::byte const> in, CommandAck& out) noexcept {
    if (!read_le(in, out.command_id)) {
        return false;
    }
    return in.empty();
}

// --- CommandOutcome: [command_id(u64le)][result(u8)][reason: str] ---
bool encode(CommandOutcome const& m, common::LinearBuffer& out) noexcept {
    if (!write_le<std::uint64_t>(out, m.command_id)) {
        return false;
    }
    std::byte const result_b{static_cast<std::uint8_t>(m.result)};
    if (!out.write({&result_b, 1})) {
        return false;
    }
    return write_string(out, m.reason);
}
bool decode(std::span<std::byte const> in, CommandOutcome& out) {
    if (!read_le(in, out.command_id)) {
        return false;
    }
    if (in.empty()) {
        return false;
    }
    out.result = static_cast<CommandResult>(static_cast<std::uint8_t>(in[0]));
    in = in.subspan(1);
    if (!read_string(in, out.reason)) {
        return false;
    }
    return in.empty();
}

} // namespace ddcs::proto::msg
