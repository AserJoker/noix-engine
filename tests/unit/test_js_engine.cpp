#include <gtest/gtest.h>
#include "script/ScriptEngine.h"
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>

using namespace noix::script;

class ScriptEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        _tempDir = std::filesystem::temp_directory_path() / "noix-script-test";
        std::filesystem::remove_all(_tempDir);
        std::filesystem::create_directories(_tempDir);
    }

    void TearDown() override {
        if (_engine) {
            _engine->stop();
        }
        std::filesystem::remove_all(_tempDir);
    }

    std::filesystem::path _tempDir;
    std::unique_ptr<ScriptEngine> _engine;
};

TEST_F(ScriptEngineTest, EvalArithmetic) {
    _engine = std::make_unique<ScriptEngine>(_tempDir.string());
    _engine->start();

    std::mutex mtx;
    std::condition_variable cv;
    std::string result;
    bool done = false;

    _engine->postTask([&](JSContext*) {
        result = _engine->evalSync("1 + 2");
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3), [&] { return done; }));
    EXPECT_EQ(result, "3");
}

TEST_F(ScriptEngineTest, EvalStringConcat) {
    _engine = std::make_unique<ScriptEngine>(_tempDir.string());
    _engine->start();

    std::mutex mtx;
    std::condition_variable cv;
    std::string result;
    bool done = false;

    _engine->postTask([&](JSContext*) {
        result = _engine->evalSync("'hello' + ' ' + 'world'");
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3), [&] { return done; }));
    EXPECT_EQ(result, "hello world");
}

TEST_F(ScriptEngineTest, EvalSyntaxError) {
    _engine = std::make_unique<ScriptEngine>(_tempDir.string());
    _engine->start();

    std::mutex mtx;
    std::condition_variable cv;
    std::string result;
    bool done = false;

    _engine->postTask([&](JSContext*) {
        result = _engine->evalSync("invalid syntax here!!!");
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(3), [&] { return done; }));
    EXPECT_FALSE(result.empty());
}

TEST_F(ScriptEngineTest, StartAndStop) {
    _engine = std::make_unique<ScriptEngine>(_tempDir.string());
    _engine->start();
    _engine->stop();
    // Should not crash
}
