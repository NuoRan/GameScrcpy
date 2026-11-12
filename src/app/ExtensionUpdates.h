#pragma once

#include <gs/ext/Version.h>

#include <QByteArray>
#include <QCryptographicHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QFile;
class QJSEngine;
class QNetworkAccessManager;
class QQmlEngine;

namespace gs::app {

class AppSettings;
class ExtensionHost;
class UpdateTransfer;

struct ExtensionUpdatePaths {
    std::filesystem::path state;
    std::filesystem::path staging;
    std::filesystem::path extensions;
};

struct UpdateOffer {
    std::string version;
    std::string url;
    std::string sha256;
    std::uint64_t size = 0;
    std::string publisher;
    std::string released;
    std::string notes;

    friend bool operator==(const UpdateOffer&, const UpdateOffer&) = default;
};

struct UpdateState {
    std::string index;
    std::optional<std::chrono::sys_seconds> checked;
    std::string problem;
    std::optional<UpdateOffer> offer;
    std::string skipped;

    friend bool operator==(const UpdateState&, const UpdateState&) = default;
};

using UpdateStates = std::map<std::string, UpdateState, std::less<>>;

[[nodiscard]] bool updateCheckDue(bool enabled, std::optional<std::chrono::sys_seconds> last,
                                  std::chrono::sys_seconds now);

class ExtensionUpdates final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantMap states READ states NOTIFY statesChanged FINAL)
    Q_PROPERTY(int sources READ sources NOTIFY statesChanged FINAL)
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged FINAL)
    Q_PROPERTY(QString downloading READ downloading NOTIFY downloadChanged FINAL)
    Q_PROPERTY(qreal progress READ progress NOTIFY downloadChanged FINAL)

public:
    using Route = std::function<QUrl(const QUrl&)>;

    static constexpr int kIdleMilliseconds = 30000;
    static constexpr int kMostRedirects = 8;
    static constexpr std::chrono::hours kCheckEvery{24};

    ExtensionUpdates(ExtensionHost& host, AppSettings& settings, ExtensionUpdatePaths paths,
                     QObject* parent = nullptr);
    ~ExtensionUpdates() override;
    ExtensionUpdates(const ExtensionUpdates&) = delete;
    ExtensionUpdates& operator=(const ExtensionUpdates&) = delete;

    static ExtensionUpdates* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    [[nodiscard]] static ExtensionUpdates* instance() noexcept { return s_instance; }

    bool checkWhenDue();
    void setRoute(Route route);

    [[nodiscard]] const UpdateStates& entries() const noexcept { return m_states; }
    [[nodiscard]] std::optional<std::chrono::sys_seconds> lastCheck() const noexcept { return m_lastCheck; }
    [[nodiscard]] QVariantMap states() const;
    [[nodiscard]] int sources() const;
    [[nodiscard]] bool checking() const noexcept { return !m_lists.empty(); }
    [[nodiscard]] QString downloading() const { return m_updating; }
    [[nodiscard]] qreal progress() const noexcept;

    Q_INVOKABLE void check();
    Q_INVOKABLE void update(const QString& id);
    Q_INVOKABLE void skip(const QString& id);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void release();

signals:
    void statesChanged();
    void checkingChanged();
    void downloadChanged();
    void packageReady(const QString& fileUrl, const QString& source);
    void failed(const QString& message);
    void checkFinished(int offered, int problems, bool manual);

private:
    struct Candidate {
        std::string id;
        ext::Version version;
        std::string index;
    };

    [[nodiscard]] std::vector<Candidate> candidates() const;
    [[nodiscard]] QNetworkAccessManager& network();
    void start(bool manual);
    void settle(const std::string& index, QString problem, const QByteArray& body);
    void finishCheck();
    void downloaded(const QString& problem, const QUrl& from);
    [[nodiscard]] QString verify(const std::filesystem::path& package, const std::string& id,
                                 const UpdateOffer& offer) const;
    void follow();
    void dropFile();
    void load();
    void save() const;

    static inline ExtensionUpdates* s_instance = nullptr;

    ExtensionHost& m_host;
    AppSettings& m_settings;
    ExtensionUpdatePaths m_paths;
    Route m_route;
    std::unique_ptr<QNetworkAccessManager> m_network;
    UpdateStates m_states;
    std::optional<std::chrono::sys_seconds> m_lastCheck;
    std::map<std::string, QPointer<UpdateTransfer>, std::less<>> m_lists;
    std::map<std::string, QByteArray, std::less<>> m_bodies;
    bool m_manual = false;
    int m_offered = 0;
    int m_problems = 0;
    QPointer<UpdateTransfer> m_download;
    std::unique_ptr<QFile> m_file;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    UpdateOffer m_offer;
    QString m_updating;
    QString m_ready;
    qint64 m_received = 0;
};

}
