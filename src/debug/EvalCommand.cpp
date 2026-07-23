#include "debug/EvalCommand.h"
#include "script/ScriptEngine.h"
#include <cJSON.h>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace noix::debug {

EvalCommand::EvalCommand(script::ScriptEngine& engine)
    : _engine(engine) {}

std::string EvalCommand::execute(const std::string& arguments) {
    cJSON* args = cJSON_Parse(arguments.c_str());
    cJSON* expr = args ? cJSON_GetObjectItem(args, "expr") : nullptr;
    std::string code;
    if (expr && cJSON_IsString(expr)) {
        code = expr->valuestring;
    } else {
        cJSON_Delete(args);
        return "error: missing 'expr' field";
    }
    cJSON_Delete(args);

    // TODO: 同步等待脚本线程执行结果（需 JS 引擎 eval 接口）
    return "error: script engine not available";
}

} // namespace noix::debug
