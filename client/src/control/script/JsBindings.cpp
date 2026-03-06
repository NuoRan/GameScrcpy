/**
 * @file JsBindings.cpp
 * @brief QuickJS C 绑定实现 — 28 个 SandboxScriptApi 方法
 *
 * 每个 JS 函数通过 JS_GetContextOpaque(ctx) 获取 SandboxScriptApi* 指针。
 * 参数类型转换: JSValue → C++, 返回值: C++ → JSValue
 */

#include "JsBindings.h"
#include "ScriptSandbox.h"

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

#include <string>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include "StringUtils.h"
#include "GameTypes.h"

#define LOG_TAG "JsBindings"
#include "Logger.h"

// ============================================================
// Helper: 从 context 获取 API 指针
// ============================================================

static inline SandboxScriptApi* getApi(JSContext* ctx)
{
    return static_cast<SandboxScriptApi*>(JS_GetContextOpaque(ctx));
}

// ============================================================
// 线程局部模块缓存
// ============================================================
static thread_local std::unordered_map<std::string, JSValue> s_moduleCache;
static thread_local JSContext* s_moduleCacheCtx = nullptr;

void clearModuleCache()
{
    for (auto& kv : s_moduleCache) {
        if (s_moduleCacheCtx) {
            JS_FreeValue(s_moduleCacheCtx, kv.second);
        }
    }
    s_moduleCache.clear();
    s_moduleCacheCtx = nullptr;
}

// Helper: JSValue → std::string
static inline std::string jsToStdString(JSContext* ctx, JSValueConst val)
{
    const char* str = JS_ToCString(ctx, val);
    if (!str) return std::string();
    std::string result(str);
    JS_FreeCString(ctx, str);
    return result;
}

// Helper: PosResult → JSValue object
static JSValue posResultToJS(JSContext* ctx, const PosResult& r)
{
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, r.x));
    JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, r.y));
    return obj;
}

// Helper: KeyPosResult → JSValue object
static JSValue keyPosResultToJS(JSContext* ctx, const KeyPosResult& r)
{
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, r.x));
    JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, r.y));
    JS_SetPropertyStr(ctx, obj, "valid", JS_NewBool(ctx, r.valid));
    return obj;
}

// Helper: ButtonPosResult → JSValue object
static JSValue buttonPosResultToJS(JSContext* ctx, const ButtonPosResult& r)
{
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, r.x));
    JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, r.y));
    JS_SetPropertyStr(ctx, obj, "valid", JS_NewBool(ctx, r.valid));
    JS_SetPropertyStr(ctx, obj, "name", JS_NewString(ctx, r.name.c_str()));
    return obj;
}

// Helper: FindImageResult → JSValue object
static JSValue findImageResultToJS(JSContext* ctx, const FindImageResult& r)
{
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "found", JS_NewBool(ctx, r.found));
    JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, r.x));
    JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, r.y));
    JS_SetPropertyStr(ctx, obj, "confidence", JS_NewFloat64(ctx, r.confidence));
    return obj;
}

// Helper: JSValue → ScriptValue (用于 setGlobal)
static ScriptValue jsToScriptValue(JSContext* ctx, JSValueConst val)
{
    if (JS_IsBool(val))
        return ScriptValue(JS_ToBool(ctx, val) != 0);
    if (JS_IsNumber(val)) {
        double d;
        JS_ToFloat64(ctx, &d, val);
        // 如果是整数值，存为 int
        if (d == static_cast<int>(d) && d >= INT_MIN && d <= INT_MAX)
            return ScriptValue(static_cast<int>(d));
        return ScriptValue(d);
    }
    if (JS_IsString(val))
        return ScriptValue(jsToStdString(ctx, val));
    if (JS_IsNull(val) || JS_IsUndefined(val))
        return ScriptValue();
    // 对于对象/数组，JSON 序列化
    JSValue jsonStr = JS_JSONStringify(ctx, val, JS_UNDEFINED, JS_UNDEFINED);
    if (!JS_IsException(jsonStr)) {
        std::string s = jsToStdString(ctx, jsonStr);
        JS_FreeValue(ctx, jsonStr);
        return ScriptValue(s);  // 存为 JSON 字符串
    }
    JS_FreeValue(ctx, jsonStr);
    return ScriptValue();
}

// Helper: ScriptValue → JSValue (用于 getGlobal)
static JSValue scriptValueToJS(JSContext* ctx, const ScriptValue& v)
{
    if (std::holds_alternative<std::monostate>(v)) return JS_UNDEFINED;
    if (auto* b = std::get_if<bool>(&v))
        return JS_NewBool(ctx, *b);
    if (auto* i = std::get_if<int>(&v))
        return JS_NewInt32(ctx, *i);
    if (auto* d = std::get_if<double>(&v))
        return JS_NewFloat64(ctx, *d);
    if (auto* s = std::get_if<std::string>(&v))
        return JS_NewString(ctx, s->c_str());
    return JS_UNDEFINED;
}

// ============================================================
// 28 个 API 绑定函数
// ============================================================

// click(x?, y?)
static JSValue js_click(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    auto* api = getApi(ctx);
    double x = -1, y = -1;
    if (argc > 0) JS_ToFloat64(ctx, &x, argv[0]);
    if (argc > 1) JS_ToFloat64(ctx, &y, argv[1]);
    api->click(x, y);
    return JS_UNDEFINED;
}

// holdpress(x?, y?)
static JSValue js_holdpress(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    auto* api = getApi(ctx);
    double x = -1, y = -1;
    if (argc > 0) JS_ToFloat64(ctx, &x, argv[0]);
    if (argc > 1) JS_ToFloat64(ctx, &y, argv[1]);
    api->holdpress(x, y);
    return JS_UNDEFINED;
}

// release()
static JSValue js_release(JSContext* ctx, JSValueConst, int, JSValueConst*)
{
    getApi(ctx)->release();
    return JS_UNDEFINED;
}

// slide(sx, sy, ex, ey, delayMs, num)
static JSValue js_slide(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    auto* api = getApi(ctx);
    double sx = 0, sy = 0, ex = 0, ey = 0;
    int delayMs = 200, num = 10;
    if (argc > 0) JS_ToFloat64(ctx, &sx, argv[0]);
    if (argc > 1) JS_ToFloat64(ctx, &sy, argv[1]);
    if (argc > 2) JS_ToFloat64(ctx, &ex, argv[2]);
    if (argc > 3) JS_ToFloat64(ctx, &ey, argv[3]);
    if (argc > 4) JS_ToInt32(ctx, &delayMs, argv[4]);
    if (argc > 5) JS_ToInt32(ctx, &num, argv[5]);
    api->slide(sx, sy, ex, ey, delayMs, num);
    return JS_UNDEFINED;
}

// pinch(centerX, centerY, scale, durationMs?, steps?)
static JSValue js_pinch(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    auto* api = getApi(ctx);
    double cx = 0.5, cy = 0.5, scale = 1.0;
    int duration = 300, steps = 10;
    if (argc > 0) JS_ToFloat64(ctx, &cx, argv[0]);
    if (argc > 1) JS_ToFloat64(ctx, &cy, argv[1]);
    if (argc > 2) JS_ToFloat64(ctx, &scale, argv[2]);
    if (argc > 3) JS_ToInt32(ctx, &duration, argv[3]);
    if (argc > 4) JS_ToInt32(ctx, &steps, argv[4]);
    api->pinch(cx, cy, scale, duration, steps);
    return JS_UNDEFINED;
}

// isPress() → bool
static JSValue js_isPress(JSContext* ctx, JSValueConst, int, JSValueConst*)
{
    return JS_NewBool(ctx, getApi(ctx)->isPress());
}

// key(keyName, durationMs?)
static JSValue js_key(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    auto* api = getApi(ctx);
    if (argc < 1) return JS_UNDEFINED;
    std::string keyName = jsToStdString(ctx, argv[0]);
    int duration = 50;
    if (argc > 1) JS_ToInt32(ctx, &duration, argv[1]);
    api->key(keyName, duration);
    return JS_UNDEFINED;
}

// releaseAll()
static JSValue js_releaseAll(JSContext* ctx, JSValueConst, int, JSValueConst*)
{
    getApi(ctx)->releaseAll();
    return JS_UNDEFINED;
}

// sleep(ms)
static JSValue js_sleep(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    int ms = 0;
    if (argc > 0) JS_ToInt32(ctx, &ms, argv[0]);
    getApi(ctx)->sleep(ms);
    return JS_UNDEFINED;
}

// isInterrupted() → bool
static JSValue js_isInterrupted(JSContext* ctx, JSValueConst, int, JSValueConst*)
{
    return JS_NewBool(ctx, getApi(ctx)->isInterrupted());
}

// stop()
static JSValue js_stop(JSContext* ctx, JSValueConst, int, JSValueConst*)
{
    getApi(ctx)->stop();
    return JS_UNDEFINED;
}

// toast(msg, durationMs?)
static JSValue js_toast(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    auto* api = getApi(ctx);
    if (argc < 1) return JS_UNDEFINED;
    std::string msg = jsToStdString(ctx, argv[0]);
    int duration = 3000;
    if (argc > 1) JS_ToInt32(ctx, &duration, argv[1]);
    api->toast(msg, duration);
    return JS_UNDEFINED;
}

// setGlobal(key, value)
static JSValue js_setGlobal(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    auto* api = getApi(ctx);
    if (argc < 2) return JS_UNDEFINED;
    std::string key = jsToStdString(ctx, argv[0]);
    ScriptValue value = jsToScriptValue(ctx, argv[1]);
    api->setGlobal(key, value);
    return JS_UNDEFINED;
}

// getGlobal(key) → value
static JSValue js_getGlobal(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    auto* api = getApi(ctx);
    if (argc < 1) return JS_UNDEFINED;
    std::string key = jsToStdString(ctx, argv[0]);
    ScriptValue value = api->getGlobal(key);
    return scriptValueToJS(ctx, value);
}

// loadModule(path) → module namespace
static JSValue js_loadModule(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    auto* api = getApi(ctx);
    if (argc < 1) return JS_UNDEFINED;
    std::string path = jsToStdString(ctx, argv[0]);
    std::string fullPath = api->resolveModulePath(path);

    // 检查缓存
    auto it = s_moduleCache.find(fullPath);
    if (it != s_moduleCache.end()) {
        return JS_DupValue(ctx, it->second);
    }

    namespace fs = std::filesystem;
    fs::path fsFullPath(strutil::toWide(fullPath));
    if (!fs::exists(fsFullPath)) {
        LOGW() << "[JsBindings loadModule] File not found:" << fullPath;
        return JS_UNDEFINED;
    }

    // 读取文件
    std::ifstream ifs(fsFullPath, std::ios::binary);
    if (!ifs) {
        LOGW() << "[JsBindings loadModule] Cannot open:" << fullPath;
        return JS_UNDEFINED;
    }
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    ifs.close();

    // 编译为模块
    JSValue func_val = JS_Eval(ctx, content.data(), content.size(),
                               fullPath.c_str(),
                               JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(func_val)) {
        LOGW() << "[JsBindings loadModule] Compile error:" << fullPath;
        return JS_EXCEPTION;
    }

    // 获取模块定义（在 JS_EvalFunction 消耗 func_val 之前）
    JSModuleDef* m = (JSModuleDef*)JS_VALUE_GET_PTR(func_val);

    // 执行模块
    JSValue eval_result = JS_EvalFunction(ctx, func_val);
    if (JS_IsException(eval_result)) {
        LOGW() << "[JsBindings loadModule] Eval error:" << fullPath;
        return JS_EXCEPTION;
    }
    JS_FreeValue(ctx, eval_result);

    // 获取模块命名空间（导出对象）
    JSValue ns = JS_GetModuleNamespace(ctx, m);

    // 缓存（增加引用计数）
    s_moduleCache[fullPath] = JS_DupValue(ctx, ns);
    s_moduleCacheCtx = ctx;

    return ns;  // 调用者拥有该引用
}

// log(msg)
static JSValue js_log(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    auto* api = getApi(ctx);
    if (argc < 1) return JS_UNDEFINED;
    std::string msg = jsToStdString(ctx, argv[0]);
    api->log(msg);
    return JS_UNDEFINED;
}

// shotmode(gameMode)
static JSValue js_shotmode(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    auto* api = getApi(ctx);
    if (argc < 1) return JS_UNDEFINED;
    bool gameMode = JS_ToBool(ctx, argv[0]);
    api->shotmode(gameMode);
    return JS_UNDEFINED;
}

// setRadialParam(up, down, left, right)
static JSValue js_setRadialParam(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    auto* api = getApi(ctx);
    double up = 1, down = 1, left = 1, right = 1;
    if (argc > 0) JS_ToFloat64(ctx, &up, argv[0]);
    if (argc > 1) JS_ToFloat64(ctx, &down, argv[1]);
    if (argc > 2) JS_ToFloat64(ctx, &left, argv[2]);
    if (argc > 3) JS_ToFloat64(ctx, &right, argv[3]);
    api->setRadialParam(up, down, left, right);
    return JS_UNDEFINED;
}

// resetview()
static JSValue js_resetview(JSContext* ctx, JSValueConst, int, JSValueConst*)
{
    getApi(ctx)->resetview();
    return JS_UNDEFINED;
}

// resetwheel()
static JSValue js_resetwheel(JSContext* ctx, JSValueConst, int, JSValueConst*)
{
    getApi(ctx)->resetwheel();
    return JS_UNDEFINED;
}

// getmousepos() → {x, y}
static JSValue js_getmousepos(JSContext* ctx, JSValueConst, int, JSValueConst*)
{
    PosResult result = getApi(ctx)->getmousepos();
    return posResultToJS(ctx, result);
}

// getkeypos(keyName) → {x, y, valid}
static JSValue js_getkeypos(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    if (argc < 1) return JS_UNDEFINED;
    std::string keyName = jsToStdString(ctx, argv[0]);
    KeyPosResult result = getApi(ctx)->getkeypos(keyName);
    return keyPosResultToJS(ctx, result);
}

// getbuttonpos(buttonId) → {x, y, valid, name}
static JSValue js_getbuttonpos(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    if (argc < 1) return JS_UNDEFINED;
    int id = 0;
    JS_ToInt32(ctx, &id, argv[0]);
    ButtonPosResult result = getApi(ctx)->getbuttonpos(id);
    return buttonPosResultToJS(ctx, result);
}

// getKeyState(keyName) → int
static JSValue js_getKeyState(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    if (argc < 1) return JS_NewInt32(ctx, 0);
    std::string keyName = jsToStdString(ctx, argv[0]);
    return JS_NewInt32(ctx, getApi(ctx)->getKeyState(keyName));
}

// setKeyUIPos(keyName, x, y, xoffset?, yoffset?)
static JSValue js_setKeyUIPos(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    auto* api = getApi(ctx);
    if (argc < 3) return JS_UNDEFINED;
    std::string keyName = jsToStdString(ctx, argv[0]);
    double x = 0, y = 0, xoff = 0, yoff = 0;
    JS_ToFloat64(ctx, &x, argv[1]);
    JS_ToFloat64(ctx, &y, argv[2]);
    if (argc > 3) JS_ToFloat64(ctx, &xoff, argv[3]);
    if (argc > 4) JS_ToFloat64(ctx, &yoff, argv[4]);
    api->setKeyUIPos(keyName, x, y, xoff, yoff);
    return JS_UNDEFINED;
}

// findImage(imageName, x1?, y1?, x2?, y2?, threshold?)
static JSValue js_findImage(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    auto* api = getApi(ctx);
    if (argc < 1) return JS_UNDEFINED;
    std::string imageName = jsToStdString(ctx, argv[0]);
    double x1 = 0, y1 = 0, x2 = 1, y2 = 1, threshold = 0.8;
    if (argc > 1) JS_ToFloat64(ctx, &x1, argv[1]);
    if (argc > 2) JS_ToFloat64(ctx, &y1, argv[2]);
    if (argc > 3) JS_ToFloat64(ctx, &x2, argv[3]);
    if (argc > 4) JS_ToFloat64(ctx, &y2, argv[4]);
    if (argc > 5) JS_ToFloat64(ctx, &threshold, argv[5]);
    FindImageResult result = api->findImage(imageName, x1, y1, x2, y2, threshold);
    return findImageResultToJS(ctx, result);
}

// findImageByRegion(imageName, regionId, threshold?)
static JSValue js_findImageByRegion(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    auto* api = getApi(ctx);
    if (argc < 2) return JS_UNDEFINED;
    std::string imageName = jsToStdString(ctx, argv[0]);
    int regionId = 0;
    JS_ToInt32(ctx, &regionId, argv[1]);
    double threshold = 0.8;
    if (argc > 2) JS_ToFloat64(ctx, &threshold, argv[2]);
    FindImageResult result = api->findImageByRegion(imageName, regionId, threshold);
    return findImageResultToJS(ctx, result);
}

// swipeById(swipeId, durationMs?, steps?)
static JSValue js_swipeById(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    auto* api = getApi(ctx);
    if (argc < 1) return JS_UNDEFINED;
    int swipeId = 0;
    JS_ToInt32(ctx, &swipeId, argv[0]);
    int duration = 200, steps = 10;
    if (argc > 1) JS_ToInt32(ctx, &duration, argv[1]);
    if (argc > 2) JS_ToInt32(ctx, &steps, argv[2]);
    api->swipeById(swipeId, duration, steps);
    return JS_UNDEFINED;
}

// logerror(err) — 全局函数
static JSValue js_logerror(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
    if (argc < 1) return JS_UNDEFINED;
    std::string msg = jsToStdString(ctx, argv[0]);
    LOG_W("[Script Error] %s", msg.c_str());
    return JS_UNDEFINED;
}

// ============================================================
// 注册所有绑定
// ============================================================

void registerApiBindings(JSContext* ctx, SandboxScriptApi* api)
{
    JS_SetContextOpaque(ctx, api);

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue mapi = JS_NewObject(ctx);

    // 注册 28 个 API 方法
    JS_SetPropertyStr(ctx, mapi, "click",            JS_NewCFunction(ctx, js_click, "click", 2));
    JS_SetPropertyStr(ctx, mapi, "holdpress",        JS_NewCFunction(ctx, js_holdpress, "holdpress", 2));
    JS_SetPropertyStr(ctx, mapi, "release",          JS_NewCFunction(ctx, js_release, "release", 0));
    JS_SetPropertyStr(ctx, mapi, "slide",            JS_NewCFunction(ctx, js_slide, "slide", 6));
    JS_SetPropertyStr(ctx, mapi, "pinch",            JS_NewCFunction(ctx, js_pinch, "pinch", 5));
    JS_SetPropertyStr(ctx, mapi, "isPress",          JS_NewCFunction(ctx, js_isPress, "isPress", 0));
    JS_SetPropertyStr(ctx, mapi, "key",              JS_NewCFunction(ctx, js_key, "key", 2));
    JS_SetPropertyStr(ctx, mapi, "releaseAll",       JS_NewCFunction(ctx, js_releaseAll, "releaseAll", 0));
    JS_SetPropertyStr(ctx, mapi, "sleep",            JS_NewCFunction(ctx, js_sleep, "sleep", 1));
    JS_SetPropertyStr(ctx, mapi, "isInterrupted",    JS_NewCFunction(ctx, js_isInterrupted, "isInterrupted", 0));
    JS_SetPropertyStr(ctx, mapi, "stop",             JS_NewCFunction(ctx, js_stop, "stop", 0));
    JS_SetPropertyStr(ctx, mapi, "toast",            JS_NewCFunction(ctx, js_toast, "toast", 2));
    JS_SetPropertyStr(ctx, mapi, "setGlobal",        JS_NewCFunction(ctx, js_setGlobal, "setGlobal", 2));
    JS_SetPropertyStr(ctx, mapi, "getGlobal",        JS_NewCFunction(ctx, js_getGlobal, "getGlobal", 1));
    JS_SetPropertyStr(ctx, mapi, "loadModule",       JS_NewCFunction(ctx, js_loadModule, "loadModule", 1));
    JS_SetPropertyStr(ctx, mapi, "log",              JS_NewCFunction(ctx, js_log, "log", 1));
    JS_SetPropertyStr(ctx, mapi, "shotmode",         JS_NewCFunction(ctx, js_shotmode, "shotmode", 1));
    JS_SetPropertyStr(ctx, mapi, "setRadialParam",   JS_NewCFunction(ctx, js_setRadialParam, "setRadialParam", 4));
    JS_SetPropertyStr(ctx, mapi, "resetview",        JS_NewCFunction(ctx, js_resetview, "resetview", 0));
    JS_SetPropertyStr(ctx, mapi, "resetwheel",       JS_NewCFunction(ctx, js_resetwheel, "resetwheel", 0));
    JS_SetPropertyStr(ctx, mapi, "getmousepos",      JS_NewCFunction(ctx, js_getmousepos, "getmousepos", 0));
    JS_SetPropertyStr(ctx, mapi, "getkeypos",        JS_NewCFunction(ctx, js_getkeypos, "getkeypos", 1));
    JS_SetPropertyStr(ctx, mapi, "getbuttonpos",     JS_NewCFunction(ctx, js_getbuttonpos, "getbuttonpos", 1));
    JS_SetPropertyStr(ctx, mapi, "getKeyState",      JS_NewCFunction(ctx, js_getKeyState, "getKeyState", 1));
    JS_SetPropertyStr(ctx, mapi, "setKeyUIPos",      JS_NewCFunction(ctx, js_setKeyUIPos, "setKeyUIPos", 5));
    JS_SetPropertyStr(ctx, mapi, "findImage",        JS_NewCFunction(ctx, js_findImage, "findImage", 6));
    JS_SetPropertyStr(ctx, mapi, "findImageByRegion",JS_NewCFunction(ctx, js_findImageByRegion, "findImageByRegion", 3));
    JS_SetPropertyStr(ctx, mapi, "swipeById",        JS_NewCFunction(ctx, js_swipeById, "swipeById", 3));

    JS_SetPropertyStr(ctx, global, "mapi", mapi);

    // 注册 logerror 全局函数
    JS_SetPropertyStr(ctx, global, "logerror",
                      JS_NewCFunction(ctx, js_logerror, "logerror", 1));

    JS_FreeValue(ctx, global);
}
