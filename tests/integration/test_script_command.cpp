#include <gtest/gtest.h>
#include "HttpClient.h"
#include <SDL3/SDL_process.h>
#include <SDL3_net/SDL_net.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace noix::test {

/*
 * Fixture that launches noix-engine with a custom entry.js script
 * that registers script commands via the noix:debug native module.
 */
class ScriptCommandTest : public ::testing::Test {
protected:
    uint16_t _port = 0;
    std::unique_ptr<HttpClient> _client;
    std::filesystem::path _tempDir;
    SDL_Process* _process = nullptr;

    void SetUp() override {
        auto testName = std::string(::testing::UnitTest::GetInstance()->current_test_info()->name());
        _tempDir = std::filesystem::temp_directory_path() / ("noix-script-test-" + testName);
        std::filesystem::create_directories(_tempDir);

        writeEntryScript();
        _port = allocateFreePort();
        launchEngine(_port);
        _client = std::make_unique<HttpClient>("localhost", _port, 10000);
        waitForServerReady();
    }

    void TearDown() override {
        if (!waitForProcessExit(3000)) {
            killProcess();
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (_process) {
            SDL_DestroyProcess(_process);
            _process = nullptr;
        }
        if (!_tempDir.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(_tempDir, ec);
        }
    }

private:
    void writeEntryScript() {
        auto scriptsDir = _tempDir / "scripts";
        std::filesystem::create_directories(scriptsDir);

        std::ofstream out(scriptsDir / "entry.js");
        out << R"(
import { registerCommand } from "noix:debug";
import * as logger from "noix:logger";

// Echo: returns the request body as-is
registerCommand("script/echo", "v1", (request) => {
    return { echo: request, status: "ok" };
});

// Greet: extracts name from request, returns greeting
registerCommand("script/greet", "v1", (request) => {
    const name = request.name || "world";
    return { message: "hello, " + name + "!" };
});

// Compute: demonstrates request processing
registerCommand("script/compute", "v1", (request) => {
    const a = request.a || 0;
    const b = request.b || 0;
    return { sum: a + b, product: a * b };
});

logger.info("Script commands registered: script/echo, script/greet, script/compute");
)";
    }

    static uint16_t allocateFreePort() {
        if (!NET_Init()) return 9900;
        for (uint16_t port = 19200; port < 19300; ++port) {
            NET_Server* server = NET_CreateServer(nullptr, port, 0);
            if (server) {
                NET_DestroyServer(server);
                NET_Quit();
                return port;
            }
        }
        NET_Quit();
        return 9900;
    }

    void launchEngine(uint16_t port) {
        std::string portStr = std::to_string(port);
        std::string basePathStr = _tempDir.string();

        const char* args[] = {
            "noix-engine",
            "--headless",
            "--debug-port", portStr.c_str(),
            "--base-path", basePathStr.c_str(),
            nullptr
        };

        _process = SDL_CreateProcess(args, false);
        ASSERT_TRUE(_process) << "Failed to launch engine subprocess: " << SDL_GetError();
    }

    void waitForServerReady() {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(15000);

        while (std::chrono::steady_clock::now() < deadline) {
            auto resp = _client->post("/api/v1/system/ping", "{}");
            if (resp.statusCode == 200) return;
            _client = std::make_unique<HttpClient>("localhost", _port, 10000);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        FAIL() << "Engine did not become ready within timeout (port=" << _port << ")";
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

    void killProcess() {
        if (_process) SDL_KillProcess(_process, true);
    }
};

// --- Test cases ---

TEST_F(ScriptCommandTest, EchoReturnsRequest) {
    auto resp = _client->post("/api/v1/script/echo", R"({"key":"value"})");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("\"echo\""), std::string::npos);
    EXPECT_NE(resp.body.find("\"status\""), std::string::npos);
    EXPECT_NE(resp.body.find("\"ok\""), std::string::npos);
    EXPECT_NE(resp.body.find("\"key\""), std::string::npos);
    EXPECT_NE(resp.body.find("\"value\""), std::string::npos);
}

TEST_F(ScriptCommandTest, GreetDefaultName) {
    auto resp = _client->post("/api/v1/script/greet", "{}");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("hello, world!"), std::string::npos);
}

TEST_F(ScriptCommandTest, GreetCustomName) {
    auto resp = _client->post("/api/v1/script/greet", R"({"name":"noix"})");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("hello, noix!"), std::string::npos);
}

TEST_F(ScriptCommandTest, ComputeArithmetic) {
    auto resp = _client->post("/api/v1/script/compute", R"({"a":3,"b":4})");
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("\"sum\":7"), std::string::npos);
    EXPECT_NE(resp.body.find("\"product\":12"), std::string::npos);
}

TEST_F(ScriptCommandTest, ScriptEndpointNotFound) {
    auto resp = _client->post("/api/v1/script/nonexistent", "{}");
    EXPECT_EQ(resp.statusCode, 404);
}

TEST_F(ScriptCommandTest, ScriptAndSystemEndpointsCoexist) {
    // System endpoint still works
    auto sysResp = _client->post("/api/v1/system/ping", "{}");
    EXPECT_EQ(sysResp.statusCode, 200);

    // Script endpoint also works
    auto scriptResp = _client->post("/api/v1/script/echo", R"({"test":true})");
    EXPECT_EQ(scriptResp.statusCode, 200);
}

} // namespace noix::test
