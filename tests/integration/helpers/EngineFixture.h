#pragma once

#include "HttpClient.h"
#include <SDL3_net/SDL_net.h>
#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace noix::test {

class EngineFixture : public ::testing::Test {
protected:
    uint16_t _port = 0;
    std::unique_ptr<HttpClient> _client;
    std::filesystem::path _tempDir;

#ifdef _WIN32
    PROCESS_INFORMATION _procInfo = {};
    bool _processCreated = false;
#endif

    void SetUp() override {
        // Create temp directory for this test's logs
        auto testName = std::string(::testing::UnitTest::GetInstance()->current_test_info()->name());
        _tempDir = std::filesystem::temp_directory_path() / ("noix-test-" + testName);
        std::filesystem::create_directories(_tempDir);

        _port = allocateFreePort();
        launchEngine(_port);
        _client = std::make_unique<HttpClient>("localhost", _port, 10000);
        waitForServerReady();
    }

    void TearDown() override {
        if (_client) {
            auto initResp = _client->post("/debug/initialize",
                R"({"arguments":{"clientName":"test","clientVersion":"1.0"}})");
            if (initResp.statusCode == 200) {
                std::string sid = extractSessionId(initResp.body);
                if (!sid.empty()) {
                    _client->post("/debug/command",
                        "{\"namespace\":\"debug\",\"command\":\"shutdown\","
                        "\"arguments\":{\"sessionId\":\"" + sid + "\"}}");
                }
            }
        }

        if (!waitForProcessExit(5000)) {
            killProcess();
        }

        // Clean up temp directory
        std::filesystem::remove_all(_tempDir);
    }

    static uint16_t allocateFreePort() {
        if (!NET_Init()) {
            return 9900;
        }
        for (uint16_t port = 19100; port < 19200; ++port) {
            NET_Server *server = NET_CreateServer(nullptr, port, 0);
            if (server) {
                NET_DestroyServer(server);
                NET_Quit();
                return port;
            }
        }
        NET_Quit();
        return 9900;
    }

private:
    void launchEngine(uint16_t port) {
#ifdef _WIN32
        std::string cmdLine = "noix-engine.exe --headless --debug-port " + std::to_string(port)
            + " --base-path " + _tempDir.string();

        STARTUPINFOW si = {sizeof(STARTUPINFOW)};
        std::wstring wCmdLine(cmdLine.begin(), cmdLine.end());

        BOOL result = CreateProcessW(
            nullptr,
            wCmdLine.data(),
            nullptr, nullptr, FALSE,
            CREATE_NEW_PROCESS_GROUP,
            nullptr, nullptr,
            &si, &_procInfo);

        ASSERT_TRUE(result) << "Failed to launch engine subprocess";
        _processCreated = true;
#endif
    }

    void waitForServerReady() {
        auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(10000);

        while (std::chrono::steady_clock::now() < deadline) {
            auto resp = _client->get("/debug/ping");
            if (resp.statusCode == 200) return;
            _client = std::make_unique<HttpClient>("localhost", _port, 10000);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        FAIL() << "Engine did not become ready within timeout (port=" << _port << ")";
    }

    bool waitForProcessExit(int timeoutMs) {
#ifdef _WIN32
        if (!_processCreated) return true;
        return WaitForSingleObject(_procInfo.hProcess, timeoutMs) == WAIT_OBJECT_0;
#else
        return true;
#endif
    }

    void killProcess() {
#ifdef _WIN32
        if (!_processCreated) return;
        TerminateProcess(_procInfo.hProcess, 1);
        WaitForSingleObject(_procInfo.hProcess, 3000);
#endif
    }

    static std::string extractSessionId(const std::string& json) {
        std::string key = "\"sessionId\":\"";
        size_t pos = json.find(key);
        if (pos == std::string::npos) return "";
        pos += key.size();
        size_t end = json.find('"', pos);
        if (end == std::string::npos) return "";
        return json.substr(pos, end - pos);
    }
};

} // namespace noix::test
