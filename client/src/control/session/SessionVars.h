#ifndef SESSION_VARS_H
#define SESSION_VARS_H

#include <QObject>
#include <QHash>
#include <QMutex>
#include <QVariant>
#include <QString>
#include <QList>

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
class SessionVars : public QObject
{
    Q_OBJECT
public:
    explicit SessionVars(QObject* parent = nullptr);
    ~SessionVars();

    // ========== 通用会话变量 ==========

    /**
     * @brief 获取变量
     * @param key 变量名
     * @param defaultValue 默认值
     * @return 变量值
     */
    QVariant getVar(const QString& key, const QVariant& defaultValue = QVariant()) const;

    /**
     * @brief 设置变量
     * @param key 变量名
     * @param value 变量值
     */
    void setVar(const QString& key, const QVariant& value);

    /**
     * @brief 检查变量是否存在
     */
    bool hasVar(const QString& key) const;

    /**
     * @brief 移除变量
     */
    void removeVar(const QString& key);

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
    void addTouchSeq(int keyId, quint32 seqId);

    /**
     * @brief 获取并移除触摸序列 ID 列表
     * @param keyId 按键 ID
     * @return 序列 ID 列表
     */
    QList<quint32> takeTouchSeqs(int keyId);

    /**
     * @brief 获取触摸序列数量
     */
    int touchSeqCount(int keyId) const;

    /**
     * @brief 检查是否有触摸序列
     */
    bool hasTouchSeqs(int keyId) const;

    /**
     * @brief 清空所有触摸序列
     */
    void clearTouchSeqs();

    // ========== 轮盘参数标识 ==========

    /**
     * @brief 设置轮盘参数标识
     */
    void setRadialParamKeyId(const QString& keyId);

    /**
     * @brief 获取轮盘参数标识
     */
    QString radialParamKeyId() const;

private:
    // 通用变量
    QHash<QString, QVariant> m_vars;
    mutable QMutex m_varsMutex;

    // 触摸序列
    QHash<int, QList<quint32>> m_touchSeqIds;
    mutable QMutex m_touchSeqMutex;

    // 轮盘参数
    QString m_radialParamKeyId;
    mutable QMutex m_radialParamMutex;
};

#endif // SESSION_VARS_H
