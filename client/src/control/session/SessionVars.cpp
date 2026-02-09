#include "SessionVars.h"
#include <QMutexLocker>

SessionVars::SessionVars(QObject* parent)
    : QObject(parent)
{
}

SessionVars::~SessionVars()
{
}

// ========== 通用会话变量 ==========

QVariant SessionVars::getVar(const QString& key, const QVariant& defaultValue) const
{
    QMutexLocker locker(&m_varsMutex);
    return m_vars.value(key, defaultValue);
}

void SessionVars::setVar(const QString& key, const QVariant& value)
{
    QMutexLocker locker(&m_varsMutex);
    m_vars[key] = value;
}

bool SessionVars::hasVar(const QString& key) const
{
    QMutexLocker locker(&m_varsMutex);
    return m_vars.contains(key);
}

void SessionVars::removeVar(const QString& key)
{
    QMutexLocker locker(&m_varsMutex);
    m_vars.remove(key);
}

void SessionVars::clearVars()
{
    QMutexLocker locker(&m_varsMutex);
    m_vars.clear();
}

// ========== 触摸序列 ID 管理 ==========

void SessionVars::addTouchSeq(int keyId, quint32 seqId)
{
    QMutexLocker locker(&m_touchSeqMutex);
    m_touchSeqIds[keyId].append(seqId);
}

QList<quint32> SessionVars::takeTouchSeqs(int keyId)
{
    QMutexLocker locker(&m_touchSeqMutex);
    return m_touchSeqIds.take(keyId);
}

int SessionVars::touchSeqCount(int keyId) const
{
    QMutexLocker locker(&m_touchSeqMutex);
    return m_touchSeqIds.value(keyId).size();
}

bool SessionVars::hasTouchSeqs(int keyId) const
{
    QMutexLocker locker(&m_touchSeqMutex);
    return m_touchSeqIds.contains(keyId);
}

void SessionVars::clearTouchSeqs()
{
    QMutexLocker locker(&m_touchSeqMutex);
    m_touchSeqIds.clear();
}

// ========== 轮盘参数标识 ==========

void SessionVars::setRadialParamKeyId(const QString& keyId)
{
    QMutexLocker locker(&m_radialParamMutex);
    m_radialParamKeyId = keyId;
}

QString SessionVars::radialParamKeyId() const
{
    QMutexLocker locker(&m_radialParamMutex);
    return m_radialParamKeyId;
}
