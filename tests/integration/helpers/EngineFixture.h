#pragma once

#include "HttpClient.h"
#include <SDL3/SDL_process.h>
#include <SDL3_net/SDL_net.h>
#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>
#include <thread>

namespace noix::test {

class EngineFixture : public ::testing::Test {
protected:
    uint16_t _port = 0;
    std::unique_ptr<HttpClient> _client;
    std::filesystem::path _tempDir;
    SDL_Process *_process = nullptr;

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
        // 1. 尝试优雅关闭：先 continue（解除暂停），再 shutdown
        if (_client) {
            // 创建临时会话用于发送关闭命令
            auto initResp = _client->post("/debug/initialize",
                R"({"arguments":{"clientName":"test","clientVersion":"1.0"}})");
            if (initResp.statusCode == 200) {
                std::string sid = extractSessionId(initResp.body);
                if (!sid.empty()) {
                    // 先 continue，解除可能存在的调试暂停
                    _client->post("/debug/command",
                        "{\"namespace\":\"noix\",\"command\":\"debug-continue\","
                        "\"arguments\":{\"sessionId\":\"" + sid + "\"}}");
                    // 再 shutdown
                    _client->post("/debug/command",
                        "{\"namespace\":\"noix\",\"command\":\"shutdown\","
                        "\"arguments\":{\"sessionId\":\"" + sid + "\"}}");
                }
            }
            _client.reset();
        }

        // 2. 等待进程退出
        if (!waitForProcessExit(5000)) {
            killProcess();
            // 强杀后额外等待，确保文件句柄释放
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        if (_process) {
            SDL_DestroyProcess(_process);
            _process = nullptr;
        }

        // 3. 清理临时目录（包括日志文件）
        if (!_tempDir.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(_tempDir, ec);
            // 忽略清理失败（文件可能仍被占用）
        }
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
        std::string portStr = std::to_string(port);
        std::string basePathStr = _tempDir.string();

        const char *args[] = {
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
        if (!_process) return true;
        auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            int exitcode;
            if (SDL_WaitProcess(_process, false, &exitcode)) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return false;
    }

    void killProcess() {
        if (!_process) return;
        SDL_KillProcess(_process, true);
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
