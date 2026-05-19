#include "ddcs/net/message/message.hpp"

#include <string_view>
#include <utility>

namespace ddcs::net::message {

namespace {

void write_u16_le(std::vector<std::byte>& out, std::uint16_t v) {
    out.push_back(static_cast<std::byte>(v & 0xff));
    out.push_back(static_cast<std::byte>((v >> 8) & 0xff));
}

void write_u64_le(std::vector<std::byte>& out, std::uint64_t v) {
    for (std::size_t i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::byte>((v >> (i * 8)) & 0xff));
    }
}

void write_string(std::vector<std::byte>& out, std::string_view s) {
    auto const len = static_cast<std::uint16_t>(s.size());
    write_u16_le(out, len);
    for (char c : s) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }
}

std::optional<std::uint16_t> read_u16_le(std::span<std::byte const> src, std::size_t& cursor) {
    if (cursor + 2 > src.size()) {
        return std::nullopt;
    }
    auto const v = static_cast<std::uint16_t>(
        std::to_integer<std::uint16_t>(src[cursor]) |
        (std::to_integer<std::uint16_t>(src[cursor + 1]) << 8)
    );
    cursor += 2;
    return v;
}

std::optional<std::uint64_t> read_u64_le(std::span<std::byte const> src, std::size_t& cursor) {
    if (cursor + 8 > src.size()) {
        return std::nullopt;
    }
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        v |= static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(src[cursor + i])) << (i * 8);
    }
    cursor += 8;
    return v;
}

std::optional<std::string> read_string(std::span<std::byte const> src, std::size_t& cursor) {
    auto const len = read_u16_le(src, cursor);
    if (!len) {
        return std::nullopt;
    }
    if (cursor + *len > src.size()) {
        return std::nullopt;
    }
    std::string s;
    s.reserve(*len);
    for (std::size_t i = 0; i < *len; ++i) {
        s.push_back(static_cast<char>(std::to_integer<unsigned char>(src[cursor + i])));
    }
    cursor += *len;
    return s;
}

} // namespace

std::vector<std::byte> encode(RegisterRequest const& msg) {
    std::vector<std::byte> out;
    write_string(out, msg.agent_tag);
    return out;
}

std::vector<std::byte> encode(RegisterSuccess const&) { return {}; }

std::vector<std::byte> encode(RegisterFail const& msg) {
    std::vector<std::byte> out;
    write_string(out, msg.reason);
    return out;
}

std::vector<std::byte> encode(Status const& msg) {
    std::vector<std::byte> out;
    write_u64_le(out, msg.timestamp_ns);
    write_string(out, msg.state);
    return out;
}

std::vector<std::byte> encode(Command const& msg) {
    std::vector<std::byte> out;
    write_u64_le(out, msg.command_id);
    write_string(out, msg.body);
    return out;
}

std::vector<std::byte> encode(CommandAck const& msg) {
    std::vector<std::byte> out;
    write_u64_le(out, msg.command_id);
    return out;
}

std::vector<std::byte> encode(CommandSuccess const& msg) {
    std::vector<std::byte> out;
    write_u64_le(out, msg.command_id);
    return out;
}

std::vector<std::byte> encode(CommandFail const& msg) {
    std::vector<std::byte> out;
    write_u64_le(out, msg.command_id);
    write_string(out, msg.reason);
    return out;
}

std::optional<RegisterRequest> decode_register_request(std::span<std::byte const> src) {
    std::size_t cursor = 0;
    auto agent_tag = read_string(src, cursor);
    if (!agent_tag || cursor != src.size()) {
        return std::nullopt;
    }
    return RegisterRequest{.agent_tag = std::move(*agent_tag)};
}

std::optional<RegisterSuccess> decode_register_success(std::span<std::byte const> src) {
    if (!src.empty()) {
        return std::nullopt;
    }
    return RegisterSuccess{};
}

std::optional<RegisterFail> decode_register_fail(std::span<std::byte const> src) {
    std::size_t cursor = 0;
    auto reason = read_string(src, cursor);
    if (!reason || cursor != src.size()) {
        return std::nullopt;
    }
    return RegisterFail{.reason = std::move(*reason)};
}

std::optional<Status> decode_status(std::span<std::byte const> src) {
    std::size_t cursor = 0;
    auto timestamp_ns = read_u64_le(src, cursor);
    if (!timestamp_ns) {
        return std::nullopt;
    }
    auto state = read_string(src, cursor);
    if (!state || cursor != src.size()) {
        return std::nullopt;
    }
    return Status{.timestamp_ns = *timestamp_ns, .state = std::move(*state)};
}

std::optional<Command> decode_command(std::span<std::byte const> src) {
    std::size_t cursor = 0;
    auto command_id = read_u64_le(src, cursor);
    if (!command_id) {
        return std::nullopt;
    }
    auto body = read_string(src, cursor);
    if (!body || cursor != src.size()) {
        return std::nullopt;
    }
    return Command{.command_id = *command_id, .body = std::move(*body)};
}

std::optional<CommandAck> decode_command_ack(std::span<std::byte const> src) {
    std::size_t cursor = 0;
    auto command_id = read_u64_le(src, cursor);
    if (!command_id || cursor != src.size()) {
        return std::nullopt;
    }
    return CommandAck{.command_id = *command_id};
}

std::optional<CommandSuccess> decode_command_success(std::span<std::byte const> src) {
    std::size_t cursor = 0;
    auto command_id = read_u64_le(src, cursor);
    if (!command_id || cursor != src.size()) {
        return std::nullopt;
    }
    return CommandSuccess{.command_id = *command_id};
}

std::optional<CommandFail> decode_command_fail(std::span<std::byte const> src) {
    std::size_t cursor = 0;
    auto command_id = read_u64_le(src, cursor);
    if (!command_id) {
        return std::nullopt;
    }
    auto reason = read_string(src, cursor);
    if (!reason || cursor != src.size()) {
        return std::nullopt;
    }
    return CommandFail{.command_id = *command_id, .reason = std::move(*reason)};
}

} // namespace ddcs::net::message
