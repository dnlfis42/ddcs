#include "ddcs/json/value.hpp"
#include "ddcs/profile/dump.hpp"
#include "ddcs/profile/recorder.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <sys/stat.h>

#include <gtest/gtest.h>

namespace {

using ddcs::profile::DumpError;
using ddcs::profile::Recorder;
using ddcs::profile::RunMetadata;
using ddcs::profile::TickOutcome;
using ddcs::profile::TickSample;

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        std::array<char, 32> pattern{"/tmp/ddcs-profile-test.XXXXXX"};
        char* const created = ::mkdtemp(pattern.data());
        if (created == nullptr) {
            throw std::runtime_error{"mkdtemp failed"};
        }

        path_ = created;
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] std::filesystem::path const& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

TickSample sample() {
    return {
        .tick_id = 1,
        .started_ns = 0,
        .command_sweep_ended_ns = 2,
        .session_sweep_ended_ns = 3,
        .policy_evaluate_ended_ns = 5,
        .finished_ns = 5,
        .outcome = TickOutcome::completed,
    };
}

RunMetadata metadata() {
    return {.run_id = "dump-test"};
}

TEST(DumpTest, PublishesAParseableFileOnlyAfterSerializationSucceeds) {
    TemporaryDirectory directory;
    auto const output = directory.path() / "profile.json";
    Recorder recorder{1};
    recorder.record(sample());

    auto const result = ddcs::profile::dump_recording(recorder.finish(), metadata(), output);

    ASSERT_TRUE(result.succeeded());
    ASSERT_TRUE(result.published);
    ASSERT_TRUE(std::filesystem::exists(output));

    std::ifstream input{output};
    std::string const text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    auto const root = ddcs::json::parse(text);
    ASSERT_TRUE(root.has_value());
    ASSERT_EQ(root->find("schema_name")->as_string(), "ddcs.tick_profile");

    struct stat metadata{};
    ASSERT_EQ(::stat(output.c_str(), &metadata), 0);
    EXPECT_EQ(metadata.st_mode & 0777, 0644);
}

TEST(DumpTest, LeavesAnExistingOutputUntouched) {
    TemporaryDirectory directory;
    auto const output = directory.path() / "profile.json";
    {
        std::ofstream existing{output};
        existing << "keep this file";
    }

    Recorder recorder{1};
    recorder.record(sample());
    auto const result = ddcs::profile::dump_recording(recorder.finish(), metadata(), output);

    EXPECT_FALSE(result.succeeded());
    EXPECT_FALSE(result.published);
    EXPECT_EQ(result.error, DumpError::output_already_exists);

    std::ifstream input{output};
    std::string const content{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}
    };
    EXPECT_EQ(content, "keep this file");
}

TEST(DumpTest, ReportsTemporaryFileCreationFailureWithoutPublishing) {
    TemporaryDirectory directory;
    auto const output = directory.path() / "missing" / "profile.json";
    Recorder recorder{1};
    recorder.record(sample());

    auto const result = ddcs::profile::dump_recording(recorder.finish(), metadata(), output);

    EXPECT_FALSE(result.succeeded());
    EXPECT_FALSE(result.published);
    EXPECT_EQ(result.error, DumpError::temporary_file_create_failed);
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(DumpTest, RejectsInvalidRecordingBeforeCreatingAnOutput) {
    TemporaryDirectory directory;
    auto const output = directory.path() / "profile.json";
    Recorder recorder{1};
    auto invalid = sample();
    invalid.tick_id = 0;
    recorder.record(invalid);

    auto const result = ddcs::profile::dump_recording(recorder.finish(), metadata(), output);

    EXPECT_FALSE(result.succeeded());
    EXPECT_FALSE(result.published);
    EXPECT_EQ(result.error, DumpError::invalid_recording);
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(DumpTest, RejectsEmptyRunIdBeforeCreatingAnOutput) {
    TemporaryDirectory directory;
    auto const output = directory.path() / "profile.json";
    Recorder recorder{1};
    recorder.record(sample());

    auto const result =
        ddcs::profile::dump_recording(recorder.finish(), RunMetadata{.run_id = ""}, output);

    EXPECT_FALSE(result.succeeded());
    EXPECT_FALSE(result.published);
    EXPECT_EQ(result.error, DumpError::invalid_metadata);
    EXPECT_FALSE(std::filesystem::exists(output));
}

} // namespace
