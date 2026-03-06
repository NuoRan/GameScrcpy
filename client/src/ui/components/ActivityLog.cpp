/**
 * @file ActivityLog.cpp
 */
#include "ActivityLog.h"
#include "ThemeManager.h"

#include <chrono>
#include <ctime>
#include <QScrollBar>
#include <QFrame>

static inline std::string currentTimeString() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf;
    localtime_s(&tm_buf, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm_buf);
    return std::string(buf);
}

namespace Fluent {

ActivityLog::ActivityLog(QWidget* parent) : QWidget(parent)
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    m_scrollArea = new QScrollArea;
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* container = new QWidget;
    m_entriesLayout = new QVBoxLayout(container);
    m_entriesLayout->setContentsMargins(0, 0, 0, 0);
    m_entriesLayout->setSpacing(2);
    m_entriesLayout->addStretch();

    m_scrollArea->setWidget(container);
    outerLayout->addWidget(m_scrollArea);
}

void ActivityLog::addEntry(const QString& message, const QString& level)
{
    Entry e;
    e.timeStr = currentTimeString();
    e.message = message;
    e.level = level;
    m_entries.append(e);

    while (m_entries.size() > m_maxEntries) m_entries.removeFirst();

    auto& tm = ThemeManager::instance();
    QString timeStr = QString::fromStdString(e.timeStr);

    QColor levelColor;
    if (level == "error") levelColor = QColor("#ef4444");
    else if (level == "warning") levelColor = QColor("#f59e0b");
    else if (level == "success") levelColor = QColor("#22c55e");
    else levelColor = QColor(tm.textTertiary());

    auto* label = new QLabel(QStringLiteral(
        "<span style='color:%1;font-size:10px;'>%2</span> "
        "<span style='color:%3;font-size:12px;'>%4</span>")
        .arg(levelColor.name(), timeStr, tm.textSecondary(), message.toHtmlEscaped()));
    label->setWordWrap(true);
    label->setStyleSheet("background: transparent; padding: 2px 4px;");

    // Insert before the stretch
    int idx = m_entriesLayout->count() - 1;
    m_entriesLayout->insertWidget(idx, label);

    // Prune UI entries
    while (m_entriesLayout->count() > m_maxEntries + 1) {
        auto* item = m_entriesLayout->takeAt(0);
        if (item->widget()) { item->widget()->deleteLater(); }
        delete item;
    }

    // Auto-scroll to bottom
    QMetaObject::invokeMethod(this, [this]() {
        m_scrollArea->verticalScrollBar()->setValue(m_scrollArea->verticalScrollBar()->maximum());
    }, Qt::QueuedConnection);
}

void ActivityLog::clear()
{
    m_entries.clear();
    while (m_entriesLayout->count() > 1) {
        auto* item = m_entriesLayout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
}

} // namespace Fluent
