#pragma once

#include "ddcs/io/channel_events.hpp"
#include "ddcs/io/channel_handler.hpp"
#include "ddcs/io/fd.hpp"

#include <cassert>
#include <cstdint>
#include <exception>
#include <utility>

namespace ddcs::io {

class Reactor;

// fd readiness를 Reactor에 등록하기 위한 정보
class Channel {
public:
    enum class State : std::uint8_t {
        idle,
        ready,
        registered,
    };

    Channel() = default;
    ~Channel() = default;

    Channel(Channel const&) = delete;
    Channel& operator=(Channel const&) = delete;
    Channel(Channel&&) noexcept = delete;
    Channel& operator=(Channel&&) noexcept = delete;

    [[nodiscard]] bool
    init(io::Fd&& fd, ChannelEvents interests, ChannelHandler& handler) noexcept {
        if (valid() || !fd.valid()) {
            return false;
        }

        fd_ = std::move(fd);
        interests_ = interests;
        handler_ = &handler;
        state_ = State::ready;
        return true;
    }

    [[nodiscard]] int fd() const noexcept {
        return fd_.get();
    }

    [[nodiscard]] ChannelEvents interests() const noexcept {
        return interests_;
    }

    [[nodiscard]] ChannelHandler& handler() const noexcept {
        assert(handler_ != nullptr);
        return *handler_;
    }

    [[nodiscard]] bool valid() const noexcept {
        return state_ != State::idle;
    }

    [[nodiscard]] bool registered() const noexcept {
        return state_ == State::registered;
    }

    void close() noexcept {
        // CAUTION: registered 상태로 close하면 Reactor에 dangling 포인터와 stale fd가 남는다.
        if (registered()) {
            assert(false && "Channel: close on registered channel");
            std::terminate();
        }

        fd_.close();
        interests_ = ChannelEvents::none;
        handler_ = nullptr;
        state_ = State::idle;
    }

    void reset() noexcept {
        close();
    }

private:
    friend class Reactor;

    void set_interests(ChannelEvents interests) noexcept {
        interests_ = interests;
    }

    void mark_registered() noexcept {
        assert(state_ == State::ready);
        state_ = State::registered;
    }

    void mark_deregistered() noexcept {
        assert(state_ == State::registered);
        state_ = State::ready;
    }

    io::Fd fd_;
    ChannelEvents interests_ = ChannelEvents::none;
    ChannelHandler* handler_ = nullptr;
    State state_ = State::idle;
};

} // namespace ddcs::io
