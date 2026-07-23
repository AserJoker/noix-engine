#include <gtest/gtest.h>
#include "debug/EvalCommand.h"
#include "debug/ShutdownCommand.h"
#include "script/ScriptEngine.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>

using namespace noix::debug;
using namespace noix::script;

class EvalCommandTest : public ::testing::Test {
protected:
    void SetUp() override {
        _tempDir = std::filesystem::temp_directory_path() / "noix-eval-test";
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

TEST_F(EvalCommandTest, ExecuteWithExpr) {
    EvalCommand cmd(*_engine);
    std::string result = cmd.execute(R"({"expr":"40+2"})");
    EXPECT_EQ(result, "42");
}

TEST_F(EvalCommandTest, ExecuteMissingExpr) {
    EvalCommand cmd(*_engine);
    std::string result = cmd.execute(R"({"notexpr":"40+2"})");
    EXPECT_EQ(result, "error: missing 'expr' field");
}

TEST_F(EvalCommandTest, ExecuteEmptyArgs) {
    EvalCommand cmd(*_engine);
    std::string result = cmd.execute("");
    EXPECT_EQ(result, "error: missing 'expr' field");
}

TEST(ShutdownCommandTest, ExecuteReturnsMessage) {
    SDL_Init(SDL_INIT_EVENTS);
    uint32_t eventType = SDL_RegisterEvents(1);

    ShutdownCommand cmd(eventType);
    std::string result = cmd.execute("{}");
    EXPECT_EQ(result, "shutting down");

    SDL_Event event;
    SDL_PollEvent(&event);

    SDL_Quit();
}

TEST(ShutdownCommandTest, PushesCorrectEventType) {
    SDL_Init(SDL_INIT_EVENTS);
    uint32_t eventType = SDL_RegisterEvents(1);

    ShutdownCommand cmd(eventType);
    cmd.execute("{}");

    SDL_Event event;
    ASSERT_TRUE(SDL_PollEvent(&event));
    EXPECT_EQ(event.type, eventType);

    SDL_Quit();
}
