/**
 * @file JsEngine.h
 * @brief QuickJS C++ 包装类 / QuickJS C++ wrapper
 *
 * 替代 QJSEngine，提供 JavaScript 执行环境。
 * Replaces QJSEngine, providing a JavaScript execution environment.
 */

#ifndef JSENGINE_H
#define JSENGINE_H

#include <string>
#include <functional>
#include <atomic>

// QuickJS is C, need extern "C" wrapper
// Suppress MSVC warnings from quickjs.h (narrowing conversions in inline functions)
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244 4267)
#endif
extern "C" {
#include "quickjs.h"
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

/**
 * @brief QuickJS JavaScript 引擎包装
 *
 * 功能：
 * - evaluate(): 执行 JS 代码
 * - importModule(): 导入 ES 模块
 * - setInterrupted(): 中断执行 (线程安全)
 * - registerFunction(): 注册 C 函数到 JS 全局
 * - setGlobalObject(): 注册一个包含多个方法的对象 (替代 QJSEngine::newQObject)
 */
class JsEngine
{
public:
    JsEngine();
    ~JsEngine();

    // 禁止拷贝
    JsEngine(const JsEngine&) = delete;
    JsEngine& operator=(const JsEngine&) = delete;

    /// 获取 QuickJS context (高级用法)
    JSContext* context() const { return m_ctx; }
    JSRuntime* runtime() const { return m_rt; }

    /**
     * @brief 执行 JavaScript 代码
     * @param code UTF-8 编码的 JS 代码
     * @param error [out] 如果执行失败，返回错误信息
     * @return true 成功, false 失败
     */
    bool evaluate(const std::string& code, std::string& error);

    /**
     * @brief 导入 ES 模块
     * @param filePath 模块文件的绝对路径 (UTF-8)
     * @param error [out] 如果导入失败，返回错误信息
     * @return 模块的导出值 (需要调用者 JS_FreeValue)
     */
    JSValue importModule(const std::string& filePath, std::string& error);

    /**
     * @brief 设置中断标志 (线程安全)
     *
     * 设置后，正在执行的 JS 代码会在下一个检查点中断。
     * 替代 QJSEngine::setInterrupted()
     */
    void setInterrupted(bool interrupted);

    /// 检查是否已中断
    bool isInterrupted() const { return m_interrupted.load(); }

    /**
     * @brief 注册全局 C 函数
     * @param name 函数名 (在 JS 中的名称)
     * @param func QuickJS C 函数指针
     * @param argc 参数个数
     */
    void registerFunction(const char* name, JSCFunction* func, int argc);

    /**
     * @brief 设置全局属性
     * @param name 属性名
     * @param val JS 值 (所有权转移给引擎)
     */
    void setGlobalProperty(const char* name, JSValue val);

    /**
     * @brief 获取全局属性
     * @param name 属性名
     * @return JS 值 (调用者需要 JS_FreeValue)
     */
    JSValue getGlobalProperty(const char* name);

    /**
     * @brief 安装 console 扩展 (console.log, console.error 等)
     *
     * 替代 QJSEngine::installExtensions(ConsoleExtension)
     */
    void installConsoleExtension();

    /**
     * @brief 从 JS 值提取字符串
     */
    static std::string jsValueToString(JSContext* ctx, JSValue val);

    /**
     * @brief 从异常中提取错误信息
     */
    std::string getExceptionString();

    /**
     * @brief 设置模块加载器（用于 import 语句）
     */
    void setModuleLoader();

private:
    static int interruptHandler(JSRuntime* rt, void* opaque);
    static JSModuleDef* moduleLoader(JSContext* ctx, const char* module_name, void* opaque);
    static char* moduleNormalize(JSContext* ctx, const char* base_name,
                                  const char* name, void* opaque);

    JSRuntime* m_rt = nullptr;
    JSContext* m_ctx = nullptr;
    std::atomic<bool> m_interrupted{false};
};

#endif // JSENGINE_H
