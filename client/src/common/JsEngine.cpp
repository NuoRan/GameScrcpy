/**
 * @file JsEngine.cpp
 * @brief QuickJS C++ 包装实现 / QuickJS C++ wrapper implementation
 */

#include "JsEngine.h"

#include <cstring>
#include <cstdio>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include "StringUtils.h"
#else
#include <unistd.h>
#endif

#define LOG_TAG "JsEngine"
#include "Logger.h"

// ============================================================
// Console 扩展的 C 回调函数
// ============================================================

extern "C" {

static JSValue js_console_log(JSContext* ctx, JSValueConst this_val,
                              int argc, JSValueConst* argv)
{
    std::string msg;
    for (int i = 0; i < argc; i++) {
        if (i > 0) msg += " ";
        const char* str = JS_ToCString(ctx, argv[i]);
        if (str) {
            msg += str;
            JS_FreeCString(ctx, str);
        }
    }
    fprintf(stdout, "%s\n", msg.c_str());
    fflush(stdout);
    return JS_UNDEFINED;
}

static JSValue js_console_error(JSContext* ctx, JSValueConst this_val,
                                int argc, JSValueConst* argv)
{
    std::string msg;
    for (int i = 0; i < argc; i++) {
        if (i > 0) msg += " ";
        const char* str = JS_ToCString(ctx, argv[i]);
        if (str) {
            msg += str;
            JS_FreeCString(ctx, str);
        }
    }
    fprintf(stderr, "%s\n", msg.c_str());
    fflush(stderr);
    return JS_UNDEFINED;
}

} // extern "C"

// ============================================================
// JsEngine 实现
// ============================================================

JsEngine::JsEngine()
{
    m_rt = JS_NewRuntime();
    if (!m_rt) {
        LOGE() << "Failed to create QuickJS runtime";
        return;
    }

    // 设置内存限制 (64MB 足够脚本使用)
    JS_SetMemoryLimit(m_rt, 64 * 1024 * 1024);

    // 设置最大栈大小 (2MB)
    JS_SetMaxStackSize(m_rt, 2 * 1024 * 1024);

    // 设置中断处理器
    JS_SetInterruptHandler(m_rt, interruptHandler, this);

    m_ctx = JS_NewContext(m_rt);
    if (!m_ctx) {
        LOGE() << "Failed to create QuickJS context";
        JS_FreeRuntime(m_rt);
        m_rt = nullptr;
        return;
    }

    // 设置模块加载器
    setModuleLoader();
}

JsEngine::~JsEngine()
{
    if (m_ctx) {
        JS_FreeContext(m_ctx);
        m_ctx = nullptr;
    }
    if (m_rt) {
        JS_FreeRuntime(m_rt);
        m_rt = nullptr;
    }
}

bool JsEngine::evaluate(const std::string& code, std::string& error)
{
    if (!m_ctx) {
        error = "JsEngine not initialized";
        return false;
    }

    // 注意：不使用 JS_EVAL_FLAG_STRICT — 旧版 QJSEngine 使用非严格模式，
    // 严格模式下 QuickJS 的闭包变量解析机制对 JS_SetPropertyStr 注册的全局变量
    // （如 mapi）不兼容，会导致 js_closure_global_var 走到 uninitialized var ref 路径
    JSValue result = JS_Eval(m_ctx, code.c_str(), code.size(), "<eval>",
                             JS_EVAL_TYPE_GLOBAL);

    if (JS_IsException(result)) {
        if (JS_HasException(m_ctx)) {
            error = getExceptionString();
        } else {
            // JS_Eval 返回了 JS_EXCEPTION 但没有设置异常（QuickJS 内部 bug）
            std::string preview = code.substr(0, 120);
            error = "internal: JS_Eval returned exception without pending exception. Script: " + preview;
        }
        JS_FreeValue(m_ctx, result);
        return false;
    }

    JS_FreeValue(m_ctx, result);
    return true;
}

JSValue JsEngine::importModule(const std::string& filePath, std::string& error)
{
    if (!m_ctx) {
        error = "JsEngine not initialized";
        return JS_UNDEFINED;
    }

    // 读取文件内容
    FILE* f = nullptr;
#ifdef _WIN32
    // 使用 _wfopen_s 支持中文等非 ASCII 文件名
    std::wstring widePath = strutil::toWide(filePath);
    _wfopen_s(&f, widePath.c_str(), L"rb");
#else
    f = fopen(filePath.c_str(), "rb");
#endif
    if (!f) {
        error = "Cannot open file: " + filePath;
        return JS_UNDEFINED;
    }

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<char> buf(fileSize + 1);
    size_t readSize = fread(buf.data(), 1, fileSize, f);
    fclose(f);
    buf[readSize] = '\0';

    // 使用 module 模式 evaluate
    JSValue result = JS_Eval(m_ctx, buf.data(), readSize, filePath.c_str(),
                             JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);

    if (JS_IsException(result)) {
        error = getExceptionString();
        JS_FreeValue(m_ctx, result);
        return JS_UNDEFINED;
    }

    // 执行模块
    JSValue evalResult = JS_EvalFunction(m_ctx, result);
    if (JS_IsException(evalResult)) {
        error = getExceptionString();
        JS_FreeValue(m_ctx, evalResult);
        return JS_UNDEFINED;
    }

    return evalResult;
}

void JsEngine::setInterrupted(bool interrupted)
{
    m_interrupted.store(interrupted);
}

void JsEngine::registerFunction(const char* name, JSCFunction* func, int argc)
{
    if (!m_ctx) return;

    JSValue global = JS_GetGlobalObject(m_ctx);
    JSValue funcVal = JS_NewCFunction(m_ctx, func, name, argc);
    JS_SetPropertyStr(m_ctx, global, name, funcVal);
    JS_FreeValue(m_ctx, global);
}

void JsEngine::setGlobalProperty(const char* name, JSValue val)
{
    if (!m_ctx) return;

    JSValue global = JS_GetGlobalObject(m_ctx);
    JS_SetPropertyStr(m_ctx, global, name, val);
    JS_FreeValue(m_ctx, global);
}

JSValue JsEngine::getGlobalProperty(const char* name)
{
    if (!m_ctx) return JS_UNDEFINED;

    JSValue global = JS_GetGlobalObject(m_ctx);
    JSValue val = JS_GetPropertyStr(m_ctx, global, name);
    JS_FreeValue(m_ctx, global);
    return val;
}

void JsEngine::installConsoleExtension()
{
    if (!m_ctx) return;

    // 创建 console 对象
    JSValue global = JS_GetGlobalObject(m_ctx);
    JSValue console = JS_NewObject(m_ctx);

    JS_SetPropertyStr(m_ctx, console, "log",
                      JS_NewCFunction(m_ctx, js_console_log, "log", 1));
    JS_SetPropertyStr(m_ctx, console, "info",
                      JS_NewCFunction(m_ctx, js_console_log, "info", 1));
    JS_SetPropertyStr(m_ctx, console, "warn",
                      JS_NewCFunction(m_ctx, js_console_error, "warn", 1));
    JS_SetPropertyStr(m_ctx, console, "error",
                      JS_NewCFunction(m_ctx, js_console_error, "error", 1));

    JS_SetPropertyStr(m_ctx, global, "console", console);
    JS_FreeValue(m_ctx, global);
}

std::string JsEngine::jsValueToString(JSContext* ctx, JSValue val)
{
    const char* str = JS_ToCString(ctx, val);
    if (!str) return "";
    std::string result(str);
    JS_FreeCString(ctx, str);
    return result;
}

std::string JsEngine::getExceptionString()
{
    if (!m_ctx) return "No context";

    JSValue exception = JS_GetException(m_ctx);

    // 检查异常是否有效（JS_TAG_UNINITIALIZED 表示没有待处理的异常）
    int tag = JS_VALUE_GET_TAG(exception);
    if (tag == JS_TAG_UNINITIALIZED) {
        return "(no pending exception - internal error)";
    }

    std::string result = jsValueToString(m_ctx, exception);

    // 如果转为字符串失败（得到 [unsupported type] 等），附加 tag 信息
    if (result.empty() || result == "[unsupported type]") {
        char buf[128];
        snprintf(buf, sizeof(buf), "(exception with JS tag=%d, cannot convert to string)", tag);
        result = buf;
    }

    // 尝试获取 stack trace
    if (JS_IsObject(exception)) {
        JSValue stack = JS_GetPropertyStr(m_ctx, exception, "stack");
        if (!JS_IsUndefined(stack)) {
            std::string stackStr = jsValueToString(m_ctx, stack);
            if (!stackStr.empty()) {
                result += "\n" + stackStr;
            }
        }
        JS_FreeValue(m_ctx, stack);
    }

    JS_FreeValue(m_ctx, exception);
    return result;
}

int JsEngine::interruptHandler(JSRuntime* rt, void* opaque)
{
    JsEngine* engine = static_cast<JsEngine*>(opaque);
    return engine->m_interrupted.load() ? 1 : 0;
}

void JsEngine::setModuleLoader()
{
    if (!m_rt) return;
    JS_SetModuleLoaderFunc(m_rt, moduleNormalize, moduleLoader, this);
}

char* JsEngine::moduleNormalize(JSContext* ctx, const char* base_name,
                                 const char* name, void* opaque)
{
    // 如果是绝对路径，直接返回
    if (name[0] == '/' || name[0] == '\\' ||
        (name[0] != '\0' && name[1] == ':')) {
        char* result = js_strdup(ctx, name);
        return result;
    }

    // 相对路径：基于 base_name 解析
    std::string basePath(base_name);
    size_t lastSlash = basePath.find_last_of("/\\");
    std::string dir;
    if (lastSlash != std::string::npos) {
        dir = basePath.substr(0, lastSlash + 1);
    }

    std::string fullPath = dir + name;

    // 如果没有扩展名，添加 .js
    if (fullPath.find_last_of('.') == std::string::npos ||
        fullPath.find_last_of('.') < fullPath.find_last_of("/\\")) {
        fullPath += ".js";
    }

    char* result = js_strdup(ctx, fullPath.c_str());
    return result;
}

JSModuleDef* JsEngine::moduleLoader(JSContext* ctx, const char* module_name, void* opaque)
{
    // 读取文件
    FILE* f = nullptr;
#ifdef _WIN32
    // 使用 _wfopen_s 支持中文等非 ASCII 模块文件名（module_name 是 UTF-8）
    std::wstring wideModuleName = strutil::toWide(std::string(module_name));
    _wfopen_s(&f, wideModuleName.c_str(), L"rb");
#else
    f = fopen(module_name, "rb");
#endif
    if (!f) {
        JS_ThrowReferenceError(ctx, "could not load module '%s': file not found", module_name);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* buf = (char*)js_malloc(ctx, fileSize + 1);
    if (!buf) {
        fclose(f);
        JS_ThrowOutOfMemory(ctx);
        return NULL;
    }

    size_t readSize = fread(buf, 1, fileSize, f);
    fclose(f);
    buf[readSize] = '\0';

    // 编译模块
    JSValue func_val = JS_Eval(ctx, buf, readSize, module_name,
                                JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    js_free(ctx, buf);

    if (JS_IsException(func_val)) {
        return NULL;
    }

    /* the module is already referenced, so we must free func_val */
    JSModuleDef* m = (JSModuleDef*)JS_VALUE_GET_PTR(func_val);
    JS_FreeValue(ctx, func_val);
    return m;
}
