#pragma once

#include <gs/script/Report.h>

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QJSEngine;
class QQmlEngine;

namespace gs::app {

class ExtensionHost;

struct ConverterEntry {
    QString extensionId;
    QString extensionName;
    QString name;
    QString title;
    QString from;
    QString to;
    QStringList extensions;
    std::filesystem::path folder;
    std::string script;
    std::vector<std::string> permissions;

    [[nodiscard]] QString key() const { return extensionId + u'/' + name; }
    [[nodiscard]] bool imports() const;

    friend bool operator==(const ConverterEntry&, const ConverterEntry&) = default;
};

struct ConvertInput {
    QString name;
    QByteArray bytes;
};

struct ConvertOutcome {
    bool ok = false;
    QString name;
    QByteArray bytes;
    QString problem;
};

class Converters final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantList converters READ converters NOTIFY convertersChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)

public:
    using Done = std::function<void(const ConvertOutcome& outcome)>;

    static constexpr qsizetype kMostBytes = qsizetype{32} << 20;
    static constexpr std::chrono::milliseconds kTimeLimit{30'000};

    explicit Converters(QObject* parent);
    ~Converters() override;
    Converters(const Converters&) = delete;
    Converters& operator=(const Converters&) = delete;

    static Converters* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    [[nodiscard]] static Converters* instance() noexcept { return s_instance; }

    void setConverters(std::vector<ConverterEntry> converters);
    void setTimeLimit(std::chrono::milliseconds limit);
    void follow(ExtensionHost& extensions);
    [[nodiscard]] const std::vector<ConverterEntry>& all() const noexcept { return m_converters; }
    [[nodiscard]] const ConverterEntry* find(const QString& key) const;
    [[nodiscard]] QVariantList converters() const { return m_list; }
    [[nodiscard]] bool busy() const noexcept { return m_running != nullptr || !m_waiting.empty(); }

    void run(const QString& key, ConvertInput input, Done done);

    Q_INVOKABLE QStringList importFilters(const QString& format, const QString& own) const;
    Q_INVOKABLE QString filterConverter(const QString& format, int index) const;
    Q_INVOKABLE bool accepts(const QString& format, const QUrl& url) const;
    Q_INVOKABLE QString importerFor(const QString& format, const QUrl& url, const QString& chosen = {}) const;
    Q_INVOKABLE int importProfiles(const QList<QUrl>& files, const QString& chosen = {});
    Q_INVOKABLE int exportTo(const QString& key, const QString& source, const QString& target);

signals:
    void convertersChanged();
    void busyChanged();
    void imported(int ticket, const QVariantMap& result);
    void exported(int ticket, bool ok, const QString& message);

private:
    class Bridge;
    struct Running;

    struct Job {
        QString key;
        ConvertInput input;
        Done done;
    };

    struct Import {
        int left = 0;
        QStringList imported;
        QStringList ids;
        QVariantList failed;
    };

    [[nodiscard]] std::vector<const ConverterEntry*> importersOf(const QString& format) const;
    [[nodiscard]] std::optional<ConvertInput> sourceOf(const ConverterEntry& entry, const QString& source,
                                                       QString& problem) const;
    void startNext();
    void handle(std::uint64_t id, const script::Report& report);
    void finish(std::uint64_t id, ConvertOutcome outcome);
    void checkLimit();
    void settleImport(int ticket, const QString& file, const ConvertOutcome& outcome);

    static inline Converters* s_instance = nullptr;

    std::vector<ConverterEntry> m_converters;
    QVariantList m_list;
    std::deque<Job> m_waiting;
    std::unique_ptr<Running> m_running;
    std::map<int, Import> m_imports;
    QTimer m_clock;
    std::chrono::milliseconds m_limit = kTimeLimit;
    std::uint64_t m_lastRun = 0;
    int m_lastTicket = 0;
};

}
