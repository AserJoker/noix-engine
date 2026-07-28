#include <gtest/gtest.h>
#include "EngineFixture.h"

using namespace noix::test;

using DebugServerTest = EngineFixture;

TEST_F(DebugServerTest, PingReturnsOk) {
    auto resp = _client->get("/debug/ping");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("ok"), std::string::npos);
}

TEST_F(DebugServerTest, HandshakeReturnsEngineInfo) {
    auto resp = _client->get("/debug/handshake");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("noix-engine"), std::string::npos);
}

TEST_F(DebugServerTest, StatusReturnsRunning) {
    auto resp = _client->get("/debug/status");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("running"), std::string::npos);
}

TEST_F(DebugServerTest, UnknownPathReturnsHint) {
    auto resp = _client->get("/debug/unknown");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("DAP"), std::string::npos);
}
