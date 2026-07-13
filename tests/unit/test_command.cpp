#include <gtest/gtest.h>
#include "debug/EvalCommand.h"
#include "debug/ShutdownCommand.h"
#include "script/JSEngine.h"
#include <SDL3/SDL.h>

using namespace noix::debug;
using namespace noix::script;

TEST(EvalCommandTest, ExecuteWithExpr) {
    JSEngine engine;
    EvalCommand cmd(engine);
    std::string result = cmd.execute(R"({"expr":"40+2"})");
    EXPECT_EQ(result, "42");
}

TEST(EvalCommandTest, ExecuteMissingExpr) {
    JSEngine engine;
    EvalCommand cmd(engine);
    std::string result = cmd.execute(R"({"notexpr":"40+2"})");
    EXPECT_EQ(result, "error: missing 'expr' field");
}

TEST(EvalCommandTest, ExecuteEmptyArgs) {
    JSEngine engine;
    EvalCommand cmd(engine);
    std::string result = cmd.execute("");
    EXPECT_EQ(result, "error: missing 'expr' field");
}

TEST(ShutdownCommandTest, ExecuteReturnsMessage) {
    // SDL_Init needed for SDL_PushEvent to work
    SDL_Init(SDL_INIT_EVENTS);
    uint32_t eventType = SDL_RegisterEvents(1);

    ShutdownCommand cmd(eventType);
    std::string result = cmd.execute("{}");
    EXPECT_EQ(result, "shutting down");

    // Drain the pushed event
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
