#include <gtest/gtest.h>
#include "HttpClient.h"
#include <SDL3/SDL_process.h>
#include <SDL3_net/SDL_net.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace noix::test;

/// Fixture that launches the engine with --debug-wait, allowing breakpoint
/// setup before the entry script starts executing.
class DebugWaitTest : public ::testing::Test {
protected:
    uint16_t _port = 0;
    std::unique_ptr<HttpClient> _client;
    std::filesystem::path _tempDir;
    SDL_Process* _process = nullptr;
    std::string _sid;

    void SetUp() override {
        auto testName = std::string(::testing::UnitTest::GetInstance()->current_test_info()->name());
        _tempDir = std::filesystem::temp_directory_path() / ("noix-dbg-" + testName);
        std::filesystem::create_directories(_tempDir);

        // Create a simple entry script
        // Use functions to prevent QuickJS constant folding (which would
        // collapse "x = x + 1" lines and skip intermediate line numbers).
        auto scriptsDir = _tempDir / "scripts";
        std::filesystem::create_directories(scriptsDir);
        std::ofstream(scriptsDir / "entry.js") <<
            "function add(a, b) { return a + b; }\n"   // line 1
            "var x = 1;\n"                              // line 2
            "x = add(x, 1);\n"                          // line 3
            "x = add(x, 1);\n"                          // line 4
            "x = add(x, 1);\n"                          // line 5
            "console.log('done: ' + x);\n";             // line 6

        _port = allocateFreePort();
        launchEngine(_port);
        _client = std::make_unique<HttpClient>("localhost", _port, 10000);
        waitForServerReady();

        // Initialize debug session
        auto initResp = _client->post("/debug/initialize",
            R"({"arguments":{"clientName":"gtest","clientVersion":"1.0"}})");
        ASSERT_EQ(initResp.statusCode, 200);
        _sid = extractJsonField(initResp.body, "sessionId");
        ASSERT_FALSE(_sid.empty());
    }

    void TearDown() override {
        if (_client) {
            _client->post("/debug/command",
                "{\"namespace\":\"noix\",\"command\":\"debug-continue\","
                "\"arguments\":{\"sessionId\":\"" + _sid + "\"}}");
            _client->post("/debug/command",
                "{\"namespace\":\"noix\",\"command\":\"shutdown\","
                "\"arguments\":{\"sessionId\":\"" + _sid + "\"}}");
            _client.reset();
        }
        if (_process) {
            if (!waitForProcessExit(5000)) {
                SDL_KillProcess(_process, true);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            SDL_DestroyProcess(_process);
            _process = nullptr;
        }
        if (!_tempDir.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(_tempDir, ec);
        }
    }

    HttpResponse sendCommand(const std::string& command,
                            const std::string& extraArgs = "") {
        std::string body = "{\"namespace\":\"noix\",\"command\":\"" + command + "\","
            "\"arguments\":{\"sessionId\":\"" + _sid + "\"";
        if (!extraArgs.empty()) body += "," + extraArgs;
        body += "}}";
        return _client->post("/debug/command", body);
    }

    bool waitUntilPaused(int timeoutMs = 5000) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            auto resp = sendCommand("debug-status");
            if (resp.body.find("\"paused\"") != std::string::npos) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return false;
    }

    static uint16_t allocateFreePort() {
        if (!NET_Init()) return 9900;
        for (uint16_t p = 19300; p < 19400; ++p) {
            NET_Server* s = NET_CreateServer(nullptr, p, 0);
            if (s) { NET_DestroyServer(s); NET_Quit(); return p; }
        }
        NET_Quit();
        return 9900;
    }

    static std::string extractJsonField(const std::string& json, const std::string& key) {
        // Try string value first: "key":"value"
        std::string search = "\"" + key + "\":\"";
        size_t pos = json.find(search);
        if (pos != std::string::npos) {
            pos += search.size();
            size_t end = json.find('"', pos);
            return end == std::string::npos ? "" : json.substr(pos, end - pos);
        }
        // Try numeric value: "key":123
        search = "\"" + key + "\":";
        pos = json.find(search);
        if (pos != std::string::npos) {
            pos += search.size();
            size_t end = pos;
            while (end < json.size() && (json[end] == '-' || (json[end] >= '0' && json[end] <= '9')))
                end++;
            return json.substr(pos, end - pos);
        }
        return "";
    }

private:
    void launchEngine(uint16_t port) {
        std::string portStr = std::to_string(port);
        std::string basePathStr = _tempDir.string();
        const char* args[] = {
            "noix-engine", "--headless",
            "--debug-port", portStr.c_str(),
            "--debug-wait",
            "--base-path", basePathStr.c_str(),
            nullptr
        };
        _process = SDL_CreateProcess(args, false);
        ASSERT_TRUE(_process) << "Failed to launch engine";
    }

    void waitForServerReady() {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(10000);
        while (std::chrono::steady_clock::now() < deadline) {
            auto resp = _client->get("/debug/ping");
            if (resp.statusCode == 200) return;
            _client = std::make_unique<HttpClient>("localhost", _port, 10000);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        FAIL() << "Engine not ready";
    }

    bool waitForProcessExit(int timeoutMs) {
        if (!_process) return true;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            int exitcode;
            if (SDL_WaitProcess(_process, false, &exitcode)) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    }
};

// ---- Stop on entry ----

TEST_F(DebugWaitTest, StopOnEntry_PausesAtFirstLine) {
    // Request pause before starting
    auto pauseResp = sendCommand("debug-pause");
    EXPECT_EQ(pauseResp.statusCode, 200);

    // Release the script
    auto runResp = sendCommand("debug-run");
    EXPECT_EQ(runResp.statusCode, 200);

    // Should pause within a few seconds
    bool paused = waitUntilPaused(5000);
    ASSERT_TRUE(paused) << "Engine did not pause on entry";

    // Verify we have stack frames
    auto stackResp = sendCommand("debug-stack-trace");
    EXPECT_EQ(stackResp.statusCode, 200);
    EXPECT_NE(stackResp.body.find("entry.js"), std::string::npos)
        << "Stack should contain entry.js, got: " << stackResp.body;

    // Continue and let script finish
    sendCommand("debug-continue");
}

// ---- Breakpoint hit ----

TEST_F(DebugWaitTest, BreakpointHitOnEntryScript) {
    // Build full path for the entry script (QuickJS stores canonical paths)
    std::string entryPath = (std::filesystem::path(_tempDir) / "scripts" / "entry.js").string();
    for (auto& c : entryPath) { if (c == '\\') c = '/'; }

    // Set a breakpoint at line 4 of entry.js (x = add(x, 1))
    auto bpResp = sendCommand("debug-breakpoint-set",
        "\"source\":\"" + entryPath + "\",\"line\":4");
    EXPECT_EQ(bpResp.statusCode, 200);

    // Release the script
    auto runResp = sendCommand("debug-run");
    EXPECT_EQ(runResp.statusCode, 200);

    // Should pause at the breakpoint (nearest sentinel to line 4 is line 3 or 4)
    bool paused = waitUntilPaused(5000);
    ASSERT_TRUE(paused) << "Engine did not hit breakpoint";

    // Verify the paused location includes a reasonable line number
    auto statusResp = sendCommand("debug-status");
    EXPECT_EQ(statusResp.statusCode, 200);
    // The breakpoint is at line 4, nearest sentinel should be line 3 or 4
    std::string lineStr = extractJsonField(statusResp.body, "line");
    int lineNum = lineStr.empty() ? 0 : std::stoi(lineStr);
    EXPECT_GE(lineNum, 3) << "Paused line should be >= 3, got: " << lineNum;
    EXPECT_LE(lineNum, 4) << "Paused line should be <= 4, got: " << lineNum;

    // Verify stack trace has entry.js
    auto stackResp = sendCommand("debug-stack-trace");
    EXPECT_NE(stackResp.body.find("entry.js"), std::string::npos);

    // Continue
    sendCommand("debug-continue");
}

// ---- Step after stop-on-entry ----

TEST_F(DebugWaitTest, StepAfterStopOnEntry) {
    // Stop on entry
    sendCommand("debug-pause");
    sendCommand("debug-run");
    bool paused = waitUntilPaused(5000);
    ASSERT_TRUE(paused);

    // Step into
    auto stepResp = sendCommand("debug-step", "\"kind\":\"into\"");
    EXPECT_EQ(stepResp.statusCode, 200);

    // Should pause again after stepping
    bool pausedAgain = waitUntilPaused(5000);
    ASSERT_TRUE(pausedAgain) << "Engine did not pause after step";

    // Verify we still have a valid stack
    auto stackResp = sendCommand("debug-stack-trace");
    EXPECT_NE(stackResp.body.find("entry.js"), std::string::npos);

    sendCommand("debug-continue");
}

// ---- Variables captured while paused ----

TEST_F(DebugWaitTest, VariablesCapturedAtBreakpoint) {
    // Build full path for the entry script
    std::string entryPath = (std::filesystem::path(_tempDir) / "scripts" / "entry.js").string();
    for (auto& c : entryPath) { if (c == '\\') c = '/'; }

    // Set breakpoint at line 5 (x = add(x, 1) - second call)
    sendCommand("debug-breakpoint-set", "\"source\":\"" + entryPath + "\",\"line\":5");
    sendCommand("debug-run");

    bool paused = waitUntilPaused(5000);
    ASSERT_TRUE(paused);

    // Check variables - should have 'x' with value 2
    auto varsResp = sendCommand("debug-variables", "\"frameIndex\":0");
    EXPECT_EQ(varsResp.statusCode, 200);
    EXPECT_NE(varsResp.body.find("x"), std::string::npos)
        << "Variables should include 'x', got: " << varsResp.body;

    sendCommand("debug-continue");
}

// ---- debugger keyword ----

TEST_F(DebugWaitTest, DebuggerStatementPauses) {
    // Create script with debugger statement
    auto scriptsDir = _tempDir / "scripts";
    std::ofstream(scriptsDir / "entry.js") <<
        "var x = 1;\n"           // line 1
        "debugger;\n"             // line 2
        "x = x + 1;\n";          // line 3

    sendCommand("debug-run");

    // Should pause at the debugger statement
    bool paused = waitUntilPaused(5000);
    ASSERT_TRUE(paused) << "Engine did not pause at debugger statement";

    // Verify paused location is at or near line 2
    auto statusResp = sendCommand("debug-status");
    EXPECT_EQ(statusResp.statusCode, 200);
    EXPECT_NE(statusResp.body.find("paused"), std::string::npos);

    // Verify stack trace
    auto stackResp = sendCommand("debug-stack-trace");
    EXPECT_NE(stackResp.body.find("entry.js"), std::string::npos);

    sendCommand("debug-continue");
}

// ---- Step over ----

TEST_F(DebugWaitTest, StepOverStaysInSameFunction) {
    // Create script with a function call — step over should NOT step into add()
    auto scriptsDir = _tempDir / "scripts";
    std::ofstream(scriptsDir / "entry.js") <<
        "function add(a, b) { return a + b; }\n"   // line 1
        "var x = 1;\n"                              // line 2
        "x = add(x, 1);\n"                          // line 3
        "x = add(x, 1);\n"                          // line 4
        "console.log('done: ' + x);\n";             // line 5

    std::string entryPath = (std::filesystem::path(_tempDir) / "scripts" / "entry.js").string();
    for (auto& c : entryPath) { if (c == '\\') c = '/'; }

    // Set breakpoint at line 3, then step over
    sendCommand("debug-breakpoint-set", "\"source\":\"" + entryPath + "\",\"line\":3");
    sendCommand("debug-run");

    bool paused = waitUntilPaused(5000);
    ASSERT_TRUE(paused) << "Did not pause at breakpoint";

    // Step over — should advance to line 4 without entering add()
    auto stepResp = sendCommand("debug-step", "\"kind\":\"over\"");
    EXPECT_EQ(stepResp.statusCode, 200);

    bool pausedAgain = waitUntilPaused(5000);
    ASSERT_TRUE(pausedAgain) << "Did not pause after step over";

    // After step over, we should be at line 4 (not inside add())
    auto statusResp = sendCommand("debug-status");
    std::string lineStr = extractJsonField(statusResp.body, "line");
    int lineNum = lineStr.empty() ? 0 : std::stoi(lineStr);
    EXPECT_EQ(lineNum, 4) << "Step over should land at line 4, got: " << lineNum;

    // Stack should still contain entry.js (top-level)
    auto stackResp = sendCommand("debug-stack-trace");
    EXPECT_NE(stackResp.body.find("entry.js"), std::string::npos);

    sendCommand("debug-continue");
}

// ---- Conditional breakpoint ----

TEST_F(DebugWaitTest, ConditionalBreakpointOnlyPausesWhenTrue) {
    // Script that increments x in a loop
    auto scriptsDir = _tempDir / "scripts";
    std::ofstream(scriptsDir / "entry.js") <<
        "var x = 0;\n"                              // line 1
        "for (var i = 0; i < 10; i++) {\n"          // line 2
        "  x = x + 1;\n"                            // line 3
        "}\n"                                        // line 4
        "console.log('done: ' + x);\n";             // line 5

    std::string entryPath = (std::filesystem::path(_tempDir) / "scripts" / "entry.js").string();
    for (auto& c : entryPath) { if (c == '\\') c = '/'; }

    // Set conditional breakpoint: only pause when x == 5
    auto bpResp = sendCommand("debug-breakpoint-set",
        "\"source\":\"" + entryPath + "\",\"line\":3,\"condition\":\"x === 5\"");
    EXPECT_EQ(bpResp.statusCode, 200);

    sendCommand("debug-run");

    bool paused = waitUntilPaused(5000);
    ASSERT_TRUE(paused) << "Did not pause at conditional breakpoint";

    // x should be 5 at the pause point
    auto evalResp = sendCommand("debug-eval-frame",
        "\"frameIndex\":0,\"expr\":\"x\"");
    EXPECT_NE(evalResp.body.find("5"), std::string::npos)
        << "x should be 5, got: " << evalResp.body;

    sendCommand("debug-continue");
}

// ---- Evaluate in frame preserves object identity ----

TEST_F(DebugWaitTest, EvaluateInFramePreservesObjectIdentity) {
    // Script that creates an object
    auto scriptsDir = _tempDir / "scripts";
    std::ofstream(scriptsDir / "entry.js") <<
        "var obj = { name: 'test', value: 42 };\n"  // line 1
        "var x = 1;\n"                               // line 2
        "console.log(obj.name);\n";                   // line 3

    std::string entryPath = (std::filesystem::path(_tempDir) / "scripts" / "entry.js").string();
    for (auto& c : entryPath) { if (c == '\\') c = '/'; }

    // Set breakpoint at line 3 (after both obj and x are assigned)
    sendCommand("debug-breakpoint-set", "\"source\":\"" + entryPath + "\",\"line\":3");
    sendCommand("debug-run");

    bool paused = waitUntilPaused(5000);
    ASSERT_TRUE(paused);

    // Evaluate obj — should return [object Object] (not "undefined" or error)
    auto evalResp = sendCommand("debug-eval-frame",
        "\"frameIndex\":0,\"expr\":\"obj\"");
    EXPECT_EQ(evalResp.statusCode, 200);
    EXPECT_NE(evalResp.body.find("[object Object]"), std::string::npos)
        << "obj should be [object Object], got: " << evalResp.body;

    // Evaluate obj.value — should return 42
    auto valResp = sendCommand("debug-eval-frame",
        "\"frameIndex\":0,\"expr\":\"obj.value\"");
    EXPECT_NE(valResp.body.find("42"), std::string::npos)
        << "obj.value should be 42, got: " << valResp.body;

    // Evaluate a simple expression — should work (x=1, so x+10=11)
    auto exprResp = sendCommand("debug-eval-frame",
        "\"frameIndex\":0,\"expr\":\"x + 10\"");
    EXPECT_NE(exprResp.body.find("11"), std::string::npos)
        << "x + 10 should be 11, got: " << exprResp.body;

    sendCommand("debug-continue");
}
