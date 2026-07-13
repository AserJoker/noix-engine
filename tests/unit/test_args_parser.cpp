#include <gtest/gtest.h>
#include "core/ArgsParser.h"

using namespace noix::core;

TEST(ArgsParserTest, ParseLongFlagWithValue) {
    ArgsParser parser;
    const char* argv[] = {"prog", "--debug-port=9901"};
    parser.parse(2, const_cast<char**>(argv));

    EXPECT_TRUE(parser.has("debug-port"));
    EXPECT_EQ(parser.get("debug-port"), "9901");
}

TEST(ArgsParserTest, ParseShortFlagWithValue) {
    ArgsParser parser;
    const char* argv[] = {"prog", "-p", "9901"};
    parser.parse(3, const_cast<char**>(argv));

    EXPECT_TRUE(parser.has("p"));
    EXPECT_EQ(parser.get("p"), "9901");
}

TEST(ArgsParserTest, ParseFlagWithoutValue) {
    ArgsParser parser;
    const char* argv[] = {"prog", "--headless"};
    parser.parse(2, const_cast<char**>(argv));

    EXPECT_TRUE(parser.has("headless"));
    EXPECT_EQ(parser.get("headless"), "");
}

TEST(ArgsParserTest, PositionalArgs) {
    ArgsParser parser;
    const char* argv[] = {"prog", "file1.txt", "file2.txt"};
    parser.parse(3, const_cast<char**>(argv));

    EXPECT_EQ(parser.positional().size(), 2u);
    EXPECT_EQ(parser.positional()[0], "file1.txt");
    EXPECT_EQ(parser.positional()[1], "file2.txt");
}

TEST(ArgsParserTest, MissingKeyReturnsDefault) {
    ArgsParser parser;
    const char* argv[] = {"prog"};
    parser.parse(1, const_cast<char**>(argv));

    EXPECT_FALSE(parser.has("nonexistent"));
    EXPECT_EQ(parser.get("nonexistent"), "");
    EXPECT_EQ(parser.get("nonexistent", "fallback"), "fallback");
}

TEST(ArgsParserTest, MixedArgs) {
    ArgsParser parser;
    const char* argv[] = {"prog", "--headless", "-p", "9901", "input.txt"};
    parser.parse(5, const_cast<char**>(argv));

    EXPECT_TRUE(parser.has("headless"));
    EXPECT_TRUE(parser.has("p"));
    EXPECT_EQ(parser.get("p"), "9901");
    EXPECT_EQ(parser.positional().size(), 1u);
    EXPECT_EQ(parser.positional()[0], "input.txt");
}

TEST(ArgsParserTest, LongFlagWithSpaceValue) {
    ArgsParser parser;
    const char* argv[] = {"prog", "--debug-port", "9901"};
    parser.parse(3, const_cast<char**>(argv));

    EXPECT_TRUE(parser.has("debug-port"));
    EXPECT_EQ(parser.get("debug-port"), "9901");
}
