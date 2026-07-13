#include <gtest/gtest.h>
#include "core/NamespacedId.h"

using namespace noix::core;

TEST(NamespacedIdTest, ParseQualified) {
    auto id = NamespacedId::parse("noix:textures/stone");
    EXPECT_EQ(id.ns(), "noix");
    EXPECT_EQ(id.name(), "textures/stone");
}

TEST(NamespacedIdTest, ParseUnqualified) {
    auto id = NamespacedId::parse("textures/stone");
    EXPECT_EQ(id.ns(), "noix");
    EXPECT_EQ(id.name(), "textures/stone");
}

TEST(NamespacedIdTest, ParseWithPathSlashes) {
    auto id = NamespacedId::parse("mymod:a/b/c.png");
    EXPECT_EQ(id.ns(), "mymod");
    EXPECT_EQ(id.name(), "a/b/c.png");
}

TEST(NamespacedIdTest, ParseEmptyStringThrows) {
    EXPECT_THROW(NamespacedId::parse(""), std::invalid_argument);
}

TEST(NamespacedIdTest, ParseOnlyColonThrows) {
    EXPECT_THROW(NamespacedId::parse(":"), std::invalid_argument);
}

TEST(NamespacedIdTest, ParseEmptyNamespaceThrows) {
    EXPECT_THROW(NamespacedId::parse(":name"), std::invalid_argument);
}

TEST(NamespacedIdTest, ParseEmptyNameThrows) {
    EXPECT_THROW(NamespacedId::parse("ns:"), std::invalid_argument);
}

TEST(NamespacedIdTest, TwoArgConstructor) {
    NamespacedId id("mymod", "icon");
    EXPECT_EQ(id.ns(), "mymod");
    EXPECT_EQ(id.name(), "icon");
}

TEST(NamespacedIdTest, OneArgConstructor) {
    NamespacedId id("icon");
    EXPECT_EQ(id.ns(), "noix");
    EXPECT_EQ(id.name(), "icon");
}

TEST(NamespacedIdTest, DefaultConstructor) {
    NamespacedId id;
    EXPECT_EQ(id.ns(), "noix");
    EXPECT_EQ(id.name(), "");
}

TEST(NamespacedIdTest, ToStringQualified) {
    NamespacedId id("mymod", "icon");
    EXPECT_EQ(id.toString(), "mymod:icon");
}

TEST(NamespacedIdTest, ToStringDefault) {
    NamespacedId id("icon");
    EXPECT_EQ(id.toString(), "noix:icon");
}

TEST(NamespacedIdTest, EqualityOperator) {
    EXPECT_EQ(NamespacedId("a", "b"), NamespacedId("a", "b"));
}

TEST(NamespacedIdTest, InequalityOperator) {
    EXPECT_NE(NamespacedId("a", "b"), NamespacedId("a", "c"));
}

TEST(NamespacedIdTest, LessThanOperator) {
    EXPECT_LT(NamespacedId("a", "b"), NamespacedId("a", "c"));
}

TEST(NamespacedIdTest, LessThanOperatorNs) {
    EXPECT_LT(NamespacedId("a", "z"), NamespacedId("b", "a"));
}

TEST(NamespacedIdTest, ParseMultipleColons) {
    auto id = NamespacedId::parse("ns:a:b");
    EXPECT_EQ(id.ns(), "ns");
    EXPECT_EQ(id.name(), "a:b");
}
