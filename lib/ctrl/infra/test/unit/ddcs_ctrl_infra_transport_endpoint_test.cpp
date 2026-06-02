#include "ddcs/ctrl/infra/transport/endpoint.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string_view>

namespace {

using ddcs::ctrl::infra::transport::Endpoint;

} // namespace

TEST(EndpointTest, FormatsV4) {
    Endpoint ep{};
    ep.family = Endpoint::Family::v4;
    ep.port = 8080;
    ep.addr = {192, 168, 0, 1};
    std::array<char, 64> buf{};
    EXPECT_EQ(ep.format(buf), std::string_view{"192.168.0.1:8080"});
}

TEST(EndpointTest, FormatsV6) {
    Endpoint ep{};
    ep.family = Endpoint::Family::v6;
    ep.port = 443;
    ep.addr = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}; // ::1
    std::array<char, 64> buf{};
    EXPECT_EQ(ep.format(buf), std::string_view{"[::1]:443"});
}

TEST(EndpointTest, ReturnsEmptyForNoneFamily) {
    Endpoint const ep{};
    std::array<char, 64> buf{};
    EXPECT_TRUE(ep.format(buf).empty());
}

TEST(EndpointTest, ReturnsEmptyForUndersizedBuffer) {
    Endpoint ep{};
    ep.family = Endpoint::Family::v4;
    ep.addr = {127, 0, 0, 1};
    std::array<char, 8> small{}; // < endpoint_format_min_size
    EXPECT_TRUE(ep.format(small).empty());
}

TEST(EndpointTest, ResetClearsFields) {
    Endpoint ep{};
    ep.family = Endpoint::Family::v4;
    ep.port = 1234;
    ep.addr = {10, 0, 0, 1};
    ep.reset();
    EXPECT_EQ(ep, Endpoint{});
}
