#pragma once

#include "ddcs/common/fd.hpp"
#include "ddcs/io/channel_events.hpp"
#include "ddcs/io/channel_handler.hpp"

#include <cassert>
#include <cstdint>
#include <utility>

namespace ddcs::io {

class Reactor;

// fd readiness를 Reactor에 등록하기 위한 정보
class Channel {
    friend class Reactor;

public:
    enum class State : std::uint8_t {
        idle,
        ready,
        registered,
    };

public:
    Channel() = default;
    ~Channel() = default;

    Channel(Channel const&) = delete;
    Channel& operator=(Channel const&) = delete;
    Channel(Channel&&) noexcept = delete;
    Channel& operator=(Channel&&) noexcept = delete;

    [[nodiscard]] int fd() const noexcept { return fd_.get(); }
    [[nodiscard]] ChannelEvents interests() const noexcept { return interests_; }
    [[nodiscard]] ChannelHandler& handler() const noexcept {
        assert(handler_ != nullptr);
        return *handler_;
    }
    [[nodiscard]] bool valid() const noexcept { return state_ != State::idle; }
    [[nodiscard]] bool registered() const noexcept { return state_ == State::registered; }

    bool init(common::Fd&& fd, ChannelEvents interests, ChannelHandler& handler) noexcept {
        if (valid() || !fd.valid()) {
            return false;
        }

        fd_ = std::move(fd);
        interests_ = interests;
        handler_ = &handler;
        state_ = State::ready;
        return true;
    }

    void close() noexcept {
        assert(!registered());

        fd_.close();
        interests_ = ChannelEvents::none;
        handler_ = nullptr;
        state_ = State::idle;
    }

    void reset() noexcept { close(); }

private:
    void set_interests(ChannelEvents interests) noexcept { interests_ = interests; }

    void mark_registered() noexcept {
        assert(state_ == State::ready);
        state_ = State::registered;
    }

    void mark_deregistered() noexcept {
        assert(state_ == State::registered);
        state_ = State::ready;
    }

private:
    common::Fd fd_{};
    ChannelEvents interests_{};
    ChannelHandler* handler_{nullptr};
    State state_{State::idle};
};

} // namespace ddcs::io
