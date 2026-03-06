/**
 * @file ActivityLog.h
 * @brief 活动日志组件 — 显示最近操作记录
 */
#ifndef ACTIVITYLOG_H
#define ACTIVITYLOG_H

#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <string>
#include <QList>

namespace Fluent {

class ActivityLog : public QWidget
{
    Q_OBJECT
public:
    explicit ActivityLog(QWidget* parent = nullptr);

    void addEntry(const QString& message, const QString& level = "info");
    void clear();
    int maxEntries() const { return m_maxEntries; }
    void setMaxEntries(int max) { m_maxEntries = max; }

private:
    struct Entry {
        std::string timeStr;
        QString message;
        QString level;
    };

    void rebuildUI();

    QScrollArea* m_scrollArea = nullptr;
    QVBoxLayout* m_entriesLayout = nullptr;
    QList<Entry> m_entries;
    int m_maxEntries = 50;
};

} // namespace Fluent
#endif
