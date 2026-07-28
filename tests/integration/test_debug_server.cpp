#include <gtest/gtest.h>
#include "EngineFixture.h"

using namespace noix::test;

using HttpServerTest = EngineFixture;

TEST_F(HttpServerTest, SystemPing) {
    auto resp = _client->post("/api/v1/system/ping", "{}");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("ok"), std::string::npos);
}

TEST_F(HttpServerTest, SystemInfo) {
    auto resp = _client->post("/api/v1/system/info", "{}");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("noix-engine"), std::string::npos);
    EXPECT_NE(resp.body.find("api"), std::string::npos);
}

TEST_F(HttpServerTest, SystemSchemaPing) {
    auto resp = _client->post("/api/v1/system/schema", "{\"name\":\"system/ping\"}");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("system/ping"), std::string::npos);
    EXPECT_NE(resp.body.find("request"), std::string::npos);
    EXPECT_NE(resp.body.find("response"), std::string::npos);
}

TEST_F(HttpServerTest, SystemSchemaUnknown) {
    auto resp = _client->post("/api/v1/system/schema", "{\"name\":\"nonexistent\"}");
    EXPECT_NE(resp.body.find("unknown endpoint"), std::string::npos);
}

TEST_F(HttpServerTest, SystemShutdown) {
    auto resp = _client->post("/api/v1/system/shutdown", "{}");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("shutting-down"), std::string::npos);
}

TEST_F(HttpServerTest, NotFound) {
    auto resp = _client->post("/api/v1/nonexistent", "{}");
    EXPECT_EQ(resp.statusCode, 404);
}

TEST_F(HttpServerTest, OldDebugPathsNotFound) {
    auto resp1 = _client->post("/debug/ping", "{}");
    EXPECT_EQ(resp1.statusCode, 404);
}
