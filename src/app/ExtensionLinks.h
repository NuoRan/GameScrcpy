#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QLocalServer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

class QFile;
class QJSEngine;
class QLocalSocket;
class QNetworkAccessManager;
class QNetworkReply;
class QQmlEngine;

namespace gs::app {

struct ExtensionLink {
    enum class Kind : std::uint8_t { Install, Show };

    Kind kind = Kind::Show;
    QUrl url;
    QByteArray sha256;
    QString id;
};

enum class SchemeRegistration : std::uint8_t { None, This, Other };

[[nodiscard]] std::optional<ExtensionLink> parseExtensionLink(const QString& text);
[[nodiscard]] std::optional<QString> linkArgument(const QStringList& arguments, const QString& commandLine);
[[nodiscard]] QString ownCommandLine();
[[nodiscard]] QString shownHost(const QUrl& url);
[[nodiscard]] QString linkPipeName(const std::filesystem::path& dataDirectory);
[[nodiscard]] QString schemeCommand(const QString& program);
[[nodiscard]] SchemeRegistration schemeRegistration(const QString& classesKey, const QString& program);
[[nodiscard]] bool registerScheme(const QString& classesKey, const QString& program);
[[nodiscard]] bool unregisterScheme(const QString& classesKey, const QString& program);

class LinkHandover final : public QObject
{
    Q_OBJECT

public:
    static constexpr qsizetype kMostLinkBytes = 8192;

    explicit LinkHandover(QString name, QObject* parent = nullptr);
    ~LinkHandover() override;
    LinkHandover(const LinkHandover&) = delete;
    LinkHandover& operator=(const LinkHandover&) = delete;

    bool listen();
    [[nodiscard]] static bool send(const QString& name, const QString& link, std::chrono::milliseconds wait);

signals:
    void received(const QString& link);

private:
    void accept();
    void readFrom(QLocalSocket* socket);

    QString m_name;
    QLocalServer m_server;
};

class ExtensionLinks final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool downloading READ downloading NOTIFY downloadChanged FINAL)
    Q_PROPERTY(qreal progress READ progress NOTIFY downloadChanged FINAL)
    Q_PROPERTY(QString source READ source NOTIFY downloadChanged FINAL)
    Q_PROPERTY(bool portable READ portable CONSTANT FINAL)
    Q_PROPERTY(QString registration READ registration NOTIFY registrationChanged FINAL)

public:
    static constexpr qint64 kMostPackageBytes = qint64{200} << 20;
    static constexpr int kIdleMilliseconds = 30000;
    static constexpr int kMostRedirects = 8;

    explicit ExtensionLinks(std::filesystem::path staging, QObject* parent = nullptr);
    ~ExtensionLinks() override;
    ExtensionLinks(const ExtensionLinks&) = delete;
    ExtensionLinks& operator=(const ExtensionLinks&) = delete;

    static ExtensionLinks* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    [[nodiscard]] static ExtensionLinks* instance() noexcept { return s_instance; }

    void open(const QString& link);
    void setProgram(QString program);
    void setClassesKey(QString key);

    [[nodiscard]] bool downloading() const;
    [[nodiscard]] qreal progress() const noexcept;
    [[nodiscard]] QString source() const { return m_host; }
    [[nodiscard]] bool portable() const;
    [[nodiscard]] QString registration() const;

    Q_INVOKABLE void cancel();
    Q_INVOKABLE void release();
    Q_INVOKABLE bool registerLinks();
    Q_INVOKABLE bool unregisterLinks();

signals:
    void extensionWanted(const QString& id);
    void packageReady(const QString& fileUrl, const QString& source);
    void failed(const QString& message);
    void downloadChanged();
    void registrationChanged();

private:
    void download(const ExtensionLink& link);
    void checkHeaders();
    void take();
    void finish();
    void fail(const QString& message);
    void drop();

    static inline ExtensionLinks* s_instance = nullptr;

    std::filesystem::path m_staging;
    std::unique_ptr<QNetworkAccessManager> m_network;
    QPointer<QNetworkReply> m_reply;
    std::unique_ptr<QFile> m_file;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    QByteArray m_expected;
    QString m_host;
    QString m_ready;
    QString m_program;
    QString m_classesKey;
    qint64 m_received = 0;
    qint64 m_total = 0;
};

}
