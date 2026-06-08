#include "ddcs/ctrl/controller.hpp"

#include "ddcs/common/linear_buffer.hpp"
#include "ddcs/common/object_pool.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/logger/log.hpp"
#include "ddcs/proto/frame/frame.hpp"
#include "ddcs/proto/msg/message.hpp"
#include "ddcs/proto/msg/type.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

namespace {

using ddcs::common::LinearBuffer;
using ddcs::common::Uuid;
using ddcs::ctrl::Controller;
namespace frame = ddcs::proto::frame;
namespace msg = ddcs::proto::msg;

// 양쪽 로그를 모아 처리 여부를 단언.
class CaptureSink : public ddcs::logger::Sink {
public:
    void write(std::string_view line) noexcept override {
        std::lock_guard<std::mutex> lk{m_};
        lines_.emplace_back(line);
    }
    bool contains(std::string_view needle) {
        std::lock_guard<std::mutex> lk{m_};
        return std::any_of(lines_.begin(), lines_.end(), [&](auto const& l) {
            return l.find(needle) != std::string::npos;
        });
    }

private:
    std::vector<std::string> lines_;
    std::mutex m_;
};

Uuid make_uuid(std::uint8_t seed) {
    std::array<std::byte, 16> b{};
    b.fill(std::byte{seed});
    return Uuid{b};
}

// 새 프로토콜로 프레임을 직접 빚는/푸는 raw 소켓 의사-agent.
// (controller 는 단일 스레드라 run_once 펌프와 인터리브한다.)
class RawAgent {
public:
    ~RawAgent() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    bool connect_to(std::uint16_t port) {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) {
            return false;
        }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = ::htons(port);
        addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
        // loopback 은 app 의 accept 이전에도 커널이 핸드셰이크를 완료 -> blocking connect 즉시 성공.
        return ::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    }

    template <typename T>
    void send_message(msg::MessageType type, T const& m) {
        static auto pool = ddcs::common::make_pool<LinearBuffer>(0, 4, std::size_t{256});
        auto buf = pool.acquire();
        ASSERT_TRUE(msg::encode(m, *buf));
        auto const body = buf->readable();

        frame::Header const h{
            .magic = frame::magic,
            .type = static_cast<std::uint8_t>(type),
            .length = static_cast<std::uint16_t>(body.size()),
        };
        auto const hb = frame::encode(h);

        std::vector<std::byte> wire;
        wire.insert(wire.end(), hb.begin(), hb.end());
        wire.insert(wire.end(), body.begin(), body.end());
        ASSERT_EQ(::send(fd_, wire.data(), wire.size(), 0), static_cast<ssize_t>(wire.size()));
    }

    // 소켓에서 가용 바이트를 비차단으로 흡수. FIN/RST 면 closed_ 표시.
    void drain() {
        std::array<std::byte, 512> tmp{};
        for (;;) {
            ssize_t const n = ::recv(fd_, tmp.data(), tmp.size(), MSG_DONTWAIT);
            if (n > 0) {
                rx_.insert(rx_.end(), tmp.begin(), tmp.begin() + n);
                continue;
            }
            if (n == 0) {
                closed_ = true; // FIN
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                closed_ = true; // RST 등
            }
            break;
        }
    }

    bool closed() const { return closed_; }

    // rx_ 앞에서 완성 프레임 1개를 떼어낸다(magic 검증).
    struct Frame {
        std::uint8_t type;
        std::vector<std::byte> body;
    };
    std::optional<Frame> take_frame() {
        if (rx_.size() < frame::header_size) {
            return std::nullopt;
        }
        frame::HeaderBytes hb{};
        std::copy_n(rx_.begin(), frame::header_size, hb.begin());
        auto const parsed_header = frame::parse(hb);
        EXPECT_TRUE(parsed_header);
        if (!parsed_header) {
            return std::nullopt;
        }
        auto const header = *parsed_header;
        std::size_t const total = frame::header_size + header.length;
        if (rx_.size() < total) {
            return std::nullopt;
        }
        auto const off = static_cast<std::ptrdiff_t>(frame::header_size);
        auto const end = static_cast<std::ptrdiff_t>(total);
        Frame f{.type = header.type, .body = {}};
        f.body.assign(rx_.begin() + off, rx_.begin() + end);
        rx_.erase(rx_.begin(), rx_.begin() + end);
        return f;
    }

private:
    int fd_{-1};
    bool closed_{false};
    std::vector<std::byte> rx_;
};

// controller 를 펌프하며 의사-agent 의 응답 프레임을 기다린다.
std::optional<RawAgent::Frame> pump_for_frame(Controller& c, RawAgent& a, int max_iter = 200) {
    for (int i = 0; i < max_iter; ++i) {
        c.run_once(std::chrono::milliseconds{5});
        a.drain();
        if (auto f = a.take_frame()) {
            return f;
        }
    }
    return std::nullopt;
}

} // namespace

TEST(ControllerE2eTest, RegisterRequestGetsSuccessResponse) {
    CaptureSink sink;
    Controller::Config cfg{};
    cfg.listen_port = 0;
    cfg.accept_backlog = 16;
    cfg.log_level = ddcs::logger::Level::Debug;
    cfg.log_sink = &sink;

    Controller controller{cfg};
    controller.start();
    ASSERT_NE(controller.port(), 0);

    RawAgent agent;
    ASSERT_TRUE(agent.connect_to(controller.port()));
    agent.send_message(msg::MessageType::register_request, msg::RegisterRequest{.id = make_uuid(0xab)});

    auto resp = pump_for_frame(controller, agent);
    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ(resp->type, static_cast<std::uint8_t>(msg::MessageType::register_response));

    msg::RegisterResponse decoded{};
    ASSERT_TRUE(msg::decode({resp->body.data(), resp->body.size()}, decoded));
    EXPECT_EQ(decoded.result, msg::RegisterResult::success);

    // controller 측이 실제로 등록을 처리했는지 로그로 교차 확인.
    EXPECT_TRUE(sink.contains("\"msg\":\"agent.register\""));

    controller.stop();
}

// 같은 uuid 로 두 번째 연결이 등록하면 controller 가 옛 연결을 축출(kick-old, new-wins).
TEST(ControllerE2eTest, ReRegisterSameUuidKicksOldConnection) {
    CaptureSink sink;
    Controller::Config cfg{};
    cfg.listen_port = 0;
    cfg.log_level = ddcs::logger::Level::Debug;
    cfg.log_sink = &sink;

    Controller controller{cfg};
    controller.start();

    auto const uuid = make_uuid(0x77);

    RawAgent first;
    ASSERT_TRUE(first.connect_to(controller.port()));
    first.send_message(msg::MessageType::register_request, msg::RegisterRequest{.id = uuid});
    ASSERT_TRUE(pump_for_frame(controller, first).has_value()); // 첫 등록 OK

    RawAgent second;
    ASSERT_TRUE(second.connect_to(controller.port()));
    second.send_message(msg::MessageType::register_request, msg::RegisterRequest{.id = uuid});
    ASSERT_TRUE(pump_for_frame(controller, second).has_value()); // 새 등록 OK

    // 옛 연결(first)은 RST/FIN 으로 닫혀야 한다.
    bool first_closed = false;
    for (int i = 0; i < 200 && !first_closed; ++i) {
        controller.run_once(std::chrono::milliseconds{5});
        first.drain();
        second.drain();
        first_closed = first.closed();
    }
    EXPECT_TRUE(first_closed);
    EXPECT_TRUE(sink.contains("\"msg\":\"agent.kick_old\""));
    EXPECT_FALSE(second.closed()); // 새 연결은 유지

    controller.stop();
}
