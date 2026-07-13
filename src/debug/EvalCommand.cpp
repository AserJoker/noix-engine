#include "debug/EvalCommand.h"
#include "script/JSEngine.h"
#include <cJSON.h>

namespace noix::debug {

EvalCommand::EvalCommand(script::JSEngine& engine)
    : _engine(engine) {}

std::string EvalCommand::execute(const std::string& arguments) {
    cJSON* args = cJSON_Parse(arguments.c_str());
    cJSON* expr = args ? cJSON_GetObjectItem(args, "expr") : nullptr;
    std::string result;
    if (expr && cJSON_IsString(expr)) {
        result = _engine.eval(expr->valuestring);
    } else {
        result = "error: missing 'expr' field";
    }
    cJSON_Delete(args);
    return result;
}

} // namespace noix::debug
