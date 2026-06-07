#include "ddcs/common/throw_errno.hpp"

#include <cerrno>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

namespace ddcs::common {

TEST(ThrowErrnoTest, ThrowsSystemErrorWithErrnoAndContext) {
    try {
        throw_errno(EINVAL, "test context");
    } catch (std::system_error const& e) {
        EXPECT_EQ(e.code().value(), EINVAL);
        EXPECT_EQ(e.code().category(), std::system_category());
        EXPECT_NE(std::string{e.what()}.find("test context"), std::string::npos);
        return;
    }

    FAIL() << "throw_errno did not throw std::system_error";
}

} // namespace ddcs::common
