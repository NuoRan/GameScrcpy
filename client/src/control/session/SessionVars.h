#ifndef SESSION_VARS_H
#define SESSION_VARS_H

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include "GameTypes.h"

/**
 * @brief 会话变量存储器 / Session Variables Store
 *
 * 负责存储设备会话的所有变量数据 / Stores all variable data for a device session:
 * - m_vars：通用会话变量 / General session variables
 * - m_touchSeqIds：触摸序列 ID / Touch sequence IDs
 * - m_radialParamKeyId：轮盘参数标识 / Wheel parameter key ID
 *
 * 所有操作都是线程安全的。/ All operations are thread-safe.
 */
class SessionVars
{
public:
    SessionVars();
    ~SessionVars();

    // ========== 通用会话变量 ==========

    /**
     * @brief 获取变量
     * @param key 变量名
     * @param defaultValue 默认值
     * @return 变量值
     */
    ScriptValue getVar(const std::string& key, const ScriptValue& defaultValue = ScriptValue()) const;

    /**
     * @brief 设置变量
     * @param key 变量名
     * @param value 变量值
     */
    void setVar(const std::string& key, const ScriptValue& value);

    /**
     * @brief 检查变量是否存在
     */
    bool hasVar(const std::string& key) const;

    /**
     * @brief 移除变量
     */
    void removeVar(const std::string& key);

    /**
     * @brief 清空所有变量
     */
    void clearVars();

    // ========== 触摸序列 ID 管理 ==========

    /**
     * @brief 添加触摸序列 ID
     * @param keyId 按键 ID
     * @param seqId 序列 ID
     */
    void addTouchSeq(int keyId, uint32_t seqId);

    /**
     * @brief 获取并移除触摸序列 ID 列表
     * @param keyId 按键 ID
     * @return 序列 ID 列表
     */
    std::vector<uint32_t> takeTouchSeqs(int keyId);

    /**
     * @brief 获取触摸序列数量
     */
    int touchSeqCount(int keyId) const;

    /**
     * @brief 检查是否有触摸序列
     */
    bool hasTouchSeqs(int keyId) const;

    /**
     * @brief 获取并移除所有触摸序列（原子操作）
     * @return keyId → seqId列表 的映射
     */
    std::unordered_map<int, std::vector<uint32_t>> takeAllTouchSeqs();

    /**
     * @brief 清空所有触摸序列
     */
    void clearTouchSeqs();

    // ========== 轮盘参数标识 ==========

    /**
     * @brief 设置轮盘参数标识
     */
    void setRadialParamKeyId(const std::string& keyId);

    /**
     * @brief 获取轮盘参数标识
     */
    std::string radialParamKeyId() const;

private:
    // 通用变量
    std::unordered_map<std::string, ScriptValue> m_vars;
    mutable std::mutex m_varsMutex;

    // 触摸序列
    std::unordered_map<int, std::vector<uint32_t>> m_touchSeqIds;
    mutable std::mutex m_touchSeqMutex;

    // 轮盘参数
    std::string m_radialParamKeyId;
    mutable std::mutex m_radialParamMutex;
};

#endif // SESSION_VARS_H
