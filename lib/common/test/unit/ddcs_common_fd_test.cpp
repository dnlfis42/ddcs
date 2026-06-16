#include "ddcs/common/fd.hpp"

#include <cerrno>
#include <utility>

#include <fcntl.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <gtest/gtest.h>

namespace ddcs::common {

namespace {

Fd make_fd() {
    int const raw = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    EXPECT_NE(raw, Fd::invalid);
    return Fd{raw};
}

bool is_open(int fd) noexcept {
    errno = 0;
    return ::fcntl(fd, F_GETFD) != -1;
}

bool is_closed(int fd) noexcept {
    errno = 0;
    return ::fcntl(fd, F_GETFD) == -1 && errno == EBADF;
}

void close_raw(int fd) noexcept {
    (void)::close(fd);
}

} // namespace

TEST(FdTest, StartsInvalidByDefault) {
    Fd fd;

    EXPECT_EQ(fd.get(), Fd::invalid);
    EXPECT_FALSE(fd.valid());
}

TEST(FdTest, ReportsWrappedDescriptorAsValid) {
    Fd fd = make_fd();

    EXPECT_NE(fd.get(), Fd::invalid);
    EXPECT_TRUE(fd.valid());
    EXPECT_TRUE(is_open(fd.get()));
}

TEST(FdTest, ClosesDescriptorOnDestruction) {
    int raw{Fd::invalid};
    {
        Fd fd = make_fd();
        raw = fd.get();
    }

    EXPECT_TRUE(is_closed(raw));
}

TEST(FdTest, ClosesDescriptorOnReset) {
    Fd fd = make_fd();
    int const raw = fd.get();

    fd.reset();

    EXPECT_EQ(fd.get(), Fd::invalid);
    EXPECT_FALSE(fd.valid());
    EXPECT_TRUE(is_closed(raw));
}

TEST(FdTest, ReplacesDescriptorOnReset) {
    Fd fd = make_fd();
    Fd next = make_fd();
    int const old_raw = fd.get();
    int const next_raw = next.get();

    fd.reset(next.release());

    EXPECT_TRUE(is_closed(old_raw));
    EXPECT_EQ(fd.get(), next_raw);
    EXPECT_TRUE(is_open(next_raw));
}

TEST(FdTest, KeepsDescriptorWhenResetToSameValue) {
    Fd fd = make_fd();
    int const raw = fd.get();

    fd.reset(raw);

    EXPECT_EQ(fd.get(), raw);
    EXPECT_TRUE(is_open(raw));
}

TEST(FdTest, LeavesErrnoUntouchedWhenResettingInvalidDescriptor) {
    Fd fd;
    errno = 0;

    fd.reset();

    EXPECT_EQ(errno, 0);
}

TEST(FdTest, ReleasesDescriptorWithoutClosing) {
    Fd fd = make_fd();
    int const raw = fd.get();

    EXPECT_EQ(fd.release(), raw);

    EXPECT_EQ(fd.get(), Fd::invalid);
    EXPECT_FALSE(fd.valid());
    EXPECT_TRUE(is_open(raw));

    close_raw(raw);
}

TEST(FdTest, TransfersOwnershipOnMoveConstruction) {
    Fd fd = make_fd();
    int const raw = fd.get();

    Fd moved{std::move(fd)};

    EXPECT_EQ(fd.get(), Fd::invalid);
    EXPECT_FALSE(fd.valid());
    EXPECT_EQ(moved.get(), raw);
    EXPECT_TRUE(is_open(raw));
}

TEST(FdTest, TransfersOwnershipOnMoveAssignment) {
    Fd target = make_fd();
    Fd source = make_fd();
    int const target_raw = target.get();
    int const source_raw = source.get();

    target = std::move(source);

    EXPECT_TRUE(is_closed(target_raw));
    EXPECT_EQ(source.get(), Fd::invalid);
    EXPECT_FALSE(source.valid());
    EXPECT_EQ(target.get(), source_raw);
    EXPECT_TRUE(is_open(source_raw));
}

} // namespace ddcs::common
