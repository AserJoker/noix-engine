#include <gtest/gtest.h>
#include "script/DebugAgent.h"
#include "script/ScriptEngine.h"
#include "debug/DebugVariablesCommand.h"
#include "debug/DebugEvalFrameCommand.h"
#include "debug/BreakpointCommand.h"
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>

using namespace noix::script;
using namespace noix::debug;

class DebugVariablesTest : public ::testing::Test {
protected:
    void SetUp() override {
        _tempDir = std::filesystem::temp_directory_path() / "noix-debug-vars-test";
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

// ---- DebugVariable struct ----

TEST_F(DebugVariablesTest, DebugVariableDefaults) {
    DebugVariable var;
    EXPECT_EQ(var.name, "");
    EXPECT_EQ(var.value, "");
    EXPECT_EQ(var.type, "");
    EXPECT_FALSE(var.isArgument);
    EXPECT_FALSE(var.isConst);
    EXPECT_FALSE(var.isLexical);
    EXPECT_FALSE(var.isCaptured);
}

// ---- Breakpoint with condition ----

TEST_F(DebugVariablesTest, AddBreakpointWithCondition) {
    std::mutex mtx;
    std::condition_variable cv;
    uint32_t id = 0;
    bool done = false;

    _engine->postTask([&](JSContext*) {
        auto* agent = _engine->debugAgent();
        ASSERT_TRUE(agent != nullptr);
        id = agent->addBreakpoint("test.js", 10, "x > 5");
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3), [&] { return done; }));
    EXPECT_EQ(id, 1);

    // Verify condition is stored
    _engine->postTask([&](JSContext*) {
        auto* agent = _engine->debugAgent();
        auto bps = agent->listBreakpoints();
        ASSERT_EQ(bps.size(), 1u);
        EXPECT_EQ(bps[0].condition, "x > 5");
        std::lock_guard lock2(mtx);
        done = true;
        cv.notify_one();
    });

    done = false;
    lock.unlock();
    lock = std::unique_lock(mtx);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3), [&] { return done; }));
}

TEST_F(DebugVariablesTest, AddBreakpointWithoutCondition) {
    std::mutex mtx;
    std::condition_variable cv;
    uint32_t id = 0;
    bool done = false;

    _engine->postTask([&](JSContext*) {
        auto* agent = _engine->debugAgent();
        ASSERT_TRUE(agent != nullptr);
        id = agent->addBreakpoint("test.js", 10);
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3), [&] { return done; }));
    EXPECT_EQ(id, 1);

    // Verify condition is empty
    _engine->postTask([&](JSContext*) {
        auto* agent = _engine->debugAgent();
        auto bps = agent->listBreakpoints();
        ASSERT_EQ(bps.size(), 1u);
        EXPECT_EQ(bps[0].condition, "");
        std::lock_guard lock2(mtx);
        done = true;
        cv.notify_one();
    });

    done = false;
    lock.unlock();
    lock = std::unique_lock(mtx);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3), [&] { return done; }));
}

// ---- DebugVariablesCommand ----

TEST_F(DebugVariablesTest, VariablesCommandNotPausedReturnsEmpty) {
    DebugVariablesCommand cmd(*_engine);
    std::string result = cmd.execute(R"({"frameIndex":0})");
    // Not paused, should return empty array
    EXPECT_NE(result.find("[]"), std::string::npos);
}

// ---- DebugEvalFrameCommand ----

TEST_F(DebugVariablesTest, EvalFrameCommandNotPausedReturnsError) {
    DebugEvalFrameCommand cmd(*_engine);
    std::string result = cmd.execute(R"({"frameIndex":0,"expr":"1+1"})");
    EXPECT_NE(result.find("not paused"), std::string::npos);
}

TEST_F(DebugVariablesTest, EvalFrameCommandMissingExpr) {
    DebugEvalFrameCommand cmd(*_engine);
    std::string result = cmd.execute(R"({"frameIndex":0})");
    EXPECT_NE(result.find("error"), std::string::npos);
}

// ---- BreakpointSetCommand with condition ----

TEST_F(DebugVariablesTest, BreakpointSetWithCondition) {
    BreakpointSetCommand cmd(*_engine);
    std::string result = cmd.execute(
        R"({"source":"test.js","line":10,"condition":"x > 5"})");
    EXPECT_NE(result.find("\"id\":1"), std::string::npos);
}

TEST_F(DebugVariablesTest, BreakpointSetWithoutCondition) {
    BreakpointSetCommand cmd(*_engine);
    std::string result = cmd.execute(
        R"({"source":"test.js","line":10})");
    EXPECT_NE(result.find("\"id\":1"), std::string::npos);
}

// ---- StackFrame with locals ----

TEST_F(DebugVariablesTest, StackFrameHasLocalsVector) {
    StackFrame frame;
    EXPECT_TRUE(frame.locals.empty());
    frame.locals.push_back({"x", "42", "number", false, false, false, false});
    EXPECT_EQ(frame.locals.size(), 1u);
    EXPECT_EQ(frame.locals[0].name, "x");
    EXPECT_EQ(frame.locals[0].value, "42");
    EXPECT_EQ(frame.locals[0].type, "number");
}
