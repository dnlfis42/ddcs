#pragma once

#include "ddcs/io/channel_events.hpp"

namespace ddcs::io {

class Channel;

// Channel의 fd readiness를 받는 콜백 인터페이스
class ChannelHandler {
public:
    virtual ~ChannelHandler() = default;

    // channel의 fd가 준비되면 Reactor가 호출한다. events는 발생한 readiness 집합이다.
    virtual void on_ready(Channel& channel, ChannelEvents events) = 0;
};

} // namespace ddcs::io
