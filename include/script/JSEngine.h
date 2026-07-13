#pragma once

#include <string>

struct JSRuntime;
struct JSContext;

namespace noix::script {

class JSEngine {
public:
    JSEngine();
    ~JSEngine();

    JSEngine(const JSEngine&) = delete;
    JSEngine& operator=(const JSEngine&) = delete;

    JSContext* context() const { return _ctx; }

    std::string eval(const std::string& code, const std::string& filename = "<eval>");

private:
    JSRuntime* _rt;
    JSContext* _ctx;
};

} // namespace noix::script
