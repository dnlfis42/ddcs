#include "ddcs/profile/analysis.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gtest/gtest.h>

namespace {

static_assert(sizeof(DDCS_CTRL_BINARY) > 1);

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        char pattern[] = "/tmp/ddcs-ctrl-profile-e2e.XXXXXX";
        char* const created = ::mkdtemp(pattern);
        if (created == nullptr) {
            throw std::system_error{errno, std::generic_category(), "mkdtemp"};
        }
        path_ = created;
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TemporaryDirectory(TemporaryDirectory const&) = delete;
    TemporaryDirectory& operator=(TemporaryDirectory const&) = delete;

    [[nodiscard]] std::filesystem::path const& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void write_fast_config(std::filesystem::path const& path) {
    std::ofstream output{path};
    if (!output) {
        throw std::runtime_error{"cannot create test Controller config"};
    }

    // 정상 종료 시험을 빠르게 끝내되, main의 실제 file config 경로도 함께 지난다.
    output << R"({"controller":{"sweep_interval_ms":10}})";
    if (!output) {
        throw std::runtime_error{"cannot write test Controller config"};
    }
}

[[nodiscard]] std::string read_file(std::filesystem::path const& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        throw std::runtime_error{"cannot open test output: " + path.string()};
    }

    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

[[noreturn]] void throw_socket_error(std::string const& operation) {
    throw std::system_error{errno, std::generic_category(), operation};
}

[[nodiscard]] std::uint16_t find_unused_port() {
    int const fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throw_socket_error("socket");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        int const saved_errno = errno;
        (void)::close(fd);
        errno = saved_errno;
        throw_socket_error("bind");
    }

    socklen_t length = sizeof(address);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        int const saved_errno = errno;
        (void)::close(fd);
        errno = saved_errno;
        throw_socket_error("getsockname");
    }
    if (::close(fd) != 0) {
        throw_socket_error("close");
    }

    return ntohs(address.sin_port);
}

class PortOccupier {
public:
    PortOccupier() {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) {
            throw_socket_error("socket");
        }

        int const yes = 1;
        if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != 0) {
            int const saved_errno = errno;
            (void)::close(fd_);
            errno = saved_errno;
            throw_socket_error("setsockopt(SO_REUSEADDR)");
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            int const saved_errno = errno;
            (void)::close(fd_);
            errno = saved_errno;
            throw_socket_error("bind");
        }
        if (::listen(fd_, 1) != 0) {
            int const saved_errno = errno;
            (void)::close(fd_);
            errno = saved_errno;
            throw_socket_error("listen");
        }

        socklen_t length = sizeof(address);
        if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
            int const saved_errno = errno;
            (void)::close(fd_);
            errno = saved_errno;
            throw_socket_error("getsockname");
        }
        port_ = ntohs(address.sin_port);
    }

    ~PortOccupier() {
        if (fd_ >= 0) {
            (void)::close(fd_);
        }
    }

    PortOccupier(PortOccupier const&) = delete;
    PortOccupier& operator=(PortOccupier const&) = delete;

    [[nodiscard]] std::uint16_t port() const noexcept {
        return port_;
    }

private:
    int fd_ = -1;
    std::uint16_t port_ = 0;
};

struct EnvironmentValue {
    std::string name;
    std::string value;
};

[[nodiscard]] std::vector<EnvironmentValue> ctrl_environment(
    std::filesystem::path const& config_path, std::filesystem::path const& output_path,
    std::string_view enabled, std::string_view capacity, std::string_view run_id,
    std::uint16_t transport_port, std::uint16_t prometheus_port
) {
    return {
        {"DDCS_CONFIG_PATH", config_path.string()},
        {"DDCS_LOG_LEVEL", "warn"},
        {"DDCS_PROFILE_ENABLED", std::string{enabled}},
        {"DDCS_PROFILE_CAPACITY", std::string{capacity}},
        {"DDCS_PROFILE_OUTPUT_PATH", output_path.string()},
        {"DDCS_PROFILE_RUN_ID", std::string{run_id}},
        {"DDCS_TRANSPORT_PORT", std::to_string(transport_port)},
        {"DDCS_PROMETHEUS_PORT", std::to_string(prometheus_port)},
    };
}

class CtrlProcess {
public:
    explicit CtrlProcess(std::vector<EnvironmentValue> const& environment) {
        int pipe_fds[2]{};
        if (::pipe(pipe_fds) != 0) {
            throw_socket_error("pipe");
        }

        pid_ = ::fork();
        if (pid_ < 0) {
            int const saved_errno = errno;
            (void)::close(pipe_fds[0]);
            (void)::close(pipe_fds[1]);
            errno = saved_errno;
            throw_socket_error("fork");
        }
        if (pid_ == 0) {
            (void)::close(pipe_fds[0]);
            if (::dup2(pipe_fds[1], STDERR_FILENO) < 0) {
                _exit(127);
            }
            if (pipe_fds[1] != STDERR_FILENO) {
                (void)::close(pipe_fds[1]);
            }
            for (auto const& entry : environment) {
                if (::setenv(entry.name.c_str(), entry.value.c_str(), 1) != 0) {
                    _exit(127);
                }
            }

            ::execl(DDCS_CTRL_BINARY, DDCS_CTRL_BINARY, static_cast<char*>(nullptr));
            _exit(127);
        }

        (void)::close(pipe_fds[1]);
        stderr_fd_ = pipe_fds[0];
    }

    ~CtrlProcess() {
        if (!waited_ && pid_ > 0) {
            (void)::kill(pid_, SIGTERM);
            int ignored_status = 0;
            while (::waitpid(pid_, &ignored_status, 0) < 0 && errno == EINTR) {
            }
        }
        if (stderr_fd_ >= 0) {
            (void)::close(stderr_fd_);
        }
    }

    CtrlProcess(CtrlProcess const&) = delete;
    CtrlProcess& operator=(CtrlProcess const&) = delete;

    void terminate() {
        if (pid_ > 0) {
            (void)::kill(pid_, SIGTERM);
        }
    }

    [[nodiscard]] int wait() {
        if (waited_) {
            return status_;
        }

        pid_t result = -1;
        do {
            result = ::waitpid(pid_, &status_, 0);
        } while (result < 0 && errno == EINTR);
        if (result != pid_) {
            throw_socket_error("waitpid");
        }

        waited_ = true;
        return status_;
    }

    [[nodiscard]] std::string stderr_text() {
        if (stderr_fd_ < 0) {
            return stderr_text_;
        }

        std::array<char, 4096> buffer{};
        for (;;) {
            auto const received = ::read(stderr_fd_, buffer.data(), buffer.size());
            if (received > 0) {
                stderr_text_.append(buffer.data(), static_cast<std::size_t>(received));
                continue;
            }
            if (received == 0) {
                break;
            }
            if (errno != EINTR) {
                throw_socket_error("read(stderr)");
            }
        }

        (void)::close(stderr_fd_);
        stderr_fd_ = -1;
        return stderr_text_;
    }

private:
    pid_t pid_ = -1;
    int stderr_fd_ = -1;
    int status_ = 0;
    bool waited_ = false;
    std::string stderr_text_;
};

[[nodiscard]] bool can_connect(std::uint16_t port) {
    int const fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    bool const connected =
        ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
    (void)::close(fd);
    return connected;
}

[[nodiscard]] bool wait_for_listening(std::uint16_t port) {
    using namespace std::chrono_literals;
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (can_connect(port)) {
            return true;
        }
        std::this_thread::sleep_for(10ms);
    }
    return false;
}

TEST(CtrlProfileLifecycleE2eTest, SigtermPublishesARecordingFromTheActualExecutable) {
    using namespace std::chrono_literals;

    TemporaryDirectory directory;
    auto const config = directory.path() / "controller.json";
    auto const output = directory.path() / "tick-profile.json";
    write_fast_config(config);

    auto const transport_port = find_unused_port();
    auto metrics_port = find_unused_port();
    while (metrics_port == transport_port) {
        metrics_port = find_unused_port();
    }

    CtrlProcess ctrl{
        ctrl_environment(config, output, "true", "64", "sigterm-e2e", transport_port, metrics_port)
    };
    ASSERT_TRUE(wait_for_listening(transport_port));
    std::this_thread::sleep_for(80ms);
    ctrl.terminate();

    auto const status = ctrl.wait();
    auto const stderr_text = ctrl.stderr_text();
    ASSERT_TRUE(WIFEXITED(status)) << stderr_text;
    EXPECT_EQ(WEXITSTATUS(status), EXIT_SUCCESS) << stderr_text;

    auto const analysis = ddcs::profile::analyze_recording(read_file(output));
    ASSERT_TRUE(analysis.succeeded());
    EXPECT_EQ(analysis.summary.run_id, "sigterm-e2e");
    EXPECT_GE(analysis.summary.captured, 1u);
    EXPECT_EQ(analysis.summary.dropped, 0u);
    EXPECT_TRUE(analysis.summary.recording_complete);
    EXPECT_TRUE(analysis.summary.recording_origin_utc.has_value());

    struct stat metadata{};
    ASSERT_EQ(::stat(output.c_str(), &metadata), 0);
    EXPECT_EQ(metadata.st_mode & 0777, 0644);
}

TEST(CtrlProfileLifecycleE2eTest, DisabledProfileSkipsItsDependentValidationAndCreatesNoOutput) {
    TemporaryDirectory directory;
    auto const config = directory.path() / "controller.json";
    auto const output = directory.path() / "not-created" / "tick-profile.json";
    write_fast_config(config);

    auto const transport_port = find_unused_port();
    auto metrics_port = find_unused_port();
    while (metrics_port == transport_port) {
        metrics_port = find_unused_port();
    }

    CtrlProcess ctrl{ctrl_environment(
        config, output, "false", "not-an-integer", "ignored", transport_port, metrics_port
    )};
    ASSERT_TRUE(wait_for_listening(transport_port));
    ctrl.terminate();

    auto const status = ctrl.wait();
    auto const stderr_text = ctrl.stderr_text();
    ASSERT_TRUE(WIFEXITED(status)) << stderr_text;
    EXPECT_EQ(WEXITSTATUS(status), EXIT_SUCCESS) << stderr_text;
    EXPECT_FALSE(std::filesystem::exists(output));
    EXPECT_FALSE(std::filesystem::exists(output.parent_path()));
}

TEST(CtrlProfileLifecycleE2eTest, BootFailureStillPublishesAnEmptyProfile) {
    TemporaryDirectory directory;
    auto const config = directory.path() / "controller.json";
    auto const output = directory.path() / "tick-profile.json";
    write_fast_config(config);
    PortOccupier occupied_transport;

    auto metrics_port = find_unused_port();
    while (metrics_port == occupied_transport.port()) {
        metrics_port = find_unused_port();
    }

    CtrlProcess ctrl{ctrl_environment(
        config, output, "true", "8", "boot-failure-e2e", occupied_transport.port(), metrics_port
    )};

    auto const status = ctrl.wait();
    auto const stderr_text = ctrl.stderr_text();
    ASSERT_TRUE(WIFEXITED(status)) << stderr_text;
    EXPECT_EQ(WEXITSTATUS(status), EXIT_FAILURE) << stderr_text;
    EXPECT_NE(stderr_text.find("transport listen port"), std::string::npos);

    auto const analysis = ddcs::profile::analyze_recording(read_file(output));
    ASSERT_TRUE(analysis.succeeded());
    EXPECT_EQ(analysis.summary.run_id, "boot-failure-e2e");
    EXPECT_EQ(analysis.summary.captured, 0u);
    EXPECT_EQ(analysis.summary.dropped, 0u);
    EXPECT_FALSE(analysis.summary.recording_origin_utc.has_value());
}

TEST(CtrlProfileLifecycleE2eTest, DumpFailureDoesNotHideTheOriginalBootFailure) {
    TemporaryDirectory directory;
    auto const config = directory.path() / "controller.json";
    auto const output = directory.path() / "tick-profile.json";
    write_fast_config(config);
    std::filesystem::create_symlink("not-published", output);
    ASSERT_FALSE(
        std::filesystem::exists(output)
    ); // dangling link은 config의 existing-output 검사 통과

    PortOccupier occupied_transport;
    auto metrics_port = find_unused_port();
    while (metrics_port == occupied_transport.port()) {
        metrics_port = find_unused_port();
    }

    CtrlProcess ctrl{ctrl_environment(
        config, output, "true", "8", "error-precedence-e2e", occupied_transport.port(), metrics_port
    )};

    auto const status = ctrl.wait();
    auto const stderr_text = ctrl.stderr_text();
    ASSERT_TRUE(WIFEXITED(status)) << stderr_text;
    EXPECT_EQ(WEXITSTATUS(status), EXIT_FAILURE) << stderr_text;

    auto const dump_error =
        stderr_text.find("profile dump error: profile dump failed: output_already_exists");
    auto const controller_error = stderr_text.find("transport listen port");
    EXPECT_NE(dump_error, std::string::npos);
    EXPECT_NE(controller_error, std::string::npos);
    EXPECT_LT(
        dump_error, controller_error
    ); // secondary dump error 뒤에도 원래 boot 오류가 최종 진단
    EXPECT_TRUE(std::filesystem::is_symlink(output));
    EXPECT_EQ(std::filesystem::read_symlink(output), std::filesystem::path{"not-published"});

    std::size_t directory_entries = 0;
    for (auto const& entry : std::filesystem::directory_iterator{directory.path()}) {
        if (entry.path() != config) {
            ++directory_entries;
        }
    }
    EXPECT_EQ(directory_entries, 1u); // 충돌 symlink만 남고 dump temporary file은 정리돼야 한다.
}

} // namespace
