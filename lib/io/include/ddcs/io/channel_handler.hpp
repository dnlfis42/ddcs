#pragma once

#include "ddcs/io/channel_events.hpp"

namespace ddcs::io {

class Channel;

// Channel에 도착한 fd readiness 이벤트를 처리한다.
class ChannelHandler {
public:
    virtual ~ChannelHandler() = default;
    virtual void on_ready(Channel& channel, ChannelEvents events) = 0;
};

} // namespace ddcs::io
