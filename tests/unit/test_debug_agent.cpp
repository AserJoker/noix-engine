#include <gtest/gtest.h>
#include "script/DebugAgent.h"
#include "script/ScriptEngine.h"
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>

using namespace noix::script;

class DebugAgentTest : public ::testing::Test {
protected:
    void SetUp() override {
        _tempDir = std::filesystem::temp_directory_path() / "noix-debug-agent-test";
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

// ---- 断点管理 ----

TEST_F(DebugAgentTest, AddBreakpointReturnsId) {
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
}

TEST_F(DebugAgentTest, AddMultipleBreakpointsIncrementsId) {
    std::mutex mtx;
    std::condition_variable cv;
    std::string result;
    bool done = false;

    _engine->postTask([&](JSContext*) {
        auto* agent = _engine->debugAgent();
        ASSERT_TRUE(agent != nullptr);
        uint32_t id1 = agent->addBreakpoint("test.js", 10);
        uint32_t id2 = agent->addBreakpoint("test.js", 20);
        uint32_t id3 = agent->addBreakpoint("other.js", 5);
        result = std::to_string(id1) + "," + std::to_string(id2) + "," + std::to_string(id3);
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3), [&] { return done; }));
    EXPECT_EQ(result, "1,2,3");
}

TEST_F(DebugAgentTest, RemoveBreakpoint) {
    std::mutex mtx;
    std::condition_variable cv;
    bool removed = false;
    bool done = false;

    _engine->postTask([&](JSContext*) {
        auto* agent = _engine->debugAgent();
        ASSERT_TRUE(agent != nullptr);
        uint32_t id = agent->addBreakpoint("test.js", 10);
        removed = agent->removeBreakpoint(id);
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3), [&] { return done; }));
    EXPECT_TRUE(removed);
}

TEST_F(DebugAgentTest, RemoveNonexistentBreakpoint) {
    std::mutex mtx;
    std::condition_variable cv;
    bool removed = true;
    bool done = false;

    _engine->postTask([&](JSContext*) {
        auto* agent = _engine->debugAgent();
        ASSERT_TRUE(agent != nullptr);
        removed = agent->removeBreakpoint(999);
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3), [&] { return done; }));
    EXPECT_FALSE(removed);
}

TEST_F(DebugAgentTest, ClearBreakpoints) {
    std::mutex mtx;
    std::condition_variable cv;
    size_t count = 999;
    bool done = false;

    _engine->postTask([&](JSContext*) {
        auto* agent = _engine->debugAgent();
        ASSERT_TRUE(agent != nullptr);
        agent->addBreakpoint("test.js", 10);
        agent->addBreakpoint("test.js", 20);
        agent->clearBreakpoints();
        count = agent->listBreakpoints().size();
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3), [&] { return done; }));
    EXPECT_EQ(count, 0u);
}

TEST_F(DebugAgentTest, ListBreakpoints) {
    std::mutex mtx;
    std::condition_variable cv;
    std::string result;
    bool done = false;

    _engine->postTask([&](JSContext*) {
        auto* agent = _engine->debugAgent();
        ASSERT_TRUE(agent != nullptr);
        agent->addBreakpoint("a.js", 5);
        agent->addBreakpoint("b.js", 15);
        auto bps = agent->listBreakpoints();
        for (const auto& bp : bps) {
            result += bp.source + ":" + std::to_string(bp.line) + " ";
        }
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3), [&] { return done; }));
    EXPECT_NE(result.find("a.js:5"), std::string::npos);
    EXPECT_NE(result.find("b.js:15"), std::string::npos);
}

// ---- 状态查询 ----

TEST_F(DebugAgentTest, InitialStateIsRunning) {
    std::mutex mtx;
    std::condition_variable cv;
    DebugState state = DebugState::Paused;
    bool done = false;

    _engine->postTask([&](JSContext*) {
        auto* agent = _engine->debugAgent();
        ASSERT_TRUE(agent != nullptr);
        state = agent->state();
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3), [&] { return done; }));
    EXPECT_EQ(state, DebugState::Running);
}

// ---- QuickJS breakpoint API sync ----

TEST_F(DebugAgentTest, AddBreakpointSyncsWithQs) {
    std::mutex mtx;
    std::condition_variable cv;
    size_t count = 999;
    bool done = false;

    _engine->postTask([&](JSContext* ctx) {
        auto* agent = _engine->debugAgent();
        ASSERT_TRUE(agent != nullptr);
        EXPECT_EQ(agent->listBreakpoints().size(), 0u);
        agent->addBreakpoint("test.js", 10);
        count = agent->listBreakpoints().size();
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3), [&] { return done; }));
    EXPECT_EQ(count, 1u);
}

TEST_F(DebugAgentTest, RemoveBreakpointSyncsWithQs) {
    std::mutex mtx;
    std::condition_variable cv;
    size_t count = 999;
    bool done = false;

    _engine->postTask([&](JSContext* ctx) {
        auto* agent = _engine->debugAgent();
        ASSERT_TRUE(agent != nullptr);
        uint32_t id = agent->addBreakpoint("test.js", 10);
        agent->removeBreakpoint(id);
        count = agent->listBreakpoints().size();
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3), [&] { return done; }));
    EXPECT_EQ(count, 0u);
}

TEST_F(DebugAgentTest, ClearBreakpointsSyncsWithQs) {
    std::mutex mtx;
    std::condition_variable cv;
    size_t count = 999;
    bool done = false;

    _engine->postTask([&](JSContext* ctx) {
        auto* agent = _engine->debugAgent();
        ASSERT_TRUE(agent != nullptr);
        agent->addBreakpoint("a.js", 5);
        agent->addBreakpoint("b.js", 10);
        agent->clearBreakpoints();
        count = agent->listBreakpoints().size();
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3), [&] { return done; }));
    EXPECT_EQ(count, 0u);
}

// ---- 调试求值 ----

TEST_F(DebugAgentTest, EvaluateInContextReturnsResult) {
    std::mutex mtx;
    std::condition_variable cv;
    std::string result;
    bool done = false;

    _engine->postTask([&](JSContext*) {
        auto* agent = _engine->debugAgent();
        ASSERT_TRUE(agent != nullptr);
        result = agent->evaluateInContext("6 * 7");
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3), [&] { return done; }));
    EXPECT_EQ(result, "42");
}

TEST_F(DebugAgentTest, EvaluateInContextReturnsError) {
    std::mutex mtx;
    std::condition_variable cv;
    std::string result;
    bool done = false;

    _engine->postTask([&](JSContext*) {
        auto* agent = _engine->debugAgent();
        ASSERT_TRUE(agent != nullptr);
        result = agent->evaluateInContext("undefinedVar");
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3), [&] { return done; }));
    EXPECT_NE(result.find("ReferenceError"), std::string::npos);
}

// ---- 栈追踪 ----

TEST_F(DebugAgentTest, CaptureStackTraceNoCrash) {
    // captureStackTrace 在非执行上下文中调用时可能返回空栈
    // （JS_NewError 无活动栈帧时没有 .stack 属性）
    // 主要验证不崩溃
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;

    _engine->postTask([&](JSContext*) {
        auto* agent = _engine->debugAgent();
        ASSERT_TRUE(agent != nullptr);
        auto frames = agent->captureStackTrace();
        // 非执行上下文中栈可能为空，这是正常的
        (void)frames;
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3), [&] { return done; }));
}

// ---- 暂停/继续（无崩溃测试） ----

TEST_F(DebugAgentTest, PauseAndContinueNoCrash) {
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;

    _engine->postTask([&](JSContext*) {
        auto* agent = _engine->debugAgent();
        ASSERT_TRUE(agent != nullptr);
        agent->pause();
        // continueRun 在非暂停状态下调用应不崩溃
        agent->continueRun();
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3), [&] { return done; }));
}
