#ifndef JSBINDINGS_H
#define JSBINDINGS_H

/**
 * @file JsBindings.h
 * @brief QuickJS C 绑定 — 将 SandboxScriptApi 的 28 个方法注册到 JS 引擎
 *
 * 替代 QJSEngine::newQObject(api) + Q_INVOKABLE 的自动暴露机制。
 * 每个方法通过 JS_SetContextOpaque 获取 SandboxScriptApi 指针。
 */

struct JSContext;
class SandboxScriptApi;

/**
 * @brief 注册全部 28 个 API 绑定到 QuickJS context
 *
 * 在 JS 全局对象上创建 "mapi" 对象，包含所有脚本方法。
 * 同时安装 logerror 全局函数。
 *
 * @param ctx  QuickJS context
 * @param api  SandboxScriptApi 实例指针（生命周期由调用者管理）
 */
void registerApiBindings(JSContext* ctx, SandboxScriptApi* api);

/**
 * @brief 清理线程局部模块缓存
 *
 * 必须在 JsEngine 销毁前调用（在 runScript() 结束时）。
 */
void clearModuleCache();

#endif // JSBINDINGS_H
