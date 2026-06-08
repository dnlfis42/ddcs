#include "ddcs/proto/msg/message.hpp"

#include "ddcs/common/endian.hpp"

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace ddcs::proto::msg {

namespace {

template <std::unsigned_integral T>
bool write_le(common::LinearBuffer& out, T value) noexcept {
    T const le = common::to_le(value);
    return out.write({reinterpret_cast<std::byte const*>(&le), sizeof(T)});
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

bool write_u8(common::LinearBuffer& out, std::uint8_t value) noexcept {
    std::byte const byte{value};
    return out.write({&byte, 1});
}

bool read_u8(std::span<std::byte const>& in, std::uint8_t& out) noexcept {
    if (in.empty()) {
        return false;
    }
    out = static_cast<std::uint8_t>(in[0]);
    in = in.subspan(1);
    return true;
}

bool write_f64_le(common::LinearBuffer& out, double value) noexcept {
    return write_le<std::uint64_t>(out, std::bit_cast<std::uint64_t>(value));
}

bool read_f64_le(std::span<std::byte const>& in, double& out) noexcept {
    std::uint64_t bits{};
    if (!read_le(in, bits)) {
        return false;
    }
    out = std::bit_cast<double>(bits);
    return true;
}

bool read_bytes(std::span<std::byte const>& in, std::byte* dst, std::size_t size) noexcept {
    if (in.size() < size) {
        return false;
    }
    std::memcpy(dst, in.data(), size);
    in = in.subspan(size);
    return true;
}

// string은 [len(u16le)][UTF-8 bytes] 형식
bool write_string(common::LinearBuffer& out, std::string const& value) noexcept {
    if (value.size() > 0xFFFFu) {
        return false;
    }
    if (!write_le<std::uint16_t>(out, static_cast<std::uint16_t>(value.size()))) {
        return false;
    }
    if (value.empty()) {
        return true;
    }
    return out.write({reinterpret_cast<std::byte const*>(value.data()), value.size()});
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

// --- RegisterRequest: [id: uuid(16)][group: str] ---
bool encode(RegisterRequest const& m, common::LinearBuffer& out) noexcept {
    auto const& b = m.id.bytes();
    if (!out.write({b.data(), b.size()})) {
        return false;
    }
    return write_string(out, m.group);
}
bool decode(std::span<std::byte const> in, RegisterRequest& out) {
    std::array<std::byte, 16> b{};
    if (!read_bytes(in, b.data(), 16)) {
        return false;
    }
    out.id = common::Uuid{b};
    if (!read_string(in, out.group)) {
        return false;
    }
    return in.empty();
}

// --- RegisterResponse: [result(u8)][reason: str] ---
bool encode(RegisterResponse const& m, common::LinearBuffer& out) noexcept {
    if (!write_u8(out, static_cast<std::uint8_t>(m.result))) {
        return false;
    }
    return write_string(out, m.reason);
}
bool decode(std::span<std::byte const> in, RegisterResponse& out) {
    std::uint8_t result{};
    if (!read_u8(in, result)) {
        return false;
    }
    out.result = static_cast<RegisterResult>(result);
    if (!read_string(in, out.reason)) {
        return false;
    }
    return in.empty();
}

// --- Heartbeat: empty ---
bool encode(Heartbeat const&, common::LinearBuffer&) noexcept { return true; }
bool decode(std::span<std::byte const> in, Heartbeat&) noexcept { return in.empty(); }

// --- Status: [mode(u8)][load(f64le)][temp(f64le)] ---
bool encode(Status const& m, common::LinearBuffer& out) noexcept {
    if (!write_u8(out, m.mode)) {
        return false;
    }
    if (!write_f64_le(out, m.load)) {
        return false;
    }
    return write_f64_le(out, m.temp);
}
bool decode(std::span<std::byte const> in, Status& out) noexcept {
    if (!read_u8(in, out.mode)) {
        return false;
    }
    if (!read_f64_le(in, out.load)) {
        return false;
    }
    if (!read_f64_le(in, out.temp)) {
        return false;
    }
    return in.empty();
}

// --- Command: [command_id(u64le)][type(u8)][payload(rest)] ---
// payload는 구조체가 소유하지 않고 호출자에게 raw tail span으로 전달한다.
bool encode(Command const& m, std::span<std::byte const> payload, common::LinearBuffer& out) noexcept {
    if (!write_le<std::uint64_t>(out, m.command_id)) {
        return false;
    }
    if (!write_u8(out, m.type)) {
        return false;
    }
    if (payload.empty()) {
        return true;
    }
    return out.write(payload);
}
bool decode(std::span<std::byte const> in, Command& out, std::span<std::byte const>& payload) noexcept {
    if (!read_le(in, out.command_id)) {
        return false;
    }
    if (!read_u8(in, out.type)) {
        return false;
    }
    payload = in;
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
    if (!write_u8(out, static_cast<std::uint8_t>(m.result))) {
        return false;
    }
    return write_string(out, m.reason);
}
bool decode(std::span<std::byte const> in, CommandOutcome& out) {
    if (!read_le(in, out.command_id)) {
        return false;
    }
    std::uint8_t result{};
    if (!read_u8(in, result)) {
        return false;
    }
    out.result = static_cast<CommandResult>(result);
    if (!read_string(in, out.reason)) {
        return false;
    }
    return in.empty();
}

} // namespace ddcs::proto::msg
