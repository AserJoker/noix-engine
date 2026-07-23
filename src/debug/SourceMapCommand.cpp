#include "debug/SourceMapCommand.h"
#include "script/ScriptEngine.h"
#include <cJSON.h>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>

namespace noix::debug {

SourceMapCommand::SourceMapCommand(script::ScriptEngine& engine)
    : _engine(engine) {}

std::string SourceMapCommand::execute(const std::string& arguments) {
    cJSON* args = cJSON_Parse(arguments.c_str());
    if (!args) return "error: invalid arguments";

    std::mutex mtx;
    std::condition_variable cv;
    std::string scriptsPath;
    bool done = false;

    _engine.postTask([&]() {
        scriptsPath = _engine.scriptsPath();
        std::lock_guard lock(mtx);
        done = true;
        cv.notify_one();
    });

    std::unique_lock lock(mtx);
    cv.wait_for(lock, std::chrono::seconds(2), [&] { return done; });

    cJSON* pathArg = cJSON_GetObjectItem(args, "path");

    cJSON* result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "scriptsPath", scriptsPath.c_str());

    std::filesystem::path sp(scriptsPath);
    std::string basePath = sp.parent_path().string();
    cJSON_AddStringToObject(result, "basePath", basePath.c_str());

    if (pathArg && cJSON_IsString(pathArg)) {
        std::string inputPath = pathArg->valuestring;
        std::filesystem::path fp(inputPath);
        std::error_code ec;
        auto canonical = std::filesystem::weakly_canonical(fp, ec);

        std::string canonicalStr = ec ? inputPath : canonical.string();
        std::string spCanonical = std::filesystem::weakly_canonical(sp, ec).string();

        if (canonicalStr.size() >= spCanonical.size() &&
            canonicalStr.compare(0, spCanonical.size(), spCanonical) == 0) {
            cJSON_AddStringToObject(result, "source", canonicalStr.c_str());
            cJSON_AddBoolToObject(result, "mapped", true);
        } else {
            cJSON_AddBoolToObject(result, "mapped", false);
        }
    }

    cJSON_Delete(args);

    char* json = cJSON_PrintUnformatted(result);
    std::string ret = json;
    cJSON_free(json);
    cJSON_Delete(result);
    return ret;
}

} // namespace noix::debug
