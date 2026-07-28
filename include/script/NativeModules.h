#pragma once

struct JSContext;

namespace noix::script {

/// 注册所有原生 C 模块到 QuickJS context（脚本线程调用）
void registerNativeModules(JSContext* ctx);

} // namespace noix::script
