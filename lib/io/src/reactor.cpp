#include "ddcs/io/reactor.hpp"

#include "ddcs/common/fd.hpp"
#include "ddcs/common/throw_errno.hpp"
#include "ddcs/io/channel.hpp"
#include "ddcs/io/channel_events.hpp"
#include "ddcs/io/detail/channel_registry.hpp"

#include <array>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

#include <sys/epoll.h>

namespace ddcs::io {

namespace {

constexpr std::size_t max_epoll_events = 64;
constexpr int epoll_wait_forever = -1;
constexpr std::chrono::milliseconds wait_forever{-1};

[[nodiscard]] int to_epoll_timeout(std::chrono::milliseconds timeout) noexcept {
    auto const milliseconds = timeout.count();
    if (milliseconds < 0) {
        return epoll_wait_forever;
    }
    if (milliseconds > std::numeric_limits<int>::max()) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(milliseconds);
}

[[nodiscard]] std::uint32_t to_epoll_interests(ChannelEvents interests) noexcept {
    std::uint32_t events{0};
    if (contains(interests, ChannelEvents::readable)) {
        events |= EPOLLIN;
    }
    if (contains(interests, ChannelEvents::writable)) {
        events |= EPOLLOUT;
    }
    if (contains(interests, ChannelEvents::edge_triggered)) {
        events |= EPOLLET;
    }
    if (contains(interests, ChannelEvents::one_shot)) {
        events |= EPOLLONESHOT;
    }
    return events;
}

[[nodiscard]] ChannelEvents to_channel_events(std::uint32_t epoll_events) noexcept {
    ChannelEvents mask{ChannelEvents::none};
    if ((epoll_events & EPOLLIN) != 0u) {
        mask |= ChannelEvents::readable;
    }
    if ((epoll_events & EPOLLOUT) != 0u) {
        mask |= ChannelEvents::writable;
    }
    if ((epoll_events & EPOLLERR) != 0u) {
        mask |= ChannelEvents::error;
    }
    if ((epoll_events & EPOLLHUP) != 0u) {
        mask |= ChannelEvents::hangup;
    }
    return mask;
}

} // namespace

struct Reactor::Impl {
    enum class State : std::uint8_t {
        ready = 0x01,
        running = 0x02,
    };

    common::Fd epoll_fd;
    State state = State::ready;
    detail::ChannelRegistry channel_registry;
};

Reactor::Reactor()
    : impl_(std::make_unique<Impl>()) {
    int const fd = ::epoll_create1(EPOLL_CLOEXEC);
    if (fd < 0) {
        int const err = errno;
        common::throw_errno(err, "epoll_create1");
    }

    impl_->epoll_fd.reset(fd);
    impl_->state = Impl::State::ready;
}

Reactor::~Reactor() = default;

void Reactor::run() {
    assert(impl_->state == Impl::State::ready);

    impl_->state = Impl::State::running;
    while (impl_->state == Impl::State::running) {
        run_once(wait_forever);
    }
}

void Reactor::run_once(std::chrono::milliseconds timeout) {
    assert(impl_->state == Impl::State::ready || impl_->state == Impl::State::running);

    std::array<epoll_event, max_epoll_events> events{};

    int const n = ::epoll_wait(
        impl_->epoll_fd.get(), events.data(), static_cast<int>(events.size()),
        to_epoll_timeout(timeout)
    );
    if (n < 0) {
        int const err = errno;
        if (err == EINTR) {
            return;
        }
        common::throw_errno(err, "epoll_wait");
    }

    for (std::size_t i = 0; i < static_cast<std::size_t>(n); ++i) {
        auto const& event = events[i];
        // CAUTION: generation token이 어긋나면 fd 재사용 뒤의 stale 이벤트이므로 건너뛴다
        if (auto* channel = impl_->channel_registry.resolve(event.data.u64)) {
            channel->handler().on_ready(*channel, to_channel_events(event.events));
        }
    }
}

void Reactor::stop() noexcept {
    if (impl_->state == Impl::State::running) {
        impl_->state = Impl::State::ready;
    }
}

bool Reactor::running() const noexcept {
    return impl_->state == Impl::State::running;
}

bool Reactor::add(Channel& channel) {
    if (channel.registered()) {
        return true;
    }
    if (!channel.valid()) {
        return false;
    }

    epoll_event ev{};
    ev.events = to_epoll_interests(channel.interests());
    ev.data.u64 = impl_->channel_registry.insert(channel);

    if (::epoll_ctl(impl_->epoll_fd.get(), EPOLL_CTL_ADD, channel.fd(), &ev) != 0) {
        (void)impl_->channel_registry.erase(channel);
        return false;
    }

    channel.mark_registered();
    return true;
}

bool Reactor::modify(Channel& channel, ChannelEvents interests) {
    if (!channel.registered()) {
        return false;
    }

    epoll_event ev{};
    ev.events = to_epoll_interests(interests);
    ev.data.u64 = impl_->channel_registry.token(channel);

    if (::epoll_ctl(impl_->epoll_fd.get(), EPOLL_CTL_MOD, channel.fd(), &ev) != 0) {
        return false;
    }

    channel.set_interests(interests);
    return true;
}

void Reactor::remove(Channel& channel) noexcept {
    if (!channel.registered()) {
        return;
    }

    (void)::epoll_ctl(impl_->epoll_fd.get(), EPOLL_CTL_DEL, channel.fd(), nullptr);
    (void)impl_->channel_registry.erase(channel);
    channel.mark_deregistered();
}

} // namespace ddcs::io
