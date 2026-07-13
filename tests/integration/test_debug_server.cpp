#include <gtest/gtest.h>
#include "EngineFixture.h"

using namespace noix::test;

using DebugServerTest = EngineFixture;

TEST_F(DebugServerTest, HandshakeReturnsEngineInfo) {
    auto resp = _client->get("/debug/handshake");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("noix-engine"), std::string::npos);
}

TEST_F(DebugServerTest, PingReturnsPong) {
    auto resp = _client->get("/debug/ping");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("pong"), std::string::npos);
}

TEST_F(DebugServerTest, InitializeCreatesSession) {
    auto resp = _client->post("/debug/initialize",
        R"({"arguments":{"clientName":"gtest","clientVersion":"1.0"}})");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("sessionId"), std::string::npos);
    EXPECT_NE(resp.body.find("debug:eval"), std::string::npos);
    EXPECT_NE(resp.body.find("debug:shutdown"), std::string::npos);
}

TEST_F(DebugServerTest, StatusWithoutSession) {
    auto resp = _client->get("/debug/status");
    EXPECT_EQ(resp.statusCode, 200);
}

TEST_F(DebugServerTest, CommandWithoutSessionReturnsError) {
    auto resp = _client->post("/debug/command",
        R"({"namespace":"debug","command":"eval","arguments":{"expr":"1+1"}})");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("session expired"), std::string::npos);
}

TEST_F(DebugServerTest, FullProtocolFlow) {
    // Initialize
    auto initResp = _client->post("/debug/initialize",
        R"({"arguments":{"clientName":"gtest","clientVersion":"1.0"}})");
    EXPECT_EQ(initResp.statusCode, 200);

    // Extract sessionId
    std::string sid;
    std::string key = "\"sessionId\":\"";
    size_t pos = initResp.body.find(key);
    ASSERT_NE(pos, std::string::npos);
    pos += key.size();
    size_t end = initResp.body.find('"', pos);
    ASSERT_NE(end, std::string::npos);
    sid = initResp.body.substr(pos, end - pos);
    ASSERT_FALSE(sid.empty());

    // Status with session
    auto statusResp = _client->get("/debug/status");
    EXPECT_EQ(statusResp.statusCode, 200);

    // Disconnect
    auto discResp = _client->post("/debug/disconnect",
        "{\"sessionId\":\"" + sid + "\"}");
    EXPECT_EQ(discResp.statusCode, 200);
}
