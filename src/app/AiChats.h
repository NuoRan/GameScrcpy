#pragma once

#include "AiConversation.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

class QJSEngine;
class QQmlEngine;

namespace gs::app {

class HelpLibrary;

[[nodiscard]] QString aiConnectionLog(const QString& log, const QString& home, int most);

class AiChatTools final : public AiToolRunner
{
public:
    using Navigate = std::function<void(const QString& page, const QString& section)>;

    static constexpr qsizetype kMostHelpChars = 30'000;
    static constexpr int kMostBlocks = 40;
    static constexpr qsizetype kMostBlockChars = 300;
    static constexpr int kMostSettings = 40;
    static constexpr int kMostCommands = 60;
    static constexpr int kReadLogLines = 1000;
    static constexpr int kMostConnectionLines = 80;

    AiChatTools(AiToolRunner* base, const HelpLibrary* help, Navigate navigate);

    [[nodiscard]] QJsonArray tools() const override;
    void run(const QString& tool, const QJsonObject& arguments, McpToolHost::Reply reply) override;

private:
    [[nodiscard]] McpOutcome readHelp(const QJsonObject& arguments) const;
    [[nodiscard]] static McpOutcome readGuide(const QJsonObject& arguments);
    [[nodiscard]] McpOutcome openPage(const QJsonObject& arguments) const;
    [[nodiscard]] static McpOutcome readBlocks(const QJsonObject& arguments);
    [[nodiscard]] static McpOutcome readSettings(const QJsonObject& arguments);
    [[nodiscard]] static McpOutcome listCommands(const QJsonObject& arguments);
    void readConnectionLog(const QJsonObject& arguments, McpToolHost::Reply reply);

    AiToolRunner* m_base;
    const HelpLibrary* m_help;
    Navigate m_navigate;
};

class AiChats final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(gs::app::AiConversation* conversation READ conversation CONSTANT FINAL)
    Q_PROPERTY(QVariantList recent READ recent NOTIFY recentChanged FINAL)
    Q_PROPERTY(QString current READ current NOTIFY currentChanged FINAL)
    Q_PROPERTY(int menuShown READ menuShown CONSTANT FINAL)
    Q_PROPERTY(bool windowOpen READ windowOpen WRITE setWindowOpen NOTIFY windowOpenChanged FINAL)
    Q_PROPERTY(QVariantMap lastContext READ lastContext NOTIFY lastContextChanged FINAL)

public:
    static constexpr int kMenuShown = 20;
    static constexpr qsizetype kMostTitleChars = 40;
    static constexpr std::size_t kMostFileBytes = 16 * 1024 * 1024;
    static constexpr std::wstring_view kIndexName = L"chats.idx";

    AiChats(AiConversation::ProviderFactory provider, AiToolRunner* tools, const HelpLibrary* help,
            std::filesystem::path folder, QObject* parent = nullptr);
    ~AiChats() override;
    AiChats(const AiChats&) = delete;
    AiChats& operator=(const AiChats&) = delete;

    static AiChats* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    [[nodiscard]] AiConversation* conversation() noexcept { return m_conversation.get(); }
    [[nodiscard]] QVariantList recent() const;
    [[nodiscard]] QString current() const { return m_current; }
    [[nodiscard]] int menuShown() const noexcept { return kMenuShown; }
    [[nodiscard]] bool windowOpen() const noexcept { return m_windowOpen; }
    void setWindowOpen(bool open);
    [[nodiscard]] QVariantMap lastContext() const { return m_lastContext; }

    Q_INVOKABLE bool send(const QString& text, const QVariantMap& context);
    Q_INVOKABLE void newConversation();
    Q_INVOKABLE bool open(const QString& id);
    Q_INVOKABLE void remove(const QString& id);
    Q_INVOKABLE void clearAll();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void openWindow();
    Q_INVOKABLE void noteContext(const QVariantMap& context);

signals:
    void recentChanged();
    void currentChanged();
    void toggleRequested();
    void windowWanted();
    void windowOpenChanged();
    void lastContextChanged();
    void pageWanted(const QString& page, const QString& section);

private:
    struct Entry {
        QString id;
        QString title;
        qint64 updated = 0;
    };

    void load();
    void save();
    void writeIndex() const;
    void setCurrent(const QString& id);
    void forget(std::size_t index);
    [[nodiscard]] std::filesystem::path fileOf(const QString& id) const;

    static inline AiChats* s_instance = nullptr;

    std::filesystem::path m_folder;
    std::unique_ptr<AiChatTools> m_tools;
    std::unique_ptr<AiConversation> m_conversation;
    std::vector<Entry> m_entries;
    QString m_current;
    QVariantMap m_lastContext;
    bool m_windowOpen = false;
};

}
