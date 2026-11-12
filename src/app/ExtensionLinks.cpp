#include "ExtensionLinks.h"

#include "AiNetwork.h"
#include "ExtensionHost.h"
#include "QtConversions.h"

#include <gs/base/BuildInfo.h>
#include <gs/ext/Manifest.h>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJSEngine>
#include <QLocalSocket>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUuid>
#include <QVariant>

#include <windows.h>

#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <string>
#include <system_error>
#include <utility>

namespace gs::app {

namespace {

constexpr qsizetype kMostLinkChars = 4096;
constexpr qsizetype kMostUrlChars = 2048;
constexpr qsizetype kMostAnswerBytes = 16;
constexpr auto kDefaultClasses = QLatin1StringView("Software\\Classes");

bool hexText(const QString& text)
{
    return std::ranges::all_of(text, [](QChar c) {
        const char16_t code = c.unicode();
        return (code >= u'0' && code <= u'9') || (code >= u'a' && code <= u'f') || (code >= u'A' && code <= u'F');
    });
}

bool isLink(const QString& argument)
{
    return argument.startsWith(QLatin1StringView("gamescrcpy:"), Qt::CaseInsensitive);
}

QStringList qtArgumentsOf(const QString& commandLine)
{
    const std::wstring text = commandLine.toStdWString();
    int count = 0;
    wchar_t** const split = CommandLineToArgvW(text.c_str(), &count);
    if (split == nullptr) {
        return {};
    }
    QStringList arguments;
    for (int index = 1; index < count; ++index) {
        arguments.append(QString::fromWCharArray(split[index]));
    }
    LocalFree(split);
    return arguments;
}

std::optional<ExtensionLink> installLink(const QString& query)
{
    QString address;
    QString hash;
    bool sawAddress = false;
    bool sawHash = false;
    const QStringList parts = query.split(QLatin1Char('&'));
    for (const QString& part : parts) {
        const qsizetype equals = part.indexOf(QLatin1Char('='));
        if (equals <= 0) {
            return std::nullopt;
        }
        const QString name = part.left(equals);
        const QString value = QUrl::fromPercentEncoding(part.mid(equals + 1).toLatin1());
        if (name == QLatin1StringView("url") && !sawAddress) {
            address = value;
            sawAddress = true;
        } else if (name == QLatin1StringView("sha256") && !sawHash) {
            hash = value;
            sawHash = true;
        } else {
            return std::nullopt;
        }
    }
    if (!sawAddress || !sawHash || hash.size() != 64 || !hexText(hash) || address.size() > kMostUrlChars) {
        return std::nullopt;
    }
    const QUrl url(address, QUrl::StrictMode);
    if (!url.isValid() || url.scheme().compare(QLatin1StringView("https"), Qt::CaseInsensitive) != 0 ||
        url.host().isEmpty() || !url.userInfo().isEmpty()) {
        return std::nullopt;
    }
    return ExtensionLink{.kind = ExtensionLink::Kind::Install,
                         .url = url,
                         .sha256 = QByteArray::fromHex(hash.toLatin1()),
                         .id = {}};
}

std::optional<QString> readDefault(HKEY root, const QString& key)
{
    const std::wstring path = key.toStdWString();
    DWORD size = 0;
    if (RegGetValueW(root, path.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, nullptr, &size) != ERROR_SUCCESS ||
        size == 0) {
        return std::nullopt;
    }
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, path.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, value.data(), &size) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    value.resize(std::wcslen(value.c_str()));
    return QString::fromStdWString(value);
}

bool writeValue(const QString& key, const wchar_t* name, const QString& value)
{
    const std::wstring path = key.toStdWString();
    HKEY opened = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                        &opened, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const std::wstring text = value.toStdWString();
    const LSTATUS status = RegSetValueExW(opened, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(text.c_str()),
                                          static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(opened);
    return status == ERROR_SUCCESS;
}

QString commandKey(const QString& classesKey)
{
    return classesKey + QStringLiteral("\\gamescrcpy\\shell\\open\\command");
}

bool fromAnotherComputer(const QLocalSocket& socket)
{
    const auto handle = reinterpret_cast<HANDLE>(socket.socketDescriptor());
    std::array<wchar_t, 256> client{};
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE ||
        GetNamedPipeClientComputerNameW(handle, client.data(), static_cast<ULONG>(client.size() * sizeof(wchar_t))) ==
            FALSE) {
        return false;
    }
    std::array<wchar_t, 256> own{};
    auto length = static_cast<DWORD>(own.size());
    if (GetComputerNameExW(ComputerNameNetBIOS, own.data(), &length) == FALSE) {
        return true;
    }
    return CompareStringOrdinal(client.data(), -1, own.data(), -1, TRUE) != CSTR_EQUAL;
}

}

std::optional<ExtensionLink> parseExtensionLink(const QString& text)
{
    const QString scheme = QStringLiteral("gamescrcpy://");
    if (text.size() > kMostLinkChars || !text.startsWith(scheme, Qt::CaseInsensitive)) {
        return std::nullopt;
    }
    const bool printable =
        std::ranges::all_of(text, [](QChar c) { return c.unicode() > 0x20 && c.unicode() < 0x7F; });
    if (!printable || text.contains(QLatin1Char('#'))) {
        return std::nullopt;
    }
    const QString rest = text.mid(scheme.size());
    const qsizetype question = rest.indexOf(QLatin1Char('?'));
    const QString path = question < 0 ? rest : rest.left(question);
    if (path.compare(QLatin1StringView("install"), Qt::CaseInsensitive) == 0 ||
        path.compare(QLatin1StringView("install/"), Qt::CaseInsensitive) == 0) {
        return question < 0 ? std::nullopt : installLink(rest.mid(question + 1));
    }
    const QLatin1StringView prefix("extension/");
    if (question >= 0 || !path.startsWith(prefix, Qt::CaseInsensitive)) {
        return std::nullopt;
    }
    QString id = path.mid(prefix.size());
    if (id.endsWith(QLatin1Char('/'))) {
        id.chop(1);
    }
    if (!ext::validExtensionId(id.toStdString())) {
        return std::nullopt;
    }
    return ExtensionLink{.kind = ExtensionLink::Kind::Show, .url = {}, .sha256 = {}, .id = id};
}

std::optional<QString> linkArgument(const QStringList& arguments, const QString& commandLine)
{
    const QStringList qtArguments = qtArgumentsOf(commandLine);
    if (std::ranges::none_of(arguments, isLink) && std::ranges::none_of(qtArguments, isLink)) {
        return std::nullopt;
    }
    if (arguments.size() != 1 || qtArguments.size() != 1 || !isLink(arguments.front()) ||
        !isLink(qtArguments.front())) {
        return QString();
    }
    return qtArguments.front();
}

QString ownCommandLine()
{
    return QString::fromWCharArray(GetCommandLineW());
}

QString shownHost(const QUrl& url)
{
    return url.host(QUrl::EncodeUnicode);
}

QString linkPipeName(const std::filesystem::path& dataDirectory)
{
    const QString folder =
        QDir::cleanPath(QFileInfo(QString::fromStdWString(dataDirectory.wstring())).absoluteFilePath()).toLower();
    const QByteArray digest = QCryptographicHash::hash(folder.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    return QStringLiteral("GameScrcpy-links-") + QString::fromLatin1(digest);
}

QString schemeCommand(const QString& program)
{
    return QStringLiteral("\"") + QDir::toNativeSeparators(program) + QStringLiteral("\" \"%1\"");
}

SchemeRegistration schemeRegistration(const QString& classesKey, const QString& program)
{
    const std::optional<QString> command = readDefault(HKEY_CURRENT_USER, commandKey(classesKey));
    if (!command) {
        return SchemeRegistration::None;
    }
    return command->compare(schemeCommand(program), Qt::CaseInsensitive) == 0 ? SchemeRegistration::This
                                                                              : SchemeRegistration::Other;
}

bool registerScheme(const QString& classesKey, const QString& program)
{
    const QString base = classesKey + QStringLiteral("\\gamescrcpy");
    return writeValue(base, nullptr, QStringLiteral("URL:GameScrcpy")) &&
           writeValue(base, L"URL Protocol", QString()) &&
           writeValue(base + QStringLiteral("\\DefaultIcon"), nullptr,
                      QStringLiteral("\"") + QDir::toNativeSeparators(program) + QStringLiteral("\",0")) &&
           writeValue(commandKey(classesKey), nullptr, schemeCommand(program));
}

bool unregisterScheme(const QString& classesKey, const QString& program)
{
    if (schemeRegistration(classesKey, program) != SchemeRegistration::This) {
        return false;
    }
    const std::wstring base = (classesKey + QStringLiteral("\\gamescrcpy")).toStdWString();
    RegDeleteTreeW(HKEY_CURRENT_USER, base.c_str());
    RegDeleteKeyW(HKEY_CURRENT_USER, base.c_str());
    return schemeRegistration(classesKey, program) == SchemeRegistration::None;
}

LinkHandover::LinkHandover(QString name, QObject* parent) : QObject(parent), m_name(std::move(name))
{
    connect(&m_server, &QLocalServer::newConnection, this, &LinkHandover::accept);
}

LinkHandover::~LinkHandover()
{
    m_server.close();
}

bool LinkHandover::listen()
{
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (m_server.listen(m_name)) {
        return true;
    }
    qWarning("links: cannot listen on the pipe %s: %s", qUtf8Printable(m_name), qUtf8Printable(m_server.errorString()));
    return false;
}

bool LinkHandover::send(const QString& name, const QString& link, std::chrono::milliseconds wait)
{
    const QByteArray message = link.toUtf8();
    if (message.isEmpty() || message.size() > kMostLinkBytes || message.contains('\n')) {
        return false;
    }
    const QDeadlineTimer deadline(wait);
    const auto left = [&deadline] { return static_cast<int>(std::max<qint64>(0, deadline.remainingTime())); };
    QLocalSocket socket;
    socket.connectToServer(name);
    if (!socket.waitForConnected(left())) {
        return false;
    }
    AllowSetForegroundWindow(ASFW_ANY);
    socket.write(message + '\n');
    while (socket.bytesToWrite() > 0) {
        if (!socket.waitForBytesWritten(left())) {
            return false;
        }
    }
    QByteArray answer = socket.readAll();
    while (!answer.contains('\n')) {
        if (!socket.waitForReadyRead(left())) {
            return false;
        }
        answer += socket.readAll();
        if (answer.size() > kMostAnswerBytes) {
            return false;
        }
    }
    return answer.startsWith("ok\n");
}

void LinkHandover::accept()
{
    while (QLocalSocket* socket = m_server.nextPendingConnection()) {
        if (fromAnotherComputer(*socket)) {
            qWarning("links: refused a connection from another computer");
            socket->abort();
            socket->deleteLater();
            continue;
        }
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { readFrom(socket); });
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        if (socket->bytesAvailable() > 0) {
            readFrom(socket);
        }
    }
}

void LinkHandover::readFrom(QLocalSocket* socket)
{
    if (!socket->canReadLine()) {
        if (socket->bytesAvailable() > kMostLinkBytes) {
            socket->abort();
            socket->deleteLater();
        }
        return;
    }
    const QString link = QString::fromUtf8(socket->readLine(kMostLinkBytes + 2)).trimmed();
    socket->write("ok\n");
    socket->flush();
    socket->disconnectFromServer();
    if (!link.isEmpty()) {
        emit received(link);
    }
}

ExtensionLinks::ExtensionLinks(std::filesystem::path staging, QObject* parent)
    : QObject(parent)
    , m_staging(std::move(staging))
    , m_classesKey(kDefaultClasses)
{
    if (s_instance == nullptr) {
        s_instance = this;
    }
    if (QCoreApplication::instance() != nullptr) {
        m_program = QCoreApplication::applicationFilePath();
    }
}

ExtensionLinks::~ExtensionLinks()
{
    drop();
    release();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

ExtensionLinks* ExtensionLinks::create(QQmlEngine*, QJSEngine*)
{
    if (s_instance == nullptr) {
        auto* made = new ExtensionLinks(std::filesystem::path{}, nullptr);
        QJSEngine::setObjectOwnership(made, QJSEngine::JavaScriptOwnership);
        return made;
    }
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void ExtensionLinks::open(const QString& link)
{
    const std::optional<ExtensionLink> parsed = parseExtensionLink(link);
    if (!parsed) {
        qWarning("links: refused a link that is not one GameScrcpy opens");
        emit failed(tr("GameScrcpy cannot open this link."));
        return;
    }
    if (parsed->kind == ExtensionLink::Kind::Show) {
        emit extensionWanted(parsed->id);
        return;
    }
    download(*parsed);
}

void ExtensionLinks::setProgram(QString program)
{
    m_program = std::move(program);
    emit registrationChanged();
}

void ExtensionLinks::setClassesKey(QString key)
{
    m_classesKey = std::move(key);
    emit registrationChanged();
}

bool ExtensionLinks::downloading() const
{
    return m_reply != nullptr;
}

qreal ExtensionLinks::progress() const noexcept
{
    if (m_total <= 0) {
        return -1.0;
    }
    return std::clamp(static_cast<qreal>(m_received) / static_cast<qreal>(m_total), 0.0, 1.0);
}

bool ExtensionLinks::portable() const
{
    return QCoreApplication::instance() != nullptr &&
           QFileInfo::exists(QCoreApplication::applicationDirPath() + QStringLiteral("/portable.flag"));
}

QString ExtensionLinks::registration() const
{
    switch (schemeRegistration(m_classesKey, m_program)) {
    case SchemeRegistration::This:
        return QStringLiteral("this");
    case SchemeRegistration::Other:
        return QStringLiteral("other");
    case SchemeRegistration::None:
        break;
    }
    if (m_classesKey == kDefaultClasses) {
        if (const std::optional<QString> shared = readDefault(HKEY_LOCAL_MACHINE, commandKey(m_classesKey))) {
            return shared->compare(schemeCommand(m_program), Qt::CaseInsensitive) == 0 ? QStringLiteral("this")
                                                                                       : QStringLiteral("other");
        }
    }
    return QStringLiteral("none");
}

void ExtensionLinks::cancel()
{
    if (m_reply != nullptr || m_file != nullptr) {
        qInfo("links: the download was cancelled");
        drop();
    }
}

void ExtensionLinks::release()
{
    if (m_ready.isEmpty()) {
        return;
    }
    if (const ExtensionHost* host = ExtensionHost::instance(); host != nullptr && host->installing()) {
        return;
    }
    QFile::remove(m_ready);
    m_ready.clear();
}

bool ExtensionLinks::registerLinks()
{
    const bool done = !m_program.isEmpty() && registerScheme(m_classesKey, m_program);
    emit registrationChanged();
    return done;
}

bool ExtensionLinks::unregisterLinks()
{
    const bool done = !m_program.isEmpty() && unregisterScheme(m_classesKey, m_program);
    emit registrationChanged();
    return done;
}

void ExtensionLinks::download(const ExtensionLink& link)
{
    if (m_reply != nullptr) {
        emit failed(tr("Another extension is still downloading."));
        return;
    }
    if (!m_ready.isEmpty()) {
        emit failed(tr("Finish installing the extension from the last link first."));
        return;
    }
    if (m_staging.empty()) {
        emit failed(tr("Extensions cannot be installed here."));
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(m_staging, error);
    const std::filesystem::path target =
        m_staging / ("link-" + QUuid::createUuid().toString(QUuid::Id128).toStdString() + ".gsext");
    auto file = std::make_unique<QFile>(QString::fromStdWString(target.wstring()));
    if (error || !file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit failed(tr("The download cannot be saved: %1").arg(file->errorString()));
        return;
    }
    m_file = std::move(file);
    m_hash.reset();
    m_expected = link.sha256;
    m_host = shownHost(link.url);
    m_received = 0;
    m_total = 0;
    if (m_network == nullptr) {
        m_network = std::make_unique<QNetworkAccessManager>();
        aiUseSystemProxy(*m_network);
    }
    QNetworkRequest request(link.url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(kMostRedirects);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setTransferTimeout(kIdleMilliseconds);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("GameScrcpy/") + toQString(buildInfo().version));
    QNetworkReply* reply = m_network->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::metaDataChanged, this, &ExtensionLinks::checkHeaders);
    connect(reply, &QNetworkReply::readyRead, this, &ExtensionLinks::take);
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64, qint64 total) {
        m_total = total;
        emit downloadChanged();
    });
    connect(reply, &QNetworkReply::finished, this, &ExtensionLinks::finish);
    qInfo("links: downloading an extension from %s", qUtf8Printable(m_host));
    emit downloadChanged();
}

void ExtensionLinks::checkHeaders()
{
    if (m_reply == nullptr) {
        return;
    }
    if (m_reply->url().scheme().compare(QLatin1StringView("https"), Qt::CaseInsensitive) != 0) {
        fail(tr("The download left https, so it was stopped."));
        return;
    }
    const QVariant length = m_reply->header(QNetworkRequest::ContentLengthHeader);
    if (length.isValid() && length.toLongLong() > kMostPackageBytes) {
        fail(tr("The package is larger than 200 MiB."));
    }
}

void ExtensionLinks::take()
{
    if (m_reply == nullptr || m_file == nullptr) {
        return;
    }
    const QByteArray chunk = m_reply->readAll();
    if (chunk.isEmpty()) {
        return;
    }
    m_received += chunk.size();
    if (m_received > kMostPackageBytes) {
        fail(tr("The package is larger than 200 MiB."));
        return;
    }
    m_hash.addData(chunk);
    if (m_file->write(chunk) != chunk.size()) {
        fail(tr("The download cannot be saved: %1").arg(m_file->errorString()));
    }
}

void ExtensionLinks::finish()
{
    if (m_reply == nullptr) {
        return;
    }
    take();
    if (m_reply == nullptr || m_file == nullptr) {
        return;
    }
    QNetworkReply* reply = m_reply;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError) {
        fail(tr("The download failed: %1").arg(reply->errorString()));
        return;
    }
    if (status < 200 || status >= 300) {
        fail(tr("The server answered %1 instead of sending the package.").arg(status));
        return;
    }
    if (m_hash.result() != m_expected) {
        fail(tr("The downloaded file is not the one the link names: its SHA-256 does not match."));
        return;
    }
    m_file->close();
    const QString path = m_file->fileName();
    m_file.reset();
    m_host = shownHost(reply->url());
    m_reply = nullptr;
    disconnect(reply, nullptr, this, nullptr);
    reply->deleteLater();
    m_ready = path;
    m_received = 0;
    m_total = 0;
    qInfo("links: the package from %s matches its SHA-256", qUtf8Printable(m_host));
    emit downloadChanged();
    emit packageReady(QUrl::fromLocalFile(path).toString(), m_host);
}

void ExtensionLinks::fail(const QString& message)
{
    qWarning("links: %s", qUtf8Printable(message));
    drop();
    emit failed(message);
}

void ExtensionLinks::drop()
{
    if (QNetworkReply* reply = m_reply) {
        m_reply = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    if (m_file != nullptr) {
        m_file->close();
        m_file->remove();
        m_file.reset();
    }
    m_received = 0;
    m_total = 0;
    emit downloadChanged();
}

}
