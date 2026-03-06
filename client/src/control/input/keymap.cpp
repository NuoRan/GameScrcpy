#define LOG_TAG "KeyMap"
#include "Logger.h"



#include "keymap.h"

#include <algorithm>
#include <sstream>
#include <cctype>

namespace {
// Trim whitespace from both ends
std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Convert to lowercase
std::string toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

// Split string, skip empty parts
std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> result;
    std::istringstream iss(s);
    std::string token;
    while (std::getline(iss, token, delim)) {
        if (!token.empty()) result.push_back(token);
    }
    return result;
}

// Case-insensitive string comparison
bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    return std::equal(a.begin(), a.end(), b.begin(),
        [](unsigned char ca, unsigned char cb) { return std::tolower(ca) == std::tolower(cb); });
}
} // anonymous namespace

// ============================================================
// Static lookup: GameMouse button name → int value
// Replaces manual lookup table
// ============================================================
static int mouseButtonFromName(const char* name) {
    if (!name) return -1;
    static const std::unordered_map<std::string, int> lookup = {
        {"NoButton",      GameMouse::NoButton},
        {"LeftButton",    GameMouse::LeftButton},
        {"RightButton",   GameMouse::RightButton},
        {"MiddleButton",  GameMouse::MiddleButton},
        {"BackButton",    GameMouse::BackButton},
        {"ForwardButton", GameMouse::ForwardButton},
        {"XButton1",      GameMouse::XButton1},
        {"XButton2",      GameMouse::XButton2},
    };
    auto it = lookup.find(name);
    return (it != lookup.end()) ? it->second : -1;
}

// ============================================================
// Static lookup: GameKey name → int value
// Replaces manual lookup table
// ============================================================
static int qtKeyFromName(const char* name) {
    if (!name) return -1;
    static const std::unordered_map<std::string, int> lookup = {
        // Navigation / control
        {"Key_Escape",     GameKey::Key_Escape},
        {"Key_Tab",        GameKey::Key_Tab},
        {"Key_Backtab",    GameKey::Key_Backtab},
        {"Key_Backspace",  GameKey::Key_Backspace},
        {"Key_Return",     GameKey::Key_Return},
        {"Key_Enter",      GameKey::Key_Enter},
        {"Key_Insert",     GameKey::Key_Insert},
        {"Key_Delete",     GameKey::Key_Delete},
        {"Key_Pause",      GameKey::Key_Pause},
        {"Key_Print",      GameKey::Key_Print},
        {"Key_SysReq",     GameKey::Key_SysReq},
        {"Key_Clear",      GameKey::Key_Clear},
        {"Key_Home",       GameKey::Key_Home},
        {"Key_End",        GameKey::Key_End},
        {"Key_Left",       GameKey::Key_Left},
        {"Key_Up",         GameKey::Key_Up},
        {"Key_Right",      GameKey::Key_Right},
        {"Key_Down",       GameKey::Key_Down},
        {"Key_PageUp",     GameKey::Key_PageUp},
        {"Key_PageDown",   GameKey::Key_PageDown},
        // Modifiers
        {"Key_Shift",      GameKey::Key_Shift},
        {"Key_Control",    GameKey::Key_Control},
        {"Key_Meta",       GameKey::Key_Meta},
        {"Key_Alt",        GameKey::Key_Alt},
        {"Key_CapsLock",   GameKey::Key_CapsLock},
        {"Key_NumLock",    GameKey::Key_NumLock},
        {"Key_ScrollLock", GameKey::Key_ScrollLock},
        // Function keys
        {"Key_F1",  GameKey::Key_F1},  {"Key_F2",  GameKey::Key_F2},  {"Key_F3",  GameKey::Key_F3},
        {"Key_F4",  GameKey::Key_F4},  {"Key_F5",  GameKey::Key_F5},  {"Key_F6",  GameKey::Key_F6},
        {"Key_F7",  GameKey::Key_F7},  {"Key_F8",  GameKey::Key_F8},  {"Key_F9",  GameKey::Key_F9},
        {"Key_F10", GameKey::Key_F10}, {"Key_F11", GameKey::Key_F11}, {"Key_F12", GameKey::Key_F12},
        {"Key_F13", GameKey::Key_F13}, {"Key_F14", GameKey::Key_F14}, {"Key_F15", GameKey::Key_F15},
        {"Key_F16", GameKey::Key_F16}, {"Key_F17", GameKey::Key_F17}, {"Key_F18", GameKey::Key_F18},
        {"Key_F19", GameKey::Key_F19}, {"Key_F20", GameKey::Key_F20}, {"Key_F21", GameKey::Key_F21},
        {"Key_F22", GameKey::Key_F22}, {"Key_F23", GameKey::Key_F23}, {"Key_F24", GameKey::Key_F24},
        // Special keys
        {"Key_Super_L", GameKey::Key_Super_L}, {"Key_Super_R", GameKey::Key_Super_R},
        {"Key_Menu",    GameKey::Key_Menu},
        // ASCII / Latin
        {"Key_Space",        GameKey::Key_Space},
        {"Key_Exclam",       GameKey::Key_Exclam},
        {"Key_QuoteDbl",     GameKey::Key_QuoteDbl},
        {"Key_NumberSign",   GameKey::Key_NumberSign},
        {"Key_Dollar",       GameKey::Key_Dollar},
        {"Key_Percent",      GameKey::Key_Percent},
        {"Key_Ampersand",    GameKey::Key_Ampersand},
        {"Key_Apostrophe",   GameKey::Key_Apostrophe},
        {"Key_ParenLeft",    GameKey::Key_ParenLeft},
        {"Key_ParenRight",   GameKey::Key_ParenRight},
        {"Key_Asterisk",     GameKey::Key_Asterisk},
        {"Key_Plus",         GameKey::Key_Plus},
        {"Key_Comma",        GameKey::Key_Comma},
        {"Key_Minus",        GameKey::Key_Minus},
        {"Key_Period",       GameKey::Key_Period},
        {"Key_Slash",        GameKey::Key_Slash},
        {"Key_Colon",        GameKey::Key_Colon},
        {"Key_Semicolon",    GameKey::Key_Semicolon},
        {"Key_Less",         GameKey::Key_Less},
        {"Key_Equal",        GameKey::Key_Equal},
        {"Key_Greater",      GameKey::Key_Greater},
        {"Key_Question",     GameKey::Key_Question},
        {"Key_At",           GameKey::Key_At},
        {"Key_BracketLeft",  GameKey::Key_BracketLeft},
        {"Key_Backslash",    GameKey::Key_Backslash},
        {"Key_BracketRight", GameKey::Key_BracketRight},
        {"Key_AsciiCircum",  GameKey::Key_AsciiCircum},
        {"Key_Underscore",   GameKey::Key_Underscore},
        {"Key_QuoteLeft",    GameKey::Key_QuoteLeft},
        {"Key_BraceLeft",    GameKey::Key_BraceLeft},
        {"Key_Bar",          GameKey::Key_Bar},
        {"Key_BraceRight",   GameKey::Key_BraceRight},
        {"Key_AsciiTilde",   GameKey::Key_AsciiTilde},
        // Letters
        {"Key_A", GameKey::Key_A}, {"Key_B", GameKey::Key_B}, {"Key_C", GameKey::Key_C},
        {"Key_D", GameKey::Key_D}, {"Key_E", GameKey::Key_E}, {"Key_F", GameKey::Key_F},
        {"Key_G", GameKey::Key_G}, {"Key_H", GameKey::Key_H}, {"Key_I", GameKey::Key_I},
        {"Key_J", GameKey::Key_J}, {"Key_K", GameKey::Key_K}, {"Key_L", GameKey::Key_L},
        {"Key_M", GameKey::Key_M}, {"Key_N", GameKey::Key_N}, {"Key_O", GameKey::Key_O},
        {"Key_P", GameKey::Key_P}, {"Key_Q", GameKey::Key_Q}, {"Key_R", GameKey::Key_R},
        {"Key_S", GameKey::Key_S}, {"Key_T", GameKey::Key_T}, {"Key_U", GameKey::Key_U},
        {"Key_V", GameKey::Key_V}, {"Key_W", GameKey::Key_W}, {"Key_X", GameKey::Key_X},
        {"Key_Y", GameKey::Key_Y}, {"Key_Z", GameKey::Key_Z},
        // Numbers
        {"Key_0", GameKey::Key_0}, {"Key_1", GameKey::Key_1}, {"Key_2", GameKey::Key_2},
        {"Key_3", GameKey::Key_3}, {"Key_4", GameKey::Key_4}, {"Key_5", GameKey::Key_5},
        {"Key_6", GameKey::Key_6}, {"Key_7", GameKey::Key_7}, {"Key_8", GameKey::Key_8},
        {"Key_9", GameKey::Key_9},
        // Sentinel
        {"Key_unknown", GameKey::Key_unknown},
    };
    auto it = lookup.find(name);
    return (it != lookup.end()) ? it->second : -1;
}

// Static conversion: string → KeyMapType (replaces Q_ENUM + QMetaEnum)
KeyMap::KeyMapType KeyMap::keyMapTypeFromString(const char* name) {
    if (!name) return KMT_INVALID;
    struct Entry { const char* str; KeyMapType val; };
    static const Entry table[] = {
        {"KMT_STEER_WHEEL", KMT_STEER_WHEEL},
        {"KMT_MOUSE_MOVE",  KMT_MOUSE_MOVE},
        {"KMT_ANDROID_KEY", KMT_ANDROID_KEY},
        {"KMT_SCRIPT",      KMT_SCRIPT},
        {"KMT_CAMERA_MOVE", KMT_CAMERA_MOVE},
        {"KMT_FREE_LOOK",   KMT_FREE_LOOK},
    };
    for (const auto& e : table) {
        if (strcmp(name, e.str) == 0) return e.val;
    }
    return KMT_INVALID;
}



KeyMap::KeyMap() {}



KeyMap::~KeyMap() {}



void KeyMap::loadKeyMap(const std::string &json)

{

    std::string errorString;

    nlohmann::json rootObj;



    // 初始化

    m_idxSteerWheel = -1;

    m_idxMouseMove = -1;

    m_keyMapNodes.clear();



    // 默认开关键

    m_switchKey = { AT_KEY, GameKey::Key_QuoteLeft };



    try {

        rootObj = nlohmann::json::parse(json);

    } catch (const nlohmann::json::parse_error &e) {

        errorString = std::string("json error: ") + e.what();

        goto parseError;

    }



    // 尝试读取根节点的 switchKey

    if (checkItemString(rootObj, "switchKey")) {

        ParsedKey sk = getItemKey(rootObj, "switchKey");

        if (sk.type != AT_INVALID) {

            m_switchKey.type = sk.type;

            m_switchKey.key = sk.key;

        }

    }



    // 兼容旧的 mouseMoveMap 配置

    if (checkItemObject(rootObj, "mouseMoveMap")) {

        nlohmann::json mouseMoveMap = getItemObject(rootObj, "mouseMoveMap");

        KeyMapNode keyMapNode;

        keyMapNode.type = KMT_MOUSE_MOVE;



        bool have_speedRatio = false;



        if (checkItemDouble(mouseMoveMap, "speedRatio")) {

            float ratio = static_cast<float>(getItemDouble(mouseMoveMap, "speedRatio"));

            keyMapNode.data.mouseMove.speedRatio.x = ratio;

            keyMapNode.data.mouseMove.speedRatio.y = ratio / 2.25f;

            have_speedRatio = true;

        }



        if (checkItemDouble(mouseMoveMap, "speedRatioX")) {

            keyMapNode.data.mouseMove.speedRatio.x = static_cast<float>(getItemDouble(mouseMoveMap, "speedRatioX"));

            have_speedRatio = true;

        }



        if (checkItemDouble(mouseMoveMap, "speedRatioY")) {

            keyMapNode.data.mouseMove.speedRatio.y = static_cast<float>(getItemDouble(mouseMoveMap, "speedRatioY"));

            have_speedRatio = true;

        }



        if (have_speedRatio && checkItemObject(mouseMoveMap, "startPos")) {

            keyMapNode.data.mouseMove.startPos = getItemPos(mouseMoveMap, "startPos");

            m_idxMouseMove = static_cast<int>(m_keyMapNodes.size());

            m_keyMapNodes.push_back(keyMapNode);

        }

    }



    // keyMapNodes 解析

    if (rootObj.contains("keyMapNodes") && rootObj["keyMapNodes"].is_array()) {

        const auto& keyMapNodes = rootObj["keyMapNodes"];

        int size = static_cast<int>(keyMapNodes.size());

        for (int i = 0; i < size; i++) {

            if (!keyMapNodes[i].is_object()) continue;

            const auto& node = keyMapNodes[i];

            if (!node.contains("type") || !node["type"].is_string()) continue;



            KeyMap::KeyMapType type = getItemKeyMapType(node, "type");

            switch (type) {



            case KeyMap::KMT_STEER_WHEEL: {

                if (!checkForSteerWheel(node)) {

                    LOGW() << "json error: format error (steerWheel)";

                    break;

                }

                ParsedKey leftKey = getItemKey(node, "leftKey");

                ParsedKey rightKey = getItemKey(node, "rightKey");

                ParsedKey upKey = getItemKey(node, "upKey");

                ParsedKey downKey = getItemKey(node, "downKey");



                if (leftKey.type == AT_INVALID || rightKey.type == AT_INVALID || upKey.type == AT_INVALID || downKey.type == AT_INVALID) break;



                KeyMapNode keyMapNode;

                keyMapNode.type = type;

                keyMapNode.data.steerWheel.left = { leftKey.type, leftKey.key, leftKey.modifiers, PointF(0, 0), PointF(0, 0), getItemDouble(node, "leftOffset") };

                keyMapNode.data.steerWheel.right = { rightKey.type, rightKey.key, rightKey.modifiers, PointF(0, 0), PointF(0, 0), getItemDouble(node, "rightOffset") };

                keyMapNode.data.steerWheel.up = { upKey.type, upKey.key, upKey.modifiers, PointF(0, 0), PointF(0, 0), getItemDouble(node, "upOffset") };

                keyMapNode.data.steerWheel.down = { downKey.type, downKey.key, downKey.modifiers, PointF(0, 0), PointF(0, 0), getItemDouble(node, "downOffset") };

                keyMapNode.data.steerWheel.centerPos = getItemPos(node, "centerPos");

                m_idxSteerWheel = static_cast<int>(m_keyMapNodes.size());

                m_keyMapNodes.push_back(keyMapNode);

            } break;



            case KeyMap::KMT_ANDROID_KEY: {

                if (!checkForAndroidKey(node)) break;

                ParsedKey key = getItemKey(node, "key");

                if (key.type == AT_INVALID) break;



                KeyMapNode keyMapNode;

                keyMapNode.type = type;

                keyMapNode.data.androidKey.keyNode.type = key.type;

                keyMapNode.data.androidKey.keyNode.key = key.key;

                keyMapNode.data.androidKey.keyNode.modifiers = key.modifiers;

                keyMapNode.data.androidKey.keyNode.androidKey = static_cast<AndroidKeycode>(getItemDouble(node, "androidKey"));

                m_keyMapNodes.push_back(keyMapNode);

            } break;



            case KeyMap::KMT_SCRIPT: {

                if (!checkForScript(node)) break;

                ParsedKey key = getItemKey(node, "key");

                if (key.type == AT_INVALID) break;



                KeyMapNode keyMapNode;

                keyMapNode.type = type;

                keyMapNode.data.script.keyNode.type = key.type;

                keyMapNode.data.script.keyNode.key = key.key;

                keyMapNode.data.script.keyNode.modifiers = key.modifiers;

                keyMapNode.data.script.keyNode.pos = getItemPos(node, "pos");

                keyMapNode.script = getItemString(node, "script");

                m_keyMapNodes.push_back(keyMapNode);

            } break;



            case KeyMap::KMT_CAMERA_MOVE: {

                if (!checkForCamera(node)) {

                    LOGW() << "json error: format error (camera)";

                    break;

                }



                // 1. 设置开关按键 (这是核心，将 Camera 组件的按键设为 战斗模式开关)

                ParsedKey key = getItemKey(node, "key");

                if (key.type != AT_INVALID) {

                    m_switchKey.type = key.type;

                    m_switchKey.key = key.key;

                }



                // 2. 转换为内部的 MOUSE_MOVE 节点，用于控制视角

                KeyMapNode keyMapNode;

                keyMapNode.type = KMT_MOUSE_MOVE;



                keyMapNode.data.mouseMove.startPos = getItemPos(node, "pos");

                keyMapNode.data.mouseMove.speedRatio.x = getItemDouble(node, "speedRatioX");

                keyMapNode.data.mouseMove.speedRatio.y = getItemDouble(node, "speedRatioY");



                // 记录索引，让 isValidMouseMoveMap() 返回 true

                m_idxMouseMove = static_cast<int>(m_keyMapNodes.size());

                m_keyMapNodes.push_back(keyMapNode);

            } break;



            case KeyMap::KMT_FREE_LOOK: {

                if (!checkForFreeLook(node)) {

                    LOGW() << "json error: format error (freeLook)";

                    break;

                }

                ParsedKey key = getItemKey(node, "key");

                if (key.type == AT_INVALID) break;



                KeyMapNode keyMapNode;

                keyMapNode.type = type;

                keyMapNode.data.freeLook.keyNode.type = key.type;

                keyMapNode.data.freeLook.keyNode.key = key.key;

                keyMapNode.data.freeLook.keyNode.modifiers = key.modifiers;

                keyMapNode.data.freeLook.startPos = getItemPos(node, "startPos");

                keyMapNode.data.freeLook.speedRatio.x = getItemDouble(node, "speedRatioX");

                keyMapNode.data.freeLook.speedRatio.y = getItemDouble(node, "speedRatioY");

                keyMapNode.data.freeLook.resetViewOnRelease = getItemBool(node, "resetViewOnRelease");

                m_keyMapNodes.push_back(keyMapNode);

            } break;



            default:

                break;

            }

        }

    }



    makeReverseMap();



parseError:

    if (!errorString.empty()) {

        LOGW() << errorString;

    }

    return;

}



const KeyMap::KeyMapNode &KeyMap::getKeyMapNode(int key)

{

    auto it = m_rmapKey.find(key);

    if (it == m_rmapKey.end()) {

        auto it2 = m_rmapMouse.find(key);

        return (it2 != m_rmapMouse.end()) ? *it2->second : m_invalidNode;

    }

    return *it->second;

}



const KeyMap::KeyMapNode &KeyMap::getKeyMapNodeKey(int key, uint32_t modifiers)

{

    // 首先尝试精确匹配（带修饰键）

    int64_t hash = makeKeyHash(key, modifiers);

    auto it = m_rmapKey.find(hash);

    if (it != m_rmapKey.end()) {

        return *it->second;

    }



    // 如果没有精确匹配，尝试仅匹配按键（无修饰键）

    // 这样可以兼容旧的配置

    hash = makeKeyHash(key, GameMod::NoModifier);

    it = m_rmapKey.find(hash);

    if (it != m_rmapKey.end()) {

        return *it->second;

    }



    return m_invalidNode;

}



const KeyMap::KeyMapNode &KeyMap::getKeyMapNodeMouse(int key)

{

    auto it = m_rmapMouse.find(key);

    return (it != m_rmapMouse.end()) ? *it->second : m_invalidNode;

}

// 从显示名称转换为键码和修饰键
// 返回 std::pair<key, modifiers>
static std::pair<int, uint32_t> displayNameToKeyWithModifiers(const std::string& displayName)
{
    std::string name = trim(displayName);
    uint32_t modifiers = GameMod::NoModifier;

    // 处理组合键 (如 "Ctrl+J", "Shift+G", "Ctrl+Shift+A")
    std::vector<std::string> parts = split(name, '+');
    std::string keyPart = name;

    if (parts.size() > 1) {
        keyPart = trim(parts.back());  // 最后一部分是实际按键
        for (size_t i = 0; i < parts.size() - 1; ++i) {
            std::string mod = toLower(trim(parts[i]));
            if (mod == "ctrl" || mod == "control") {
                modifiers |= GameMod::ControlModifier;
            } else if (mod == "shift") {
                modifiers |= GameMod::ShiftModifier;
            } else if (mod == "alt") {
                modifiers |= GameMod::AltModifier;
            } else if (mod == "meta" || mod == "win") {
                modifiers |= GameMod::MetaModifier;
            }
        }
    }

    // 鼠标按键
    if (keyPart == "LMB" || iequals(keyPart, "LeftButton")) return {GameMouse::LeftButton, modifiers};
    if (keyPart == "RMB" || iequals(keyPart, "RightButton")) return {GameMouse::RightButton, modifiers};
    if (keyPart == "MMB" || iequals(keyPart, "MiddleButton")) return {GameMouse::MiddleButton, modifiers};
    if (keyPart == "MB4" || iequals(keyPart, "SideButton1") || iequals(keyPart, "XButton1")) return {GameMouse::XButton1, modifiers};
    if (keyPart == "MB5" || iequals(keyPart, "SideButton2") || iequals(keyPart, "XButton2")) return {GameMouse::XButton2, modifiers};

    // 滚轮
    if (iequals(keyPart, "WheelUp") || keyPart == "\xe6\xbb\x9a\xe4\xb8\x8a") return {WHEEL_UP, modifiers};
    if (iequals(keyPart, "WheelDown") || keyPart == "\xe6\xbb\x9a\xe4\xb8\x8b") return {WHEEL_DOWN, modifiers};

    // 符号键
    if (keyPart == "=" || keyPart == "Equal") return {GameKey::Key_Equal, modifiers};
    if (keyPart == "+" || keyPart == "Plus") return {GameKey::Key_Plus, modifiers};
    if (keyPart == "-" || keyPart == "Minus") return {GameKey::Key_Minus, modifiers};
    if (keyPart == "*" || keyPart == "Asterisk") return {GameKey::Key_Asterisk, modifiers};
    if (keyPart == "/" || keyPart == "Slash") return {GameKey::Key_Slash, modifiers};
    if (keyPart == "`" || keyPart == "QuoteLeft") return {GameKey::Key_QuoteLeft, modifiers};
    if (keyPart == "~" || keyPart == "AsciiTilde") return {GameKey::Key_AsciiTilde, modifiers};
    if (keyPart == "\\" || keyPart == "Backslash") return {GameKey::Key_Backslash, modifiers};
    if (keyPart == "[" || keyPart == "BracketLeft") return {GameKey::Key_BracketLeft, modifiers};
    if (keyPart == "]" || keyPart == "BracketRight") return {GameKey::Key_BracketRight, modifiers};
    if (keyPart == ";" || keyPart == "Semicolon") return {GameKey::Key_Semicolon, modifiers};
    if (keyPart == "'" || keyPart == "Apostrophe") return {GameKey::Key_Apostrophe, modifiers};
    if (keyPart == "," || keyPart == "Comma") return {GameKey::Key_Comma, modifiers};
    if (keyPart == "." || keyPart == "Period") return {GameKey::Key_Period, modifiers};

    // 特殊键
    if (iequals(keyPart, "Space")) return {GameKey::Key_Space, modifiers};
    if (iequals(keyPart, "Tab")) return {GameKey::Key_Tab, modifiers};
    if (iequals(keyPart, "Enter") || iequals(keyPart, "Return")) return {GameKey::Key_Return, modifiers};
    if (iequals(keyPart, "Esc") || iequals(keyPart, "Escape")) return {GameKey::Key_Escape, modifiers};
    if (iequals(keyPart, "Shift")) return {GameKey::Key_Shift, modifiers};
    if (iequals(keyPart, "Ctrl") || iequals(keyPart, "Control")) return {GameKey::Key_Control, modifiers};
    if (iequals(keyPart, "Alt")) return {GameKey::Key_Alt, modifiers};
    if (iequals(keyPart, "Backspace")) return {GameKey::Key_Backspace, modifiers};
    if (iequals(keyPart, "Up") || keyPart == "\xe2\x86\x91") return {GameKey::Key_Up, modifiers};
    if (iequals(keyPart, "Down") || keyPart == "\xe2\x86\x93") return {GameKey::Key_Down, modifiers};
    if (iequals(keyPart, "Left") || keyPart == "\xe2\x86\x90") return {GameKey::Key_Left, modifiers};
    if (iequals(keyPart, "Right") || keyPart == "\xe2\x86\x92") return {GameKey::Key_Right, modifiers};

    // F1-F12
    if (keyPart.size() >= 2 && keyPart.size() <= 3 && std::toupper(static_cast<unsigned char>(keyPart[0])) == 'F') {
        try {
            int num = std::stoi(keyPart.substr(1));
            if (num >= 1 && num <= 12) {
                return {GameKey::Key_F1 + num - 1, modifiers};
            }
        } catch (...) {}
    }

    // 单字符（字母、数字）
    if (keyPart.size() == 1) {
        char c = static_cast<char>(std::toupper(static_cast<unsigned char>(keyPart[0])));
        if (c >= 'A' && c <= 'Z') return {GameKey::Key_A + (c - 'A'), modifiers};
        if (c >= '0' && c <= '9') return {GameKey::Key_0 + (c - '0'), modifiers};
    }

    return {0, GameMod::NoModifier};  // 未找到
}

const KeyMap::KeyMapNode &KeyMap::getKeyMapNodeByDisplayName(const std::string& displayName)
{
    auto [key, modifiers] = displayNameToKeyWithModifiers(displayName);
    if (key == 0) return m_invalidNode;

    // 判断是键盘键还是鼠标按钮/滚轮
    if (key == GameMouse::LeftButton || key == GameMouse::RightButton || key == GameMouse::MiddleButton ||
        key == GameMouse::XButton1 || key == GameMouse::XButton2 ||
        key == WHEEL_UP || key == WHEEL_DOWN) {
        return getKeyMapNodeMouse(key);
    } else {
        return getKeyMapNodeKey(key, modifiers);
    }
}

bool KeyMap::isSwitchOnKeyboard()

{

    return m_switchKey.type == AT_KEY;

}



int KeyMap::getSwitchKey()

{

    return m_switchKey.key;

}



bool KeyMap::isValidMouseMoveMap()

{

    return m_idxMouseMove != -1;

}



bool KeyMap::isValidSteerWheelMap()

{

    return m_idxSteerWheel != -1;

}



const KeyMap::KeyMapNode &KeyMap::getMouseMoveMap()

{

    if (m_idxMouseMove >= 0 && m_idxMouseMove < static_cast<int>(m_keyMapNodes.size())) {

        return m_keyMapNodes[m_idxMouseMove];

    }

    return m_invalidNode;

}



void KeyMap::setSteerWheelCoefficient(double up, double down, double left, double right)
{
    // 设置临时系数，默认 1.0（不变）
    m_steerWheelCoeff[0] = up;
    m_steerWheelCoeff[1] = down;
    m_steerWheelCoeff[2] = left;
    m_steerWheelCoeff[3] = right;

    // 标记系数已变化，需要实时更新
    m_coefficientChanged = true;
}

void KeyMap::resetSteerWheelCoefficient()
{
    m_steerWheelCoeff[0] = 1.0;
    m_steerWheelCoeff[1] = 1.0;
    m_steerWheelCoeff[2] = 1.0;
    m_steerWheelCoeff[3] = 1.0;
    m_coefficientChanged = true;
}

double KeyMap::getSteerWheelCoefficient(int direction) const
{
    if (direction < 0 || direction > 3) return 1.0;
    return m_steerWheelCoeff[direction];
}

const KeyMap::KeyMapNode* KeyMap::getSteerWheelNode() const
{
    if (m_idxSteerWheel >= 0 && m_idxSteerWheel < static_cast<int>(m_keyMapNodes.size())) {
        return &m_keyMapNodes[m_idxSteerWheel];
    }
    return nullptr;
}

double KeyMap::getSteerWheelOffset(int direction) const
{
    if (m_idxSteerWheel < 0 || m_idxSteerWheel >= static_cast<int>(m_keyMapNodes.size())) {
        return 0.0;
    }

    const KeyMapNode &node = m_keyMapNodes[m_idxSteerWheel];
    double baseOffset = 0.0;

    switch (direction) {
        case 0: baseOffset = node.data.steerWheel.up.extendOffset; break;
        case 1: baseOffset = node.data.steerWheel.down.extendOffset; break;
        case 2: baseOffset = node.data.steerWheel.left.extendOffset; break;
        case 3: baseOffset = node.data.steerWheel.right.extendOffset; break;
        default: return 0.0;
    }

    // 应用系数
    return baseOffset * m_steerWheelCoeff[direction];
}



void KeyMap::makeReverseMap()

{

    m_rmapKey.clear();

    m_rmapMouse.clear();

    for (int i = 0; i < static_cast<int>(m_keyMapNodes.size()); ++i) {

        auto &node = m_keyMapNodes[i];

        switch (node.type) {



        case KMT_STEER_WHEEL: {

            // 方向盘的四个方向键

            if (node.data.steerWheel.left.type == AT_KEY) {

                m_rmapKey.insert({makeKeyHash(node.data.steerWheel.left.key, node.data.steerWheel.left.modifiers), &node});

            } else {

                m_rmapMouse.insert({node.data.steerWheel.left.key, &node});

            }

            if (node.data.steerWheel.right.type == AT_KEY) {

                m_rmapKey.insert({makeKeyHash(node.data.steerWheel.right.key, node.data.steerWheel.right.modifiers), &node});

            } else {

                m_rmapMouse.insert({node.data.steerWheel.right.key, &node});

            }

            if (node.data.steerWheel.up.type == AT_KEY) {

                m_rmapKey.insert({makeKeyHash(node.data.steerWheel.up.key, node.data.steerWheel.up.modifiers), &node});

            } else {

                m_rmapMouse.insert({node.data.steerWheel.up.key, &node});

            }

            if (node.data.steerWheel.down.type == AT_KEY) {

                m_rmapKey.insert({makeKeyHash(node.data.steerWheel.down.key, node.data.steerWheel.down.modifiers), &node});

            } else {

                m_rmapMouse.insert({node.data.steerWheel.down.key, &node});

            }

        } break;



        case KMT_ANDROID_KEY: {

            if (node.data.androidKey.keyNode.type == AT_KEY) {

                m_rmapKey.insert({makeKeyHash(node.data.androidKey.keyNode.key, node.data.androidKey.keyNode.modifiers), &node});

            } else {

                m_rmapMouse.insert({node.data.androidKey.keyNode.key, &node});

            }

        } break;



        case KMT_SCRIPT: {

            if (node.data.script.keyNode.type == AT_KEY) {

                m_rmapKey.insert({makeKeyHash(node.data.script.keyNode.key, node.data.script.keyNode.modifiers), &node});

            } else {

                m_rmapMouse.insert({node.data.script.keyNode.key, &node});

            }

        } break;



        case KMT_FREE_LOOK: {

            if (node.data.freeLook.keyNode.type == AT_KEY) {

                m_rmapKey.insert({makeKeyHash(node.data.freeLook.keyNode.key, node.data.freeLook.keyNode.modifiers), &node});

            } else {

                m_rmapMouse.insert({node.data.freeLook.keyNode.key, &node});

            }

        } break;



        default:

            break;

        }

    }

}



std::string KeyMap::getItemString(const nlohmann::json &node, const std::string &name)

{

    if (node.contains(name) && node[name].is_string())
        return node[name].get<std::string>();
    return std::string();

}



double KeyMap::getItemDouble(const nlohmann::json &node, const std::string &name)

{

    return node.value(name, 0.0);

}



bool KeyMap::getItemBool(const nlohmann::json &node, const std::string &name)

{

    return node.value(name, false);

}



nlohmann::json KeyMap::getItemObject(const nlohmann::json &node, const std::string &name)

{

    if (node.contains(name) && node[name].is_object())
        return node[name];
    return nlohmann::json::object();

}



PointF KeyMap::getItemPos(const nlohmann::json &node, const std::string &name)

{

    auto pos = getItemObject(node, name);

    return PointF(pos.value("x", 0.0), pos.value("y", 0.0));

}



KeyMap::ParsedKey KeyMap::getItemKey(const nlohmann::json &node, const std::string &name)

{

    std::string value = getItemString(node, name);

    ParsedKey result;

    result.type = AT_INVALID;

    result.key = GameKey::Key_unknown;

    result.modifiers = GameMod::NoModifier;



    if (value.empty()) {

        return result;

    }



    // 先检查是否是鼠标按钮（含简化名称）
    if (iequals(value, "SideButton1") || value == "MB4" || iequals(value, "XButton1")) {
        result.type = AT_MOUSE;
        result.key = GameMouse::XButton1;
        return result;
    }
    if (iequals(value, "SideButton2") || value == "MB5" || iequals(value, "XButton2")) {
        result.type = AT_MOUSE;
        result.key = GameMouse::XButton2;
        return result;
    }

    int btn = mouseButtonFromName(value.c_str());

    if (btn != -1) {

        result.type = AT_MOUSE;

        result.key = btn;

        return result;

    }

    // 检查是否是滚轮
    if (iequals(value, "WheelUp")) {
        result.type = AT_MOUSE;
        result.key = WHEEL_UP;
        return result;
    }
    if (iequals(value, "WheelDown")) {
        result.type = AT_MOUSE;
        result.key = WHEEL_DOWN;
        return result;
    }

    // 解析组合键（如 "Shift+G", "Ctrl+Alt+X"）

    uint32_t modifiers = GameMod::NoModifier;

    std::string keyPart = value;



    // 检查是否包含修饰键

    if (value.find('+') != std::string::npos) {

        std::vector<std::string> parts = split(value, '+');

        keyPart = parts.back();  // 最后一部分是主键



        for (size_t i = 0; i < parts.size() - 1; ++i) {

            std::string mod = trim(parts[i]);

            if (mod == "Shift") {

                modifiers |= GameMod::ShiftModifier;

            } else if (mod == "Ctrl" || mod == "Control") {

                modifiers |= GameMod::ControlModifier;

            } else if (mod == "Alt") {

                modifiers |= GameMod::AltModifier;

            } else if (mod == "Meta" || mod == "Win") {

                modifiers |= GameMod::MetaModifier;

            }

        }

    }



    // 尝试解析键值

    int key = -1;



    // 首先尝试带 "Key_" 前缀

    std::string keyWithPrefix = keyPart;

    if (keyPart.rfind("Key_", 0) != 0) {

        keyWithPrefix = "Key_" + keyPart;

    }

    key = qtKeyFromName(keyWithPrefix.c_str());



    // 如果失败，尝试不带前缀的原始值

    if (key == -1) {

        key = qtKeyFromName(keyPart.c_str());

    }



    // 如果还是失败，尝试解析单字符

    if (key == -1 && keyPart.size() == 1) {

        char c = static_cast<char>(std::toupper(static_cast<unsigned char>(keyPart[0])));

        if (c >= 'A' && c <= 'Z') {

            key = GameKey::Key_A + (c - 'A');

        } else if (c >= '0' && c <= '9') {

            key = GameKey::Key_0 + (c - '0');

        }

    }



    if (key != -1) {

        result.type = AT_KEY;

        result.key = key;

        result.modifiers = modifiers;

    }



    return result;

}



KeyMap::KeyMapType KeyMap::getItemKeyMapType(const nlohmann::json &node, const std::string &name)

{

    std::string value = getItemString(node, name);

    return keyMapTypeFromString(value.c_str());

}



bool KeyMap::checkItemString(const nlohmann::json &node, const std::string &name)

{

    return node.contains(name) && node[name].is_string();

}



bool KeyMap::checkItemDouble(const nlohmann::json &node, const std::string &name)

{

    return node.contains(name) && node[name].is_number();

}



bool KeyMap::checkItemBool(const nlohmann::json &node, const std::string &name)

{

    return node.contains(name) && node[name].is_boolean();

}



bool KeyMap::checkItemObject(const nlohmann::json &node, const std::string &name)

{

    return node.contains(name) && node[name].is_object();

}



bool KeyMap::checkItemPos(const nlohmann::json &node, const std::string &name)

{

    if (node.contains(name) && node[name].is_object()) {

        const auto& pos = node[name];

        return pos.contains("x") && pos["x"].is_number() && pos.contains("y") && pos["y"].is_number();

    }

    return false;

}



bool KeyMap::checkForAndroidKey(const nlohmann::json &node)

{

    return checkItemString(node, "key") && checkItemDouble(node, "androidKey");

}



bool KeyMap::checkForSteerWheel(const nlohmann::json &node)

{

    return checkItemString(node, "leftKey") && checkItemString(node, "rightKey") && checkItemString(node, "upKey") && checkItemString(node, "downKey")

    && checkItemDouble(node, "leftOffset") && checkItemDouble(node, "rightOffset") && checkItemDouble(node, "upOffset")

        && checkItemDouble(node, "downOffset") && checkItemPos(node, "centerPos");

}



bool KeyMap::checkForScript(const nlohmann::json &node)

{

    return checkItemString(node, "key") && checkItemPos(node, "pos") && checkItemString(node, "script");

}



bool KeyMap::checkForCamera(const nlohmann::json &node)

{

    return checkItemString(node, "key") && checkItemPos(node, "pos")

    && checkItemDouble(node, "speedRatioX") && checkItemDouble(node, "speedRatioY");

}

bool KeyMap::checkForFreeLook(const nlohmann::json &node)

{

    return checkItemString(node, "key") && checkItemPos(node, "startPos")

    && checkItemDouble(node, "speedRatioX") && checkItemDouble(node, "speedRatioY");

}
