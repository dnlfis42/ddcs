#include "ddcs/agent/agent.hpp"
#include "ddcs/agent/app/session/session_service.hpp"
#include "ddcs/agent/domain/dummy_device.hpp"
#include "ddcs/common/uuid.hpp"
#include "ddcs/ctrl/controller.hpp"
#include "ddcs/device/mode.hpp"
#include "ddcs/logger/log.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gtest/gtest.h>

namespace {

using State = ddcs::agent::app::session::SessionService::State;

// 양쪽(Controller/Agent) 로그를 한 sink에 모은다.
// 로깅 부트스트랩은 프로세스(여기선 각 테스트) 몫이라, 테스트 시작에 install_logger로
// 전역 로거에 한 번 설치하고 두 facade가 같은 sink를 공유한다.
class CaptureSink : public ddcs::logger::Sink {
public:
    bool contains(std::string_view needle) {
        std::lock_guard<std::mutex> lk{m_};
        return std::any_of(lines_.begin(), lines_.end(), [&](auto const& l) {
            return l.find(needle) != std::string::npos;
        });
    }

    // event_needle을 포함한 로그줄에서 key_needle 바로 뒤의 정수를 순서대로 뽑는다.
    // 예: ints_for(reconnect_scheduled, "\"delay_ms\":") -> 사이클별 backoff delay 수열.
    std::vector<long> ints_for(std::string_view event_needle, std::string_view key_needle) {
        std::lock_guard<std::mutex> lk{m_};
        std::vector<long> out;
        for (auto const& l : lines_) {
            if (l.find(event_needle) == std::string::npos) {
                continue;
            }
            auto const p = l.find(key_needle);
            if (p == std::string::npos) {
                continue;
            }
            out.push_back(std::strtol(l.c_str() + p + key_needle.size(), nullptr, 10));
        }
        return out;
    }

    void write(std::string_view line) noexcept override {
        std::lock_guard<std::mutex> lk{m_};
        lines_.emplace_back(line);
    }

private:
    std::vector<std::string> lines_;
    std::mutex m_;
};

ddcs::common::Uuid make_uuid(std::uint8_t seed) {
    std::array<std::byte, 16> b{};
    b.fill(std::byte{seed});
    return ddcs::common::Uuid{b};
}

// 단일 스레드로 양 reactor를 번갈아 펌프하며 pred를 기다린다.
template <typename Pred>
bool pump_until(ddcs::ctrl::Controller& c, ddcs::agent::Agent& a, Pred pred, int max_iter = 400) {
    for (int i = 0; i < max_iter; ++i) {
        if (pred()) {
            return true;
        }
        c.run_once(std::chrono::milliseconds{2});
        a.run_once(std::chrono::milliseconds{2});
    }
    return pred();
}

// 전역 로거를 테스트 sink로 세운다(debug: reconnect_scheduled 등 관측). main과 같은 수순.
void install_logger(CaptureSink& sink) {
    auto& lg = ddcs::logger::Logger::instance();
    lg.set_level(ddcs::logger::Level::debug);
    lg.set_sink(sink);
}

std::unique_ptr<ddcs::ctrl::Controller> make_controller(std::filesystem::path policy_path = {}) {
    ddcs::ctrl::Controller::Config cfg{};
    cfg.listen_port = 0;
    cfg.liveness_timeout = std::chrono::milliseconds{300};
    cfg.sweep_interval = std::chrono::milliseconds{20}; // command/liveness/policy sweep을 촘촘히
    if (!policy_path.empty()) {
        cfg.policy_path = std::move(policy_path);
    }
    return std::make_unique<ddcs::ctrl::Controller>(cfg);
}

std::unique_ptr<ddcs::agent::Agent>
make_agent(std::uint16_t port, std::uint8_t uuid_seed, std::string group = {}) {
    ddcs::agent::Agent::Config cfg{};
    cfg.controller_host = "127.0.0.1";
    cfg.controller_port = port;
    cfg.session.heartbeat = std::chrono::milliseconds{50};
    cfg.session.status_report = std::chrono::milliseconds{50};
    cfg.session.register_timeout = std::chrono::milliseconds{500};
    cfg.session.group = std::move(group); // 정책 타깃팅용 그룹 선언
    return std::make_unique<ddcs::agent::Agent>(
        std::move(cfg), std::make_unique<ddcs::agent::domain::DummyDevice>(make_uuid(uuid_seed))
    );
}

// 단일 그룹 정책을 temp 파일로 떨군다. low_load 위(>0)라 load=0인 DummyDevice는 idle로 떨어진다.
std::filesystem::path write_idle_policy(std::string_view group) {
    auto const path = std::filesystem::temp_directory_path() / "ddcs_e2e_policy.json";
    std::ofstream f{path};
    f << R"({"policy":{"groups":{")" << group
      << R"(":{"high_load":10.0,"low_load":5.0,"high_load_mode":"safe","low_load_mode":"performance"}}}})";
    return path;
}

// "귀먹은" 컨트롤러 대역: TCP는 accept하되 RegisterOutcome을 영영 안 보낸다(handshake 미완).
// issue-03 상황을 진짜 소켓으로 재현한다 -- 에이전트는 register_timeout으로 끊고 backoff 재연결을
// 반복하므로, 에이전트 쪽엔 어떤 결함 플래그도 필요 없다(오작동하는 쪽은 peer다).
class DeafListener {
public:
    DeafListener() {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int yes = 1;
        (void)::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0; // ephemeral
        (void)::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        (void)::listen(fd_, 16);
        socklen_t len = sizeof(addr);
        (void)::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        set_nonblock(fd_);
    }
    ~DeafListener() {
        for (int c : conns_) {
            if (c >= 0) {
                ::close(c);
            }
        }
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }
    DeafListener(DeafListener const&) = delete;
    DeafListener& operator=(DeafListener const&) = delete;

    std::uint16_t port() const noexcept {
        return port_;
    }

    // 대기 중 연결을 accept하되 절대 응답하지 않고, 에이전트가 끊은 연결은 reap한다.
    void poll() {
        for (;;) {
            int const c = ::accept(fd_, nullptr, nullptr);
            if (c < 0) {
                break;
            }
            set_nonblock(c);
            conns_.push_back(c);
        }
        for (auto& c : conns_) {
            if (c < 0) {
                continue;
            }
            char buf[256];
            for (;;) {
                ssize_t const n = ::recv(c, buf, sizeof(buf), 0);
                if (n > 0) {
                    continue; // RegisterRequest 바이트 -> 그냥 버린다(응답 안 함)
                }
                if (n == 0) {
                    ::close(c); // 에이전트가 close(register_timeout) -> reap
                    c = -1;
                }
                break; // EAGAIN 또는 닫음
            }
        }
    }

private:
    static void set_nonblock(int fd) {
        int const fl = ::fcntl(fd, F_GETFL, 0);
        ::fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    }
    int fd_ = -1;
    std::uint16_t port_ = 0;
    std::vector<int> conns_;
};

} // namespace

TEST(AgentControllerE2eTest, AgentRegistersAndReachesActive) {
    CaptureSink sink;
    install_logger(sink);
    auto controller = make_controller();
    controller->start();
    auto agent = make_agent(controller->port(), 0xab);
    agent->start();

    bool const active =
        pump_until(*controller, *agent, [&] { return agent->session().state() == State::active; });

    ASSERT_TRUE(active);
    EXPECT_TRUE(sink.contains(R"("event":"session.connection.register.accept")")
    ); // controller가 등록 확정
    EXPECT_TRUE(sink.contains(R"("event":"session.connection.register.success")")
    ); // agent가 응답 수신
}

TEST(AgentControllerE2eTest, HeartbeatKeepsAgentAliveOverTime) {
    CaptureSink sink;
    install_logger(sink);
    auto controller = make_controller();
    controller->start();
    auto agent = make_agent(controller->port(), 0xcd);
    agent->start();

    ASSERT_TRUE(pump_until(*controller, *agent, [&] {
        return agent->session().state() == State::active;
    }));

    // ~500ms 펌프 - heartbeat(50ms)가 controller liveness(300ms)를 계속 리셋
    auto const start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds{500}) {
        controller->run_once(std::chrono::milliseconds{2});
        agent->run_once(std::chrono::milliseconds{2});
    }

    EXPECT_EQ(agent->session().state(), State::active);
    EXPECT_FALSE(sink.contains(R"("reason":"liveness_expired")")); // 축출 안 됨
}

// 정책 경로로 c->a Command가 왕복한다(operator API 없음 - load 임계 전환이 명령을 낸다).
// load=0인 device가 low_load 밴드 아래라 controller가 idle_mode SetMode를 발신 -> agent 적용 ->
// outcome 회신.
TEST(AgentControllerE2eTest, PolicyCommandRoundTrips) {
    CaptureSink sink;
    install_logger(sink);
    auto const policy = write_idle_policy("edge");
    auto controller = make_controller(policy);
    controller->start();
    auto agent = make_agent(controller->port(), 0xef, "edge");
    agent->start();

    bool const round_trip = pump_until(*controller, *agent, [&] {
        return sink.contains(R"("event":"session.command.apply")") // agent가 명령 적용
               && sink.contains(R"("event":"command.complete")");  // controller가 성공 종결
    });

    EXPECT_TRUE(round_trip);
    EXPECT_EQ(agent->session().state(), State::active); // 명령 후에도 세션 유지
}

// controller가 사라지면 agent가 backoff 후 같은 포트의 새 controller로 자동 재접속/재등록
TEST(AgentControllerE2eTest, AgentReconnectsAfterControllerDrop) {
    CaptureSink sink;
    install_logger(sink);
    auto controller = make_controller();
    controller->start();
    auto const port = controller->port();
    auto agent = make_agent(port, 0x5a);
    agent->start();

    ASSERT_TRUE(pump_until(*controller, *agent, [&] {
        return agent->session().state() == State::active;
    }));

    // controller 파괴 -> agent 연결 끊김(FIN)
    controller.reset();
    for (int i = 0; i < 100 && agent->session().state() == State::active; ++i) {
        agent->run_once(std::chrono::milliseconds{2});
    }
    EXPECT_NE(agent->session().state(), State::active); // 끊김 감지 -> idle

    // 같은 포트로 새 controller (Acceptor SO_REUSEADDR)
    ddcs::ctrl::Controller::Config ccfg{};
    ccfg.listen_port = port;
    ccfg.liveness_timeout = std::chrono::milliseconds{300};
    auto controller2 = std::make_unique<ddcs::ctrl::Controller>(ccfg);
    controller2->start();

    // backoff(첫 지연 ~1s) 후 재접속/재등록 -> 다시 active. 넉넉히 펌프
    bool const reactive = pump_until(
        *controller2, *agent, [&] { return agent->session().state() == State::active; }, 1500
    );
    EXPECT_TRUE(reactive);
}

// issue-03 회귀(런타임): 컨트롤러가 TCP는 받아주되 registration을 끝내주지 않으면, 에이전트의
// 재연결 backoff가 base에 묶이지 말고 지수적으로 자라야 한다(thundering-herd 방지). 유닛 테스트
// BackoffGrowsWhileRegistrationNeverSucceeds는 fake로 보지만, 여기선 진짜 소켓 + 진짜 reconnect
// 타이머로 끝까지 돌린다. (backoff reset은 registration 성공 시에만 -> 여기선 영영 안 일어남.)
TEST(AgentControllerE2eTest, BackoffGrowsWhenPeerAcceptsButNeverRegisters) {
    using namespace std::chrono_literals;
    CaptureSink sink;
    install_logger(sink); // reconnect_scheduled(DEBUG)가 sink에 보이게
    DeafListener deaf;    // accept-but-never-register

    long const base_ms = 20;
    ddcs::agent::Agent::Config cfg{};
    cfg.controller_host = "127.0.0.1";
    cfg.controller_port = deaf.port(); // 진짜 컨트롤러 대신 귀먹은 리스너로
    cfg.reconnect_base_delay = std::chrono::milliseconds{base_ms};
    cfg.reconnect_max_delay = std::chrono::milliseconds{640}; // 여러 단계 자랄 여유(cap)
    cfg.session.register_timeout = std::chrono::milliseconds{60}; // 빨리 timeout -> 사이클 빠르게
    cfg.session.heartbeat = std::chrono::milliseconds{50};
    cfg.session.status_report = std::chrono::milliseconds{50};

    ddcs::agent::Agent agent{
        std::move(cfg), std::make_unique<ddcs::agent::domain::DummyDevice>(make_uuid(0xef))
    };
    agent.start();

    // 여러 재연결 사이클을 모은다(각 ~ register_timeout + backoff). 실시간 timerfd라 펌프로 시간을
    // 흘린다.
    auto const event = R"("event":"transport.reconnect.schedule")";
    auto const key = R"("delay_ms":)";
    auto const deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline && sink.ints_for(event, key).size() < 6) {
        agent.run_once(std::chrono::milliseconds{2});
        deaf.poll();
    }

    auto const delays = sink.ints_for(event, key);
    ASSERT_GE(delays.size(), 5U) << "재연결 사이클이 충분히 안 모임 (peer 동작 확인 필요)";

    // 버그(reset-on-TCP-connect)면 매 사이클 attempt=0이라 delay가 base(+-25%)에 묶여 ~base다.
    // 정상이면 base * 2^attempt로 자라 base를 훌쩍 넘는다.
    long const max_delay = *std::max_element(delays.begin(), delays.end());
    EXPECT_GE(max_delay, 4 * base_ms)
        << "backoff가 base에 묶임(지수 성장 실패) -- issue-03 회귀. delays[0]=" << delays.front();
    EXPECT_GE(delays[4], 2 * delays[0]) << "후반 delay가 초반보다 확연히 커야 한다(성장 추세)";

    ddcs::logger::Logger::instance().clear_sink(sink); // stack-local sink dangling 방지
}
