#pragma once

#include "ddcs/io/channel_events.hpp"
#include "ddcs/io/channel_handler.hpp"
#include "ddcs/io/fd.hpp"

#include <cassert>
#include <exception>
#include <utility>

namespace ddcs::io {

class Reactor;

// fd readiness를 Reactor에 등록하기 위한 정보
class Channel {
public:
    Channel() = default;
    ~Channel() = default;

    Channel(Channel const&) = delete;
    Channel& operator=(Channel const&) = delete;
    Channel(Channel&&) noexcept = delete;
    Channel& operator=(Channel&&) noexcept = delete;

    // fd 소유권을 받아 channel을 준비한다.
    // 전제조건: 미초기화 channel + 유효한 fd. 위반은 프로그래머 오류다.
    void init(Fd&& fd, ChannelEvents interests, ChannelHandler& handler) noexcept {
        if (valid() || !fd.valid()) {
            assert(false && "init() precondition: fresh channel and valid fd");
            std::terminate();
        }

        fd_ = std::move(fd);
        interests_ = interests;
        handler_ = &handler;
    }

    [[nodiscard]] int fd() const noexcept {
        return fd_.get();
    }

    [[nodiscard]] ChannelEvents interests() const noexcept {
        return interests_;
    }

    // 전제조건: 초기화된 channel. 위반은 프로그래머 오류다.
    [[nodiscard]] ChannelHandler& handler() const noexcept {
        if (handler_ == nullptr) {
            assert(false && "handler() on uninitialized channel");
            std::terminate();
        }

        return *handler_;
    }

    [[nodiscard]] bool valid() const noexcept {
        return fd_.valid();
    }

    [[nodiscard]] bool registered() const noexcept {
        return registered_;
    }

    // registered 상태로 close하면 Reactor에 dangling 포인터와 stale fd가 남는다.
    void close() noexcept {
        if (registered()) {
            assert(false && "close() on registered channel");
            std::terminate();
        }

        fd_.close();
        interests_ = ChannelEvents::none;
        handler_ = nullptr;
    }

private:
    friend class Reactor;

    void set_interests(ChannelEvents interests) noexcept {
        interests_ = interests;
    }

    void mark_registered() noexcept {
        assert(valid() && !registered_);

        registered_ = true;
    }

    void mark_deregistered() noexcept {
        assert(registered_);

        registered_ = false;
    }

    Fd fd_;
    ChannelEvents interests_ = ChannelEvents::none;
    ChannelHandler* handler_ = nullptr;
    bool registered_ = false;
};

} // namespace ddcs::io
