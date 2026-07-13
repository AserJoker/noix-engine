#include <gtest/gtest.h>
#include "EngineFixture.h"

using namespace noix::test;

using JsEvalTest = EngineFixture;

TEST_F(JsEvalTest, EvalArithmetic) {
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

    // Eval expression
    auto evalResp = _client->post("/debug/command",
        "{\"namespace\":\"noix\",\"command\":\"exec-script\","
        "\"arguments\":{\"expr\":\"40+2\",\"sessionId\":\"" + sid + "\"}}");
    EXPECT_EQ(evalResp.statusCode, 200);
    EXPECT_NE(evalResp.body.find("42"), std::string::npos);
}

TEST_F(JsEvalTest, EvalSyntaxError) {
    auto initResp = _client->post("/debug/initialize",
        R"({"arguments":{"clientName":"gtest","clientVersion":"1.0"}})");
    ASSERT_EQ(initResp.statusCode, 200);

    std::string sid;
    std::string key = "\"sessionId\":\"";
    size_t pos = initResp.body.find(key);
    ASSERT_NE(pos, std::string::npos);
    pos += key.size();
    size_t end = initResp.body.find('"', pos);
    ASSERT_NE(end, std::string::npos);
    sid = initResp.body.substr(pos, end - pos);

    auto evalResp = _client->post("/debug/command",
        "{\"namespace\":\"noix\",\"command\":\"exec-script\","
        "\"arguments\":{\"expr\":\"!!!invalid!!!\",\"sessionId\":\"" + sid + "\"}}");
    EXPECT_EQ(evalResp.statusCode, 200);
    // Should return some error, not crash
    EXPECT_FALSE(evalResp.body.empty());
}
