#include <gtest/gtest.h>
#include "core/Config.h"

using namespace noix::core;

TEST(Config, DefaultConstructIsNotEmpty) {
    Config cfg;
    EXPECT_TRUE(static_cast<bool>(cfg));
}

TEST(Config, HasKey) {
    Config cfg;
    cfg.setInt("port", 9900);
    EXPECT_TRUE(cfg.has("port"));
    EXPECT_FALSE(cfg.has("host"));
}

TEST(Config, SetAndGetString) {
    Config cfg;
    cfg.setString("host", "localhost");
    auto val = cfg.getString("host");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), "localhost");
}

TEST(Config, GetStringDefault) {
    Config cfg;
    EXPECT_EQ(cfg.getString("missing", "fallback"), "fallback");
}

TEST(Config, SetAndGetInt) {
    Config cfg;
    cfg.setInt("port", 9900);
    auto val = cfg.getInt("port");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), 9900);
}

TEST(Config, GetIntDefault) {
    Config cfg;
    EXPECT_EQ(cfg.getInt("missing", 8080), 8080);
}

TEST(Config, SetAndGetDouble) {
    Config cfg;
    cfg.setDouble("ratio", 3.14);
    auto val = cfg.getDouble("ratio");
    ASSERT_TRUE(val.has_value());
    EXPECT_DOUBLE_EQ(val.value(), 3.14);
}

TEST(Config, SetAndGetBool) {
    Config cfg;
    cfg.setBool("enabled", true);
    EXPECT_TRUE(cfg.getBool("enabled").value_or(false));
    cfg.setBool("enabled", false);
    EXPECT_FALSE(cfg.getBool("enabled").value_or(true));
}

TEST(Config, GetBoolDefault) {
    Config cfg;
    EXPECT_TRUE(cfg.getBool("missing", true));
    EXPECT_FALSE(cfg.getBool("missing", false));
}

TEST(Config, MissingKeyReturnsNullopt) {
    Config cfg;
    EXPECT_FALSE(cfg.getString("nokey").has_value());
    EXPECT_FALSE(cfg.getInt("nokey").has_value());
    EXPECT_FALSE(cfg.getDouble("nokey").has_value());
    EXPECT_FALSE(cfg.getBool("nokey").has_value());
}

TEST(Config, TypeMismatchReturnsNullopt) {
    Config cfg;
    cfg.setString("name", "hello");
    EXPECT_FALSE(cfg.getInt("name").has_value());
    EXPECT_FALSE(cfg.getBool("name").has_value());

    cfg.setInt("count", 42);
    EXPECT_FALSE(cfg.getString("count").has_value());
}

TEST(Config, SetObject) {
    Config child;
    child.setString("ip", "127.0.0.1");
    child.setInt("port", 8080);

    Config cfg;
    cfg.setObject("server", std::move(child));

    auto server = cfg.getObject("server");
    ASSERT_TRUE(static_cast<bool>(server));
    EXPECT_EQ(server.getString("ip").value_or(""), "127.0.0.1");
    EXPECT_EQ(server.getInt("port").value_or(0), 8080);
}

TEST(Config, GetObjectMissing) {
    Config cfg;
    auto obj = cfg.getObject("missing");
    EXPECT_FALSE(static_cast<bool>(obj));
}

TEST(Config, Remove) {
    Config cfg;
    cfg.setInt("port", 9900);
    EXPECT_TRUE(cfg.remove("port"));
    EXPECT_FALSE(cfg.has("port"));
    EXPECT_FALSE(cfg.remove("port")); // already removed
}

TEST(Config, RemoveMissingKey) {
    Config cfg;
    EXPECT_FALSE(cfg.remove("nonexistent"));
}

TEST(Config, OverwriteValue) {
    Config cfg;
    cfg.setInt("port", 9900);
    cfg.setInt("port", 8080);
    EXPECT_EQ(cfg.getInt("port").value_or(0), 8080);
}

TEST(Config, OverwriteString) {
    Config cfg;
    cfg.setString("host", "localhost");
    cfg.setString("host", "example.com");
    EXPECT_EQ(cfg.getString("host").value_or(""), "example.com");
}

TEST(Config, ToJson) {
    Config cfg;
    cfg.setString("host", "localhost");
    cfg.setInt("port", 9900);
    std::string json = cfg.toJson();
    EXPECT_NE(json.find("\"host\""), std::string::npos);
    EXPECT_NE(json.find("\"port\""), std::string::npos);
    EXPECT_NE(json.find("localhost"), std::string::npos);
    EXPECT_NE(json.find("9900"), std::string::npos);
}

TEST(Config, MoveConstruct) {
    Config cfg;
    cfg.setInt("port", 9900);
    Config moved = std::move(cfg);
    EXPECT_EQ(moved.getInt("port").value_or(0), 9900);
}

TEST(Config, CopyConstruct) {
    Config cfg;
    cfg.setInt("port", 9900);
    Config copy(cfg);
    EXPECT_EQ(copy.getInt("port").value_or(0), 9900);
    // original still valid
    EXPECT_EQ(cfg.getInt("port").value_or(0), 9900);
}

TEST(Config, MoveAssign) {
    Config cfg;
    cfg.setInt("port", 9900);
    Config other;
    other = std::move(cfg);
    EXPECT_EQ(other.getInt("port").value_or(0), 9900);
}

TEST(Config, CopyAssign) {
    Config cfg;
    cfg.setInt("port", 9900);
    Config other;
    other = cfg;
    EXPECT_EQ(other.getInt("port").value_or(0), 9900);
}
