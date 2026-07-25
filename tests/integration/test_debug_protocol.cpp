#include <gtest/gtest.h>
#include "EngineFixture.h"
#include <chrono>

using namespace noix::test;

class DebugProtocolTest : public EngineFixture {
protected:
    std::string _sid;

    void SetUp() override {
        EngineFixture::SetUp();
        auto initResp = _client->post("/debug/initialize",
            R"({"arguments":{"clientName":"gtest","clientVersion":"1.0"}})");
        ASSERT_EQ(initResp.statusCode, 200);

        std::string key = "\"sessionId\":\"";
        size_t pos = initResp.body.find(key);
        ASSERT_NE(pos, std::string::npos);
        pos += key.size();
        size_t end = initResp.body.find('"', pos);
        ASSERT_NE(end, std::string::npos);
        _sid = initResp.body.substr(pos, end - pos);
        ASSERT_FALSE(_sid.empty());
    }

    /// Send a debug command
    HttpResponse sendCommand(const std::string& command,
                             const std::string& extraArgs = "") {
        std::string body = "{\"namespace\":\"noix\",\"command\":\"" + command + "\","
            "\"arguments\":{\"sessionId\":\"" + _sid + "\"";
        if (!extraArgs.empty()) {
            body += "," + extraArgs;
        }
        body += "}}";
        return _client->post("/debug/command", body);
    }

    /// Poll debug-status until paused, returns true if paused within timeout
    bool waitUntilPaused(int timeoutMs = 5000) {
        auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            auto resp = sendCommand("debug-status");
            if (resp.body.find("paused") != std::string::npos) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    }

    /// Send pause request first, then launch script.
    /// This ensures the interrupt handler will fire during script execution.
    bool launchAndPause(const std::string& scriptExpr) {
        // Set pause flag first so the next interrupt will trigger
        sendCommand("debug-pause");
        // Then launch the script - it will be interrupted
        sendCommand("exec-script", "\"expr\":\"" + scriptExpr + "\"");
        return waitUntilPaused();
    }
};

// ---- debug-status ----

TEST_F(DebugProtocolTest, StatusReturnsRunning) {
    auto resp = sendCommand("debug-status");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("running"), std::string::npos);
}

// ---- debug-pause + debug-continue ----

TEST_F(DebugProtocolTest, PauseAndContinue) {
    auto evalResp = sendCommand("exec-script",
        "\"expr\":\"for(var i=0;i<100000;i++){}\"");
    auto pauseResp = sendCommand("debug-pause");
    EXPECT_EQ(pauseResp.statusCode, 200);
    EXPECT_NE(pauseResp.body.find("\"ok\""), std::string::npos);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto statusResp = sendCommand("debug-status");
    EXPECT_EQ(statusResp.statusCode, 200);

    auto continueResp = sendCommand("debug-continue");
    EXPECT_EQ(continueResp.statusCode, 200);
    EXPECT_NE(continueResp.body.find("\"ok\""), std::string::npos);
}

// ---- Pause + status verification ----

TEST_F(DebugProtocolTest, PauseThenStatusIsPaused) {
    bool paused = launchAndPause("for(var i=0;i<10000000;i++){}");
    ASSERT_TRUE(paused);

    auto statusResp = sendCommand("debug-status");
    EXPECT_EQ(statusResp.statusCode, 200);
    EXPECT_NE(statusResp.body.find("paused"), std::string::npos);

    sendCommand("debug-continue");
}

// ---- Breakpoints ----

TEST_F(DebugProtocolTest, SetBreakpoint) {
    auto resp = sendCommand("debug-breakpoint-set",
        "\"source\":\"test.js\",\"line\":5");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("id"), std::string::npos);
}

TEST_F(DebugProtocolTest, ListBreakpoints) {
    sendCommand("debug-breakpoint-set", "\"source\":\"a.js\",\"line\":10");
    sendCommand("debug-breakpoint-set", "\"source\":\"b.js\",\"line\":20");

    auto resp = sendCommand("debug-breakpoint-list");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("a.js"), std::string::npos);
    EXPECT_NE(resp.body.find("b.js"), std::string::npos);
}

TEST_F(DebugProtocolTest, RemoveBreakpoint) {
    sendCommand("debug-breakpoint-set", "\"source\":\"test.js\",\"line\":5");

    auto resp = sendCommand("debug-breakpoint-remove", "\"id\":1");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("\"ok\""), std::string::npos);

    auto listResp = sendCommand("debug-breakpoint-list");
    EXPECT_EQ(listResp.statusCode, 200);
    EXPECT_NE(listResp.body.find("[]"), std::string::npos);
}

TEST_F(DebugProtocolTest, ClearBreakpoints) {
    sendCommand("debug-breakpoint-set", "\"source\":\"a.js\",\"line\":10");
    sendCommand("debug-breakpoint-set", "\"source\":\"b.js\",\"line\":20");

    auto resp = sendCommand("debug-breakpoint-clear");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("\"ok\""), std::string::npos);

    auto listResp = sendCommand("debug-breakpoint-list");
    EXPECT_NE(listResp.body.find("[]"), std::string::npos);
}

// ---- debug-step ----

TEST_F(DebugProtocolTest, StepInto) {
    auto resp = sendCommand("debug-step", "\"kind\":\"into\"");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("\"ok\""), std::string::npos);
}

TEST_F(DebugProtocolTest, StepOver) {
    auto resp = sendCommand("debug-step", "\"kind\":\"over\"");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("\"ok\""), std::string::npos);
}

TEST_F(DebugProtocolTest, StepOut) {
    auto resp = sendCommand("debug-step", "\"kind\":\"out\"");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("\"ok\""), std::string::npos);
}

// ---- debug-stack-trace ----

TEST_F(DebugProtocolTest, StackTraceNotPausedReturnsEmpty) {
    auto resp = sendCommand("debug-stack-trace");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("[]"), std::string::npos);
}

TEST_F(DebugProtocolTest, PauseThenStackHasFrames) {
    bool paused = launchAndPause("for(var i=0;i<10000000;i++){}");
    ASSERT_TRUE(paused);

    auto stackResp = sendCommand("debug-stack-trace");
    EXPECT_EQ(stackResp.statusCode, 200);
    // Should have at least one frame (the loop script)
    EXPECT_NE(stackResp.body.find("source"), std::string::npos);

    sendCommand("debug-continue");
}

// ---- debug-eval ----

TEST_F(DebugProtocolTest, DebugEvalNotPausedReturnsError) {
    auto resp = sendCommand("debug-eval", "\"expr\":\"1+1\"");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("not paused"), std::string::npos);
}

// ---- debug-variables ----

TEST_F(DebugProtocolTest, VariablesNotPausedReturnsEmpty) {
    auto resp = sendCommand("debug-variables", "\"frameIndex\":0");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("[]"), std::string::npos);
}

TEST_F(DebugProtocolTest, PauseThenVariablesCaptured) {
    // Script with an IIFE where variables are on separate lines
    // so that stepping pauses after the assignments
    bool paused = launchAndPause(
        "(function(){\nvar x = 42;\nvar msg = 'hello';\nfor(var i=0;i<10000000;i++){x++;};\nreturn x;\n})()");
    ASSERT_TRUE(paused);

    // Step into to enter the IIFE (pauses at first statement before var x = 42)
    sendCommand("debug-step", "\"kind\":\"into\"");
    bool pausedAgain = waitUntilPaused(5000);
    ASSERT_TRUE(pausedAgain);

    // Step again to get past var x = 42 (now x should be 42)
    sendCommand("debug-step", "\"kind\":\"into\"");
    pausedAgain = waitUntilPaused(5000);
    ASSERT_TRUE(pausedAgain);

    auto resp = sendCommand("debug-variables", "\"frameIndex\":0");
    EXPECT_EQ(resp.statusCode, 200);
    // Should contain the variable 'x' with type 'number'
    EXPECT_NE(resp.body.find("x"), std::string::npos)
        << "Variables body: " << resp.body;
    EXPECT_NE(resp.body.find("number"), std::string::npos)
        << "Variables body: " << resp.body;

    sendCommand("debug-continue");
}

// ---- debug-eval-frame ----

TEST_F(DebugProtocolTest, EvalFrameNotPausedReturnsError) {
    auto resp = sendCommand("debug-eval-frame", "\"frameIndex\":0,\"expr\":\"1+1\"");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("not paused"), std::string::npos);
}

TEST_F(DebugProtocolTest, PauseThenEvalFrameReturnsResult) {
    bool paused = launchAndPause(
        "(function(){var x = 42; for(var i=0;i<10000000;i++){x++;}; return x;})()");
    ASSERT_TRUE(paused);

    auto resp = sendCommand("debug-eval-frame", "\"frameIndex\":0,\"expr\":\"1+1\"");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("2"), std::string::npos);

    sendCommand("debug-continue");
}

// ---- Conditional breakpoints ----

TEST_F(DebugProtocolTest, SetBreakpointWithCondition) {
    auto resp = sendCommand("debug-breakpoint-set",
        "\"source\":\"test.js\",\"line\":5,\"condition\":\"x > 5\"");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("id"), std::string::npos);
}

TEST_F(DebugProtocolTest, BreakpointListIncludesCondition) {
    sendCommand("debug-breakpoint-set",
        "\"source\":\"test.js\",\"line\":10,\"condition\":\"x > 5\"");

    auto resp = sendCommand("debug-breakpoint-list");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("condition"), std::string::npos);
}

// ---- Full pause-inspect-continue flow ----

TEST_F(DebugProtocolTest, PauseInspectContinue) {
    bool paused = launchAndPause(
        "(function(){var x = 1; for(var i=0;i<10000000;i++){x++}; return x;})()");
    ASSERT_TRUE(paused);

    // Check status
    auto statusResp = sendCommand("debug-status");
    EXPECT_EQ(statusResp.statusCode, 200);
    EXPECT_NE(statusResp.body.find("paused"), std::string::npos);

    // Get stack trace
    auto stackResp = sendCommand("debug-stack-trace");
    EXPECT_EQ(stackResp.statusCode, 200);

    // Get variables
    auto varsResp = sendCommand("debug-variables", "\"frameIndex\":0");
    EXPECT_EQ(varsResp.statusCode, 200);

    // Eval in frame scope
    auto evalResp = sendCommand("debug-eval-frame",
        "\"frameIndex\":0,\"expr\":\"1+1\"");
    EXPECT_EQ(evalResp.statusCode, 200);

    // Continue
    auto continueResp = sendCommand("debug-continue");
    EXPECT_EQ(continueResp.statusCode, 200);
}
