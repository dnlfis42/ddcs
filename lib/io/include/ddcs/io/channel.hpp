#pragma once

#include "ddcs/common/fd.hpp"
#include "ddcs/io/channel_events.hpp"
#include "ddcs/io/channel_handler.hpp"

#include <cassert>
#include <utility>

namespace ddcs::io {

class Reactor;

// fd readiness를 Reactor에 등록하기 위한 정보
class Channel {
public:
    Channel() = default;

    Channel(Channel const&) = delete;
    Channel& operator=(Channel const&) = delete;
    Channel(Channel&&) noexcept = delete;
    Channel& operator=(Channel&&) noexcept = delete;

public:
    [[nodiscard]] int fd() const noexcept { return fd_.get(); }
    [[nodiscard]] ChannelEvents interests() const noexcept { return interests_; }
    [[nodiscard]] ChannelHandler& handler() const noexcept { return *handler_; }
    [[nodiscard]] bool registered() const noexcept { return registered_; }
    [[nodiscard]] bool valid() const noexcept { return fd_.valid() && handler_ != nullptr; }

public:
    bool init(common::Fd&& fd, ChannelEvents interests, ChannelHandler& handler) noexcept {
        if (registered() || valid()) {
            return false;
        }
        if (!fd.valid()) {
            return false;
        }

        fd_ = std::move(fd);
        interests_ = interests;
        handler_ = &handler;
        return true;
    }

    void reset() noexcept {
        assert(!registered_);

        fd_.reset();
        interests_ = ChannelEvents::none;
        handler_ = nullptr;
        registered_ = false;
    }

private:
    friend class Reactor;

private:
    void set_interests(ChannelEvents interests) noexcept { interests_ = interests; }
    void mark_registered() noexcept { registered_ = true; }
    void mark_unregistered() noexcept { registered_ = false; }

private:
    common::Fd fd_{};
    ChannelEvents interests_{};
    ChannelHandler* handler_{nullptr};
    bool registered_{false};
};

} // namespace ddcs::io
