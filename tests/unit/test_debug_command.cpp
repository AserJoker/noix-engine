#include <gtest/gtest.h>
#include "debug/DebugControlCommand.h"
#include "debug/DebugEvalCommand.h"
#include "debug/DebugStatusCommand.h"
#include "debug/BreakpointCommand.h"
#include "debug/StackTraceCommand.h"
#include "script/ScriptEngine.h"
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>

using namespace noix::debug;
using namespace noix::script;

class DebugCommandTest : public ::testing::Test {
protected:
    void SetUp() override {
        _tempDir = std::filesystem::temp_directory_path() / "noix-debug-cmd-test";
        std::filesystem::remove_all(_tempDir);
        std::filesystem::create_directories(_tempDir);
        _engine = std::make_unique<ScriptEngine>(_tempDir.string());
        _engine->start();
    }

    void TearDown() override {
        _engine->stop();
        std::filesystem::remove_all(_tempDir);
    }

    std::filesystem::path _tempDir;
    std::unique_ptr<ScriptEngine> _engine;
};

// ---- PauseCommand ----

TEST_F(DebugCommandTest, PauseCommandReturnsOk) {
    PauseCommand cmd(*_engine);
    std::string result = cmd.execute("{}");
    EXPECT_EQ(result, "ok");
}

// ---- ContinueCommand ----

TEST_F(DebugCommandTest, ContinueCommandReturnsOk) {
    ContinueCommand cmd(*_engine);
    std::string result = cmd.execute("{}");
    EXPECT_EQ(result, "ok");
}

// ---- StepCommand ----

TEST_F(DebugCommandTest, StepCommandDefaultInto) {
    StepCommand cmd(*_engine);
    std::string result = cmd.execute("{}");
    EXPECT_EQ(result, "ok");
}

TEST_F(DebugCommandTest, StepCommandInto) {
    StepCommand cmd(*_engine);
    std::string result = cmd.execute(R"({"kind":"into"})");
    EXPECT_EQ(result, "ok");
}

TEST_F(DebugCommandTest, StepCommandOver) {
    StepCommand cmd(*_engine);
    std::string result = cmd.execute(R"({"kind":"over"})");
    EXPECT_EQ(result, "ok");
}

TEST_F(DebugCommandTest, StepCommandOut) {
    StepCommand cmd(*_engine);
    std::string result = cmd.execute(R"({"kind":"out"})");
    EXPECT_EQ(result, "ok");
}

// ---- BreakpointSetCommand ----

TEST_F(DebugCommandTest, BreakpointSetReturnsId) {
    BreakpointSetCommand cmd(*_engine);
    std::string result = cmd.execute(R"({"source":"test.js","line":10})");
    EXPECT_NE(result.find("\"id\":1"), std::string::npos);
}

TEST_F(DebugCommandTest, BreakpointSetMissingSource) {
    BreakpointSetCommand cmd(*_engine);
    std::string result = cmd.execute(R"({"line":10})");
    EXPECT_NE(result.find("error"), std::string::npos);
}

TEST_F(DebugCommandTest, BreakpointSetMissingLine) {
    BreakpointSetCommand cmd(*_engine);
    std::string result = cmd.execute(R"({"source":"test.js"})");
    EXPECT_NE(result.find("error"), std::string::npos);
}

// ---- BreakpointRemoveCommand ----

TEST_F(DebugCommandTest, BreakpointRemoveExisting) {
    BreakpointSetCommand setCmd(*_engine);
    std::string setResult = setCmd.execute(R"({"source":"test.js","line":10})");
    EXPECT_NE(setResult.find("\"id\":1"), std::string::npos);

    BreakpointRemoveCommand removeCmd(*_engine);
    std::string removeResult = removeCmd.execute(R"({"id":1})");
    EXPECT_EQ(removeResult, "ok");
}

TEST_F(DebugCommandTest, BreakpointRemoveNonexistent) {
    BreakpointRemoveCommand cmd(*_engine);
    std::string result = cmd.execute(R"({"id":999})");
    EXPECT_NE(result.find("error"), std::string::npos);
}

TEST_F(DebugCommandTest, BreakpointRemoveMissingId) {
    BreakpointRemoveCommand cmd(*_engine);
    std::string result = cmd.execute("{}");
    EXPECT_NE(result.find("error"), std::string::npos);
}

// ---- BreakpointClearCommand ----

TEST_F(DebugCommandTest, BreakpointClearReturnsOk) {
    BreakpointClearCommand cmd(*_engine);
    std::string result = cmd.execute("{}");
    EXPECT_EQ(result, "ok");
}

// ---- BreakpointListCommand ----

TEST_F(DebugCommandTest, BreakpointListEmpty) {
    BreakpointListCommand cmd(*_engine);
    std::string result = cmd.execute("{}");
    EXPECT_NE(result.find("[]"), std::string::npos);
}

TEST_F(DebugCommandTest, BreakpointListAfterSet) {
    BreakpointSetCommand setCmd(*_engine);
    setCmd.execute(R"({"source":"a.js","line":5})");
    setCmd.execute(R"({"source":"b.js","line":15})");

    BreakpointListCommand listCmd(*_engine);
    std::string result = listCmd.execute("{}");
    EXPECT_NE(result.find("a.js"), std::string::npos);
    EXPECT_NE(result.find("b.js"), std::string::npos);
}

// ---- DebugStatusCommand ----

TEST_F(DebugCommandTest, DebugStatusReturnsRunning) {
    DebugStatusCommand cmd(*_engine);
    std::string result = cmd.execute("{}");
    EXPECT_NE(result.find("\"state\":\"running\""), std::string::npos);
}

// ---- StackTraceCommand ----

TEST_F(DebugCommandTest, StackTraceNotPausedReturnsEmpty) {
    StackTraceCommand cmd(*_engine);
    std::string result = cmd.execute("{}");
    // 未暂停时 pausedStack 为空
    EXPECT_NE(result.find("[]"), std::string::npos);
}

// ---- DebugEvalCommand ----

TEST_F(DebugCommandTest, DebugEvalNotPausedReturnsError) {
    DebugEvalCommand cmd(*_engine);
    std::string result = cmd.execute(R"({"expr":"1+1"})");
    EXPECT_NE(result.find("not paused"), std::string::npos);
}

TEST_F(DebugCommandTest, DebugEvalMissingExpr) {
    DebugEvalCommand cmd(*_engine);
    std::string result = cmd.execute("{}");
    EXPECT_NE(result.find("error"), std::string::npos);
}
