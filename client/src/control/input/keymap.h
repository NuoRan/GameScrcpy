#ifndef KEYMAP_H

#define KEYMAP_H

#include <nlohmann/json.hpp>



#include <string>
#include <unordered_map>

#include <utility>

#include <cstdint>

#include "GameTypes.h"
#include "GameKeys.h"

#include <vector>



#include "keycodes.h"

// 自定义滚轮事件值 / Custom wheel event values (no conflict with Qt key values)
constexpr int WHEEL_UP = 0x10000001;
constexpr int WHEEL_DOWN = 0x10000002;

/**
 * @brief 按键映射管理器 / Key Mapping Manager
 *
 * 解析脚本中的按键绑定配置，将键盘/鼠标事件映射为 Android 触摸/按键操作。
 * Parses key binding configs from scripts, maps keyboard/mouse events to Android touch/key actions.
 */
class KeyMap

{



public:

    enum KeyMapType

    {

        KMT_INVALID = -1,

        // 移除了 KMT_CLICK, KMT_CLICK_TWICE, KMT_CLICK_MULTI, KMT_DRAG

        KMT_STEER_WHEEL = 4,

        KMT_MOUSE_MOVE,

        KMT_ANDROID_KEY,

        KMT_SCRIPT,

        KMT_CAMERA_MOVE,

        KMT_FREE_LOOK

    };

    // String-to-enum conversion for KeyMapType (replaces Q_ENUM + QMetaEnum)
    static KeyMapType keyMapTypeFromString(const char* name);



    enum ActionType

    {

        AT_INVALID = -1,

        AT_KEY = 0,

        AT_MOUSE = 1,

    };




    struct ParsedKey {

        ActionType type = AT_INVALID;

        int key = GameKey::Key_unknown;

        uint32_t modifiers = GameMod::NoModifier;

    };



    struct KeyNode

    {

        ActionType type = AT_INVALID;

        int key = GameKey::Key_unknown;

        uint32_t modifiers = GameMod::NoModifier;

        PointF pos = PointF(0, 0);

        PointF extendPos = PointF(0, 0);

        double extendOffset = 0.0;

        AndroidKeycode androidKey = AKEYCODE_UNKNOWN;



        KeyNode(

            ActionType type = AT_INVALID,

            int key = GameKey::Key_unknown,

            uint32_t modifiers = GameMod::NoModifier,

            PointF pos = PointF(0, 0),

            PointF extendPos = PointF(0, 0),

            double extendOffset = 0.0,

            AndroidKeycode androidKey = AKEYCODE_UNKNOWN)

            : type(type), key(key), modifiers(modifiers), pos(pos), extendPos(extendPos), extendOffset(extendOffset), androidKey(androidKey)

        {

        }

    };



    struct KeyMapNode

    {

        KeyMapType type = KMT_INVALID;

        std::string script;



        union DATA

        {

            struct

            {

                PointF centerPos = { 0.0, 0.0 };

                KeyNode left, right, up, down;

                double speedMultiplier = 1.0;  // 轮盘速度倍率

            } steerWheel;



            struct

            {

                PointF startPos   = { 0.0, 0.0 };

                PointF speedRatio = { 1.0, 1.0 };

                bool areaMode = false;  // true=区域模式, false=全屏模式
                double areaX = 0.3, areaY = 0.3, areaW = 0.4, areaH = 0.4;  // 归一化区域矩形

            } mouseMove;



            struct

            {

                KeyNode keyNode;

            } androidKey;



            struct

            {

                KeyNode keyNode;

            } script;

            struct

            {

                KeyNode keyNode;        // 触发热键

                PointF startPos = { 0.0, 0.0 };  // 起始位置

                PointF speedRatio = { 1.0, 1.0 };  // 灵敏度

                bool resetViewOnRelease = false;  // 松开时是否重置视角

            } freeLook;

            DATA() { memset(this, 0, sizeof(DATA)); }

            ~DATA() {}

        } data;



        KeyMapNode() {}

        ~KeyMapNode() {}

    };



    KeyMap();

    ~KeyMap();



    void loadKeyMap(const std::string &json);

    const KeyMap::KeyMapNode &getKeyMapNode(int key);

    const KeyMap::KeyMapNode &getKeyMapNodeKey(int key, uint32_t modifiers = GameMod::NoModifier);

    const KeyMap::KeyMapNode &getKeyMapNodeMouse(int key);

    // 根据显示名称查找按键（支持 "LMB", "Tab", "=" 等）
    const KeyMap::KeyMapNode &getKeyMapNodeByDisplayName(const std::string& displayName);

    // 获取所有键位节点（供自动启动脚本检测使用）
    const std::vector<KeyMapNode>& getKeyMapNodes() const { return m_keyMapNodes; }

    bool isSwitchOnKeyboard();

    int getSwitchKey();



    bool isValidMouseMoveMap();

    bool isValidSteerWheelMap();

    const KeyMap::KeyMapNode &getMouseMoveMap();



    // 设置轮盘偏移系数（临时生效）
    // 默认 1,1,1,1，实际偏移 = 原值 * 系数
    void setSteerWheelCoefficient(double up, double down, double left, double right);

    // 重置轮盘偏移系数为默认值 1,1,1,1
    void resetSteerWheelCoefficient();

    // 获取应用系数后的轮盘偏移
    double getSteerWheelOffset(int direction) const; // 0=up, 1=down, 2=left, 3=right

    // 获取单独的轮盘系数（不乘以基础偏移）
    double getSteerWheelCoefficient(int direction) const; // 0=up, 1=down, 2=left, 3=right

    // 获取轮盘节点（用于即时更新）
    const KeyMapNode* getSteerWheelNode() const;

    // 检查系数是否变化（并重置标志）
    bool checkCoefficientChanged() {
        bool changed = m_coefficientChanged;
        m_coefficientChanged = false;
        return changed;
    }



private:

    void makeReverseMap();



    bool checkItemString(const nlohmann::json &node, const std::string &name);

    bool checkItemDouble(const nlohmann::json &node, const std::string &name);

    bool checkItemBool(const nlohmann::json &node, const std::string &name);

    bool checkItemObject(const nlohmann::json &node, const std::string &name);

    bool checkItemPos(const nlohmann::json &node, const std::string &name);



    bool checkForSteerWheel(const nlohmann::json &node);

    bool checkForAndroidKey(const nlohmann::json &node);

    bool checkForScript(const nlohmann::json &node);

    bool checkForCamera(const nlohmann::json &node);

    bool checkForFreeLook(const nlohmann::json &node); // 小眼睛



    std::string getItemString(const nlohmann::json &node, const std::string &name);

    double getItemDouble(const nlohmann::json &node, const std::string &name);

    bool getItemBool(const nlohmann::json &node, const std::string &name);

    nlohmann::json getItemObject(const nlohmann::json &node, const std::string &name);

    PointF getItemPos(const nlohmann::json &node, const std::string &name);

    ParsedKey getItemKey(const nlohmann::json &node, const std::string &name);

    KeyMapType getItemKeyMapType(const nlohmann::json &node, const std::string &name);



private:

    static std::string s_keyMapPath;



    std::vector<KeyMapNode> m_keyMapNodes;

    KeyNode m_switchKey = { AT_KEY, GameKey::Key_QuoteLeft };



    KeyMapNode m_invalidNode;



    int m_idxSteerWheel = -1;

    // 轮盘偏移系数（临时生效）
    double m_steerWheelCoeff[4] = {1.0, 1.0, 1.0, 1.0}; // up, down, left, right
    bool m_coefficientChanged = false;

    int m_idxMouseMove = -1;



    // 使用 int64_t 作为键，低32位存储 key，高32位存储 modifiers

    std::unordered_multimap<int64_t, KeyMapNode *> m_rmapKey;

    std::unordered_multimap<int, KeyMapNode *> m_rmapMouse;



    // 辅助函数：组合 key 和 modifiers 为查找键

    static int64_t makeKeyHash(int key, uint32_t modifiers) {

        return (static_cast<int64_t>(modifiers) << 32) | static_cast<int64_t>(key);

    }

};



#endif // KEYMAP_H
