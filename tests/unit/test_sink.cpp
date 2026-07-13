#include <gtest/gtest.h>
#include "core/Sink.h"
#include <cstdio>
#include <fstream>

using namespace noix::core;

static std::string readFirstLine(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::string line;
    std::getline(file, line);
    return line;
}

static std::string readLine(const std::string& path, int lineNum) {
    std::ifstream file(path);
    if (!file.is_open()) return "";
    std::string line;
    for (int i = 0; i < lineNum; ++i) {
        if (!std::getline(file, line)) return "";
    }
    return line;
}

TEST(FileSinkTest, WriteAndFlush) {
    const char* path = "test_sink_output.log";
    std::remove(path);

    {
        FileSink sink(path);
        sink.write(LogLevel::Info, "hello world");
        sink.flush();
    }
    // FileSink destructor has closed the file now

    EXPECT_EQ(readFirstLine(path), "[INFO] hello world");
    std::remove(path);
}

TEST(FileSinkTest, AppendMultipleLines) {
    const char* path = "test_sink_append.log";
    std::remove(path);

    {
        FileSink sink(path);
        sink.write(LogLevel::Info, "line 1");
        sink.write(LogLevel::Warn, "line 2");
        sink.write(LogLevel::Error, "line 3");
        sink.flush();
    }

    EXPECT_EQ(readLine(path, 1), "[INFO] line 1");
    EXPECT_EQ(readLine(path, 2), "[WARN] line 2");
    EXPECT_EQ(readLine(path, 3), "[ERROR] line 3");
    std::remove(path);
}

TEST(FileSinkTest, FileCreation) {
    const char* path = "test_sink_new.log";
    std::remove(path);

    {
        FileSink sink(path);
        sink.write(LogLevel::Info, "test");
        sink.flush();
    }

    std::ifstream file(path);
    EXPECT_TRUE(file.is_open());
    std::remove(path);
}

TEST(ConsoleSinkTest, WriteDoesNotCrash) {
    ConsoleSink sink;
    sink.write(LogLevel::Info, "test message");
    sink.write(LogLevel::Error, "error message");
    sink.flush();
}
