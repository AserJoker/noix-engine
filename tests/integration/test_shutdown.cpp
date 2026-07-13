#include <gtest/gtest.h>
#include "EngineFixture.h"
#include <chrono>
#include <thread>

using namespace noix::test;

using ShutdownTest = EngineFixture;

TEST_F(ShutdownTest, ShutdownViaDebugServer) {
    // Initialize session
    auto initResp = _client->post("/debug/initialize",
        R"({"arguments":{"clientName":"gtest","clientVersion":"1.0"}})");
    ASSERT_EQ(initResp.statusCode, 200);

    // Extract sessionId
    std::string sid;
    std::string key = "\"sessionId\":\"";
    size_t pos = initResp.body.find(key);
    ASSERT_NE(pos, std::string::npos);
    pos += key.size();
    size_t end = initResp.body.find('"', pos);
    ASSERT_NE(end, std::string::npos);
    sid = initResp.body.substr(pos, end - pos);

    // Send shutdown command
    auto shutdownResp = _client->post("/debug/command",
        "{\"namespace\":\"noix\",\"command\":\"shutdown\","
        "\"arguments\":{\"sessionId\":\"" + sid + "\"}}");
    EXPECT_EQ(shutdownResp.statusCode, 200);
    EXPECT_NE(shutdownResp.body.find("shutting down"), std::string::npos);

    // Verify engine process exits (TearDown will handle this)
}
