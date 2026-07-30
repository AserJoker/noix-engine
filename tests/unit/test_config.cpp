#include <gtest/gtest.h>
#include "core/Value.h"

using namespace noix::core;

TEST(Value, DefaultConstructIsNull) {
    Value v;
    EXPECT_TRUE(v.isNull());
}

TEST(Value, BoolValue) {
    Value v(true);
    EXPECT_TRUE(v.isBool());
    EXPECT_TRUE(v.asBool());
}

TEST(Value, IntValue) {
    Value v(42);
    EXPECT_TRUE(v.isNumber());
    EXPECT_EQ(v.asInt(), 42);
}

TEST(Value, DoubleValue) {
    Value v(3.14);
    EXPECT_TRUE(v.isNumber());
    EXPECT_DOUBLE_EQ(v.asDouble(), 3.14);
}

TEST(Value, StringValue) {
    Value v("hello");
    EXPECT_TRUE(v.isString());
    EXPECT_EQ(v.asString(), "hello");
}

TEST(Value, ObjectValue) {
    Value v = Value::object();
    EXPECT_TRUE(v.isObject());
}

TEST(Value, ArrayValue) {
    Value v = Value::array();
    EXPECT_TRUE(v.isArray());
}

TEST(Value, ObjectHasAndGet) {
    Value v = Value::object();
    v.asObject()["port"] = 9900;
    v.asObject()["host"] = "localhost";
    EXPECT_TRUE(v.has("port"));
    EXPECT_FALSE(v.has("missing"));
    EXPECT_EQ(v["port"].asInt(), 9900);
    EXPECT_EQ(v["host"].asString(), "localhost");
}

TEST(Value, ObjectOverwrite) {
    Value v = Value::object();
    v.asObject()["port"] = 9900;
    v.asObject()["port"] = 8080;
    EXPECT_EQ(v["port"].asInt(), 8080);
}

TEST(Value, NestedObject) {
    Value v = Value::object();
    Value window = Value::object();
    window.asObject()["mode"] = "windowed";
    v.asObject()["window"] = std::move(window);
    EXPECT_TRUE(v["window"].isObject());
    EXPECT_EQ(v["window"]["mode"].asString(), "windowed");
}

TEST(Value, RemoveKey) {
    Value v = Value::object();
    v.asObject()["port"] = 9900;
    EXPECT_TRUE(v.has("port"));
    v.asObject().erase("port");
    EXPECT_FALSE(v.has("port"));
}

TEST(Value, DumpAndParse) {
    Value v = Value::object();
    v.asObject()["host"] = "localhost";
    v.asObject()["port"] = 9900;
    std::string json = v.dump();
    EXPECT_NE(json.find("\"host\""), std::string::npos);
    EXPECT_NE(json.find("9900"), std::string::npos);

    Value parsed = Value::parse(json);
    EXPECT_TRUE(parsed.isObject());
    EXPECT_EQ(parsed["host"].asString(), "localhost");
    EXPECT_EQ(parsed["port"].asInt(), 9900);
}

TEST(Value, AccessOnNonObjectReturnsNull) {
    Value v(42);
    EXPECT_FALSE(v.has("key"));
    EXPECT_TRUE(v["key"].isNull());
}
