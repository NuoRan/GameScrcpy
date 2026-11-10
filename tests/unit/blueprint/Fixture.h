#pragma once

#include <gs/blueprint/Document.h>
#include <gs/blueprint/Environment.h>
#include <gs/blueprint/Layout.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace blueprint_test {

using Json = nlohmann::ordered_json;

inline std::string readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

inline std::string lf(std::string text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            continue;
        }
        out.push_back(text[i]);
    }
    return out;
}

inline std::string fixture(std::string_view name)
{
    return lf(readFile(std::filesystem::path(GS_BLUEPRINT_DATA_DIR) / name));
}

inline std::filesystem::path dataDir()
{
    return std::filesystem::path(GS_BLUEPRINT_DATA_DIR);
}

inline std::shared_ptr<const gs::blueprint::Api> api()
{
    static const std::shared_ptr<const gs::blueprint::Api> loaded = gs::blueprint::loadApi(
        readFile(std::filesystem::path(GS_SDK_DIR) / "api-report.json"),
        readFile(std::filesystem::path(GS_SDK_DIR) / "types" / "gamescrcpy.d.ts"));
    return loaded;
}

class FakeLabels final : public gs::blueprint::Labels
{
public:
    std::map<std::string, std::string, std::less<>> chinese;
    std::map<std::string, std::string, std::less<>> englishTexts;
    std::map<std::string, std::string, std::less<>> keys;

    [[nodiscard]] std::string text(std::string_view key) const override
    {
        const auto found = chinese.find(key);
        return found == chinese.end() ? std::string() : found->second;
    }

    [[nodiscard]] std::string english(std::string_view key) const override
    {
        const auto found = englishTexts.find(key);
        return found == englishTexts.end() ? std::string() : found->second;
    }

    [[nodiscard]] std::string keyName(std::string_view key) const override
    {
        const auto found = keys.find(key);
        return found == keys.end() ? std::string(key) : found->second;
    }
};

inline const FakeLabels& labels()
{
    static const FakeLabels instance = [] {
        FakeLabels made;
        made.chinese = {
            {"node:key", "按键 %1"},
            {"badge:Key.onTap", "单击"},
            {"badge:Key.onLongPress", "长按 %1 秒"},
            {"badge:Key.onPress", "按下"},
            {"badge:Key.onRelease", "松开"},
            {"badge:Key.whileHeld", "按住时"},
            {"badge:Key.onDoubleTap", "双击"},
            {"every", "每隔"},
            {"unit:seconds", "%1 秒"},
            {"unit:s", "秒"},
            {"Tap.tap", "点一下"},
            {"Turbo.start", "开始"},
            {"Turbo.stop", "停止"},
            {"Component.press", "按下"},
            {"Component.release", "松开"},
            {"Component.hold", "按住一会儿"},
            {"Component.trigger", "点一下再松开"},
            {"Component.enable", "打开"},
            {"Component.touching", "摸着屏幕"},
            {"Component.enabled", "开着"},
            {"Component.position", "位置"},
            {"Component.heldMs", "按了多久"},
            {"Hold.holding", "按着中"},
            {"Hold.onPressed", "按下时"},
            {"Hold.onReleased", "松开时"},
            {"touch.tap", "点某处"},
            {"vision.find", "找图"},
            {"node:find", "找图"},
            {"node:waitFor", "等到出现"},
            {"pin:found", "找到"},
            {"pin:notFound", "没找到"},
            {"pin:appeared", "出现了"},
            {"pin:timedOut", "超时"},
            {"pin:then", "然后"},
            {"pin:true", "是"},
            {"pin:false", "否"},
            {"pin:condition", "条件"},
            {"pin:value", "值"},
            {"node:if", "判断"},
            {"node:repeat", "重复"},
            {"node:forOf", "对每个"},
            {"node:while", "当…时重复"},
            {"node:break", "跳出循环"},
            {"node:continue", "下一次"},
            {"node:return", "返回"},
            {"node:super", "调用父类 · %1"},
            {"node:varGet", "变量 %1"},
            {"node:setVar", "设置 变量 %1"},
            {"node:set", "设置 %1"},
            {"node:get", "取 %1"},
            {"node:load", "载入时"},
            {"node:code", "代码块"},
            {"node:wait", "等待"},
            {"node:setup", "准备好时"},
            {"node:run", "任务开始时"},
            {"node:override", "覆盖"},
            {"node:self", "自己"},
            {"node:keyEvent", "按键"},
            {"param:image", "图片"},
            {"param:touch.tap.x", "位置"},
            {"page:main", "主页"},
            {"group:events", "事件"},
            {"group:components", "组件"},
            {"group:touch", "触摸"},
            {"group:vision", "识图"},
            {"group:flow", "流程"},
            {"members:property", "属性"},
            {"members:state", "状态"},
            {"members:event", "事件"},
            {"members:function", "函数"},
            {"value:true", "是"},
            {"value:false", "否"},
            {"reason:async", "只能停在 async 函数里"},
            {"node:machine", "状态机"},
            {"node:enterState", "进入 %1"},
            {"node:goto", "转到 %1"},
            {"node:finish", "结束状态机"},
            {"name:state", "新状态"},
            {"reason:leaves", "“%1”最后已经转走了，到它里面去加判断"},
            {"reason:onlyStates", "状态机里只能放状态"},
            {"reason:unknownTarget", "转到的“%1”不存在"},
            {"reason:noValue", "“%1”没有值：接一根线或填一个值"},
            {"problem:syntax", "代码第 %1 行有语法错误：%2。改好了才能画成蓝图"},
            {"node:switch", "多分支"},
            {"node:sequence", "顺序"},
            {"node:multiGate", "多路门"},
            {"node:forRange", "按范围重复"},
            {"node:forEachIndex", "带序号的对每个"},
            {"pin:from", "从"},
            {"pin:to", "到"},
            {"pin:name", "名字"},
            {"pin:loop", "循环"},
            {"pin:random", "随机"},
            {"pin:done", "做完"},
            {"pin:progress", "进度"},
            {"pin:failed", "失败"},
            {"pin:fraction", "比例"},
            {"pin:status", "状态"},
            {"pin:error", "错误"},
            {"badge:job", "任务"},
            {"pin:default", "其他"},
            {"detail:cases", "分支值"},
            {"reason:cases", "按方块的分支个数写，用逗号分开"},
            {"name:gate", "门"},
            {"group:text", "文字"},
            {"group:list", "列表"},
            {"group:dict", "字典"},
            {"group:set", "集合"},
            {"group:math", "数学"},
            {"problem:snippet", "这段游离代码有错：%1。在这里改好，或者删掉这个方块"},
            {"reason:wireBlock", "只有没接任何线的方块才能放到线上"},
            {"reason:noControl", "找不到组件“%1”：已被删除"},
            {"variable:type:toggle", "开关"},
            {"variable:type:integer", "整数"},
            {"variable:type:decimal", "小数"},
            {"variable:type:point", "位置"},
            {"variable:type:duration", "时长"},
            {"variable:listOf", "%1列表"},
            {"reason:variableUses", "“%1”还在用，改掉它先"},
            {"reason:variableExists", "这里已经有同名的变量了"},
            {"reason:variableName", "变量名只能用字母、数字和 _"},
            {"section:structs", "结构"},
            {"section:enums", "枚举"},
            {"section:classes", "类"},
            {"badge:struct", "结构"},
            {"badge:enum", "枚举"},
            {"group:types", "结构与类"},
            {"node:make", "合成 %1"},
            {"node:breakStruct", "拆开 %1"},
            {"node:switchOn", "按选项分支 %1"},
            {"node:new", "新建 %1"},
            {"node:cast", "转换为 %1"},
            {"node:constructor", "构造函数"},
            {"node:emit", "触发 %1"},
            {"node:bind", "绑定 %1"},
            {"node:unbind", "解绑 %1"},
            {"pin:object", "对象"},
            {"pin:is", "是"},
            {"pin:isNot", "不是"},
            {"pin:other", "其它"},
            {"name:struct", "新结构"},
            {"name:enum", "新枚举"},
            {"name:class", "新类"},
            {"name:field", "字段"},
            {"name:option", "选项"},
            {"name:param", "参数"},
            {"name:event", "事件"},
            {"name:subscription", "订阅"},
            {"name:object", "对象"},
            {"reason:noField", "“%1”没有这个字段"},
            {"reason:noMember", "“%1”没有这个成员"},
            {"reason:noOption", "“%1”不是这个枚举的选项"},
            {"reason:lastField", "结构至少要有一个字段"},
            {"reason:lastOption", "枚举至少要有一个选项"},
            {"reason:typeTaken", "已经有叫“%1”的东西了"},
            {"reason:typeName", "“%1”不是能用的名字"},
        };
        made.englishTexts = {
            {"vision.find", "Find image"},
            {"touch.tap", "Tap at"},
            {"node:find", "Find image"},
            {"node:if", "Branch"},
            {"node:switch", "Multi-branch"},
            {"node:sequence", "Sequence"},
            {"node:multiGate", "Multi-gate"},
            {"node:forRange", "For a range"},
            {"node:forEachIndex", "For each with index"},
            {"node:make", "Make %1"},
            {"node:breakStruct", "Break %1"},
            {"Tap.tap", "Tap"},
            {"reason:ignoredValue", "This call passes values that are not used"},
        };
        made.keys = {{"F", "F"}, {"MouseLeft", "左键"}};
        return made;
    }();
    return instance;
}

inline gs::blueprint::Environment environment(std::string fileName = "logic.ts")
{
    gs::blueprint::Environment made;
    made.api = api();
    made.labels = &labels();
    made.controls = {
        {"拾取", "拾取", "Tap"}, {"背包", "背包", "Tap"}, {"开镜", "开镜", "Hold"},
        {"连发", "连发", "Turbo"}, {"开火", "开火", "Hold"}, {"移动", "移动", "Wasd"},
    };
    made.vars = {{"在车上", "在车上", "toggle"}};
    made.images = {{"拾取", "file:///res/pick.png"}};
    made.fileName = std::move(fileName);
    return made;
}

inline bool same(const Json& value, std::string_view text)
{
    return value.is_string() && value.get<std::string>() == text;
}

inline const Json* nodeTitled(const Json& graph, std::string_view title, std::string_view badge = {})
{
    for (const Json& node : graph["nodes"]) {
        if (same(node["title"], title) && (badge.empty() || same(node["badge"], badge))) {
            return &node;
        }
    }
    return nullptr;
}

inline std::size_t countTitled(const Json& graph, std::string_view title)
{
    std::size_t count = 0;
    for (const Json& node : graph["nodes"]) {
        if (same(node["title"], title)) {
            ++count;
        }
    }
    return count;
}

inline const Json* pinNamed(const Json& node, std::string_view side, std::string_view name)
{
    for (const Json& pin : node[std::string(side)]) {
        if (same(pin["name"], name)) {
            return &pin;
        }
    }
    return nullptr;
}

inline bool wired(const Json& graph, const Json* from, std::string_view fromPin, const Json* to, std::string_view toPin)
{
    if (from == nullptr || to == nullptr) {
        return false;
    }
    for (const Json& wire : graph["wires"]) {
        if (wire["from"] == (*from)["key"] && same(wire["fromPin"], fromPin) && wire["to"] == (*to)["key"]
            && same(wire["toPin"], toPin)) {
            return true;
        }
    }
    return false;
}

inline std::string keyOf(const Json* node)
{
    return node == nullptr ? std::string() : (*node)["key"].get<std::string>();
}

}
