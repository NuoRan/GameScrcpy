#include "ExtensionUpdates.h"

#include "AiNetwork.h"
#include "AppSettings.h"
#include "ExtensionHost.h"
#include "ExtensionLinks.h"
#include "QtConversions.h"

#include <gs/base/BuildInfo.h>
#include <gs/ext/NativeCheck.h>
#include <gs/ext/Package.h>
#include <gs/ext/Resolution.h>
#include <gs/ext/Signing.h>
#include <gs/ext/UpdateIndex.h>
#include <gs/platform/AtomicFile.h>

#include <nlohmann/json.hpp>

#include <QFile>
#include <QJSEngine>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUuid>

#include <algorithm>
#include <format>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace gs::app {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kFormat = "gamescrcpy.extension-updates";
constexpr int kVersion = 1;
constexpr std::size_t kMostStateBytes = std::size_t{4} << 20;
constexpr std::size_t kMostStates = 1024;
constexpr std::size_t kMostProblemBytes = 1024;

std::chrono::sys_seconds now()
{
    return std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
}

std::string timeText(std::chrono::sys_seconds at)
{
    return std::format("{:%FT%TZ}", at);
}

std::optional<std::chrono::sys_seconds> timeNamed(const std::string& text)
{
    if (text.empty()) {
        return std::nullopt;
    }
    std::istringstream in(text);
    std::chrono::sys_seconds at{};
    in >> std::chrono::parse("%FT%TZ", at);
    return in.fail() ? std::nullopt : std::optional<std::chrono::sys_seconds>(at);
}

std::string textAt(const nlohmann::json& object, const char* key, std::size_t most)
{
    if (!object.contains(key) || !object.at(key).is_string()) {
        return {};
    }
    const std::string& text = object.at(key).get_ref<const std::string&>();
    return text.size() <= most ? text : std::string();
}

bool redirectStatus(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

bool secureUrl(const QUrl& url)
{
    return url.isValid() && url.scheme() == QLatin1StringView("https") && !url.host().isEmpty() &&
           url.userInfo().isEmpty();
}

QUrl urlOf(const std::string& text)
{
    return QUrl(QString::fromStdString(text), QUrl::StrictMode);
}

QString keyIdOf(const ext::MinisignKey& key)
{
    return QString::fromStdString(ext::minisignKeyId(key));
}

UpdateOffer offerOf(const ext::IndexedRelease& release)
{
    return {.version = release.version.text(),
            .url = release.url,
            .sha256 = release.sha256,
            .size = release.size,
            .publisher = release.publisher ? ext::minisignKeyText(*release.publisher) : std::string(),
            .released = release.released,
            .notes = release.notes};
}

std::optional<UpdateOffer> offerNamed(const nlohmann::json& node)
{
    if (!node.is_object()) {
        return std::nullopt;
    }
    UpdateOffer offer{.version = textAt(node, "version", 64),
                      .url = textAt(node, "url", ext::kMaxUpdateIndexLength),
                      .sha256 = textAt(node, "sha256", 64),
                      .size = 0,
                      .publisher = textAt(node, "publisher", 64),
                      .released = textAt(node, "released", 10),
                      .notes = textAt(node, "notes", ext::kMostReleaseNotesBytes)};
    if (node.contains("size") && node.at("size").is_number_unsigned()) {
        offer.size = node.at("size").get<std::uint64_t>();
    }
    const bool hex = offer.sha256.size() == 64 && std::ranges::all_of(offer.sha256, [](char c) {
                         return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                     });
    if (!ext::Version::parse(offer.version) || !ext::validUpdateUrl(offer.url) || !hex || offer.size == 0 ||
        offer.size > ext::kMostUpdatePackageBytes ||
        (!offer.publisher.empty() && !ext::parseMinisignKey(offer.publisher))) {
        return std::nullopt;
    }
    return offer;
}

nlohmann::json offerJson(const UpdateOffer& offer)
{
    nlohmann::json node = {{"version", offer.version}, {"url", offer.url}, {"sha256", offer.sha256},
                           {"size", offer.size}};
    if (!offer.publisher.empty()) {
        node["publisher"] = offer.publisher;
    }
    if (!offer.released.empty()) {
        node["released"] = offer.released;
    }
    if (!offer.notes.empty()) {
        node["notes"] = offer.notes;
    }
    return node;
}

}

class UpdateTransfer final : public QObject
{
public:
    using Take = std::function<QString(const QByteArray&)>;
    using Restart = std::function<void()>;
    using Done = std::function<void(const QString&, const QUrl&)>;

    UpdateTransfer(QNetworkAccessManager& network, ExtensionUpdates::Route route, QUrl url, qint64 most,
                   QString tooLarge, Take take, Restart restart, Done done, QObject* parent)
        : QObject(parent)
        , m_network(network)
        , m_route(std::move(route))
        , m_url(std::move(url))
        , m_most(most)
        , m_tooLarge(std::move(tooLarge))
        , m_take(std::move(take))
        , m_restart(std::move(restart))
        , m_done(std::move(done))
    {
        send();
    }

    ~UpdateTransfer() override { drop(); }
    UpdateTransfer(const UpdateTransfer&) = delete;
    UpdateTransfer& operator=(const UpdateTransfer&) = delete;

    void cancel()
    {
        m_done = nullptr;
        drop();
        deleteLater();
    }

private:
    void send()
    {
        QNetworkRequest request(m_route ? m_route(m_url) : m_url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
        request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
        request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
        request.setTransferTimeout(ExtensionUpdates::kIdleMilliseconds);
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("GameScrcpy/") + toQString(buildInfo().version));
        QNetworkReply* reply = m_network.get(request);
        m_reply = reply;
        connect(reply, &QNetworkReply::metaDataChanged, this, [this] { checkHeaders(); });
        connect(reply, &QNetworkReply::readyRead, this, [this] { take(); });
        connect(reply, &QNetworkReply::finished, this, [this] { finish(); });
    }

    [[nodiscard]] bool redirecting() const
    {
        return m_reply != nullptr &&
               redirectStatus(m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt());
    }

    void checkHeaders()
    {
        if (m_reply == nullptr || redirecting()) {
            return;
        }
        const QVariant length = m_reply->header(QNetworkRequest::ContentLengthHeader);
        if (length.isValid() && length.toLongLong() > m_most) {
            end(m_tooLarge);
        }
    }

    void take()
    {
        if (m_reply == nullptr) {
            return;
        }
        const QByteArray chunk = m_reply->readAll();
        if (chunk.isEmpty() || redirecting()) {
            return;
        }
        m_received += chunk.size();
        if (m_received > m_most) {
            end(m_tooLarge);
            return;
        }
        if (const QString problem = m_take(chunk); !problem.isEmpty()) {
            end(problem);
        }
    }

    void finish()
    {
        if (m_reply == nullptr) {
            return;
        }
        take();
        if (m_reply == nullptr) {
            return;
        }
        QNetworkReply* reply = m_reply;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (redirectStatus(status)) {
            const QUrl location = QUrl::fromEncoded(reply->rawHeader("Location"), QUrl::StrictMode);
            drop();
            const QUrl target = m_url.resolved(location);
            if (location.isEmpty() || !target.isValid()) {
                end(ExtensionUpdates::tr("The server moved the file without saying where."));
            } else if (!secureUrl(target)) {
                end(ExtensionUpdates::tr("The server moved the file to an address that is not https, so it was not "
                                         "followed."));
            } else if (++m_hops > ExtensionUpdates::kMostRedirects) {
                end(ExtensionUpdates::tr("The server moved the file too many times."));
            } else {
                m_url = target;
                m_received = 0;
                if (m_restart) {
                    m_restart();
                }
                send();
            }
            return;
        }
        if (status >= 300 || (status > 0 && status < 200)) {
            end(ExtensionUpdates::tr("The server answered %1.").arg(status));
            return;
        }
        if (reply->error() != QNetworkReply::NoError || status == 0) {
            end(ExtensionUpdates::tr("The download failed: %1").arg(reply->errorString()));
            return;
        }
        end({});
    }

    void end(const QString& problem)
    {
        drop();
        const Done done = std::exchange(m_done, nullptr);
        deleteLater();
        if (done) {
            done(problem, m_url);
        }
    }

    void drop()
    {
        if (QNetworkReply* reply = m_reply) {
            m_reply = nullptr;
            disconnect(reply, nullptr, this, nullptr);
            reply->abort();
            reply->deleteLater();
        }
    }

    QNetworkAccessManager& m_network;
    ExtensionUpdates::Route m_route;
    QUrl m_url;
    qint64 m_most = 0;
    QString m_tooLarge;
    Take m_take;
    Restart m_restart;
    Done m_done;
    QPointer<QNetworkReply> m_reply;
    qint64 m_received = 0;
    int m_hops = 0;
};

bool updateCheckDue(bool enabled, std::optional<std::chrono::sys_seconds> last, std::chrono::sys_seconds now)
{
    return enabled && (!last || *last > now || now - *last >= ExtensionUpdates::kCheckEvery);
}

ExtensionUpdates::ExtensionUpdates(ExtensionHost& host, AppSettings& settings, ExtensionUpdatePaths paths,
                                   QObject* parent)
    : QObject(parent)
    , m_host(host)
    , m_settings(settings)
    , m_paths(std::move(paths))
{
    if (s_instance == nullptr) {
        s_instance = this;
    }
    load();
    follow();
    connect(&m_host, &ExtensionHost::extensionsChanged, this, &ExtensionUpdates::follow);
}

ExtensionUpdates::~ExtensionUpdates()
{
    for (auto& entry : m_lists) {
        delete entry.second.data();
    }
    m_lists.clear();
    delete m_download.data();
    dropFile();
    release();
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

ExtensionUpdates* ExtensionUpdates::create(QQmlEngine*, QJSEngine*)
{
    Q_ASSERT(s_instance != nullptr);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

bool ExtensionUpdates::checkWhenDue()
{
    if (!updateCheckDue(m_settings.effective({}).updates.extensions, m_lastCheck, now()) || candidates().empty()) {
        return false;
    }
    start(false);
    return true;
}

void ExtensionUpdates::setRoute(Route route)
{
    m_route = std::move(route);
}

QVariantMap ExtensionUpdates::states() const
{
    QVariantMap states;
    for (const auto& [id, state] : m_states) {
        QVariantMap entry{{QStringLiteral("host"), shownHost(urlOf(state.index))},
                          {QStringLiteral("checked"),
                           state.checked ? QString::fromStdString(timeText(*state.checked)) : QString()},
                          {QStringLiteral("problem"), QString::fromStdString(state.problem)},
                          {QStringLiteral("skipped"), QString::fromStdString(state.skipped)}};
        if (state.offer) {
            const std::optional<ext::MinisignKey> key = ext::parseMinisignKey(state.offer->publisher);
            entry.insert(QStringLiteral("version"), QString::fromStdString(state.offer->version));
            entry.insert(QStringLiteral("released"), QString::fromStdString(state.offer->released));
            entry.insert(QStringLiteral("notes"), QString::fromStdString(state.offer->notes));
            entry.insert(QStringLiteral("size"), static_cast<double>(state.offer->size));
            entry.insert(QStringLiteral("publisher"), key ? keyIdOf(*key) : QString());
        }
        states.insert(QString::fromStdString(id), entry);
    }
    return states;
}

int ExtensionUpdates::sources() const
{
    std::set<std::string> indexes;
    for (const Candidate& candidate : candidates()) {
        indexes.insert(candidate.index);
    }
    return static_cast<int>(indexes.size());
}

qreal ExtensionUpdates::progress() const noexcept
{
    if (m_updating.isEmpty() || m_offer.size == 0) {
        return -1.0;
    }
    return std::clamp(static_cast<qreal>(m_received) / static_cast<qreal>(m_offer.size), 0.0, 1.0);
}

void ExtensionUpdates::check()
{
    start(true);
}

void ExtensionUpdates::update(const QString& id)
{
    if (m_download != nullptr) {
        emit failed(tr("Another update is still downloading."));
        return;
    }
    if (!m_ready.isEmpty()) {
        emit failed(tr("Finish installing the last update first."));
        return;
    }
    const auto found = m_states.find(id.toStdString());
    if (found == m_states.end() || !found->second.offer) {
        emit failed(tr("There is no update for this extension."));
        return;
    }
    if (m_paths.staging.empty()) {
        emit failed(tr("Extensions cannot be installed here."));
        return;
    }
    const UpdateOffer offer = *found->second.offer;
    const QUrl url = urlOf(offer.url);
    if (!ext::validUpdateUrl(offer.url) || !secureUrl(url)) {
        emit failed(tr("The update address is not an https address."));
        return;
    }
    std::error_code error;
    fs::create_directories(m_paths.staging, error);
    const fs::path target =
        m_paths.staging / ("update-" + QUuid::createUuid().toString(QUuid::Id128).toStdString() + ".gsext");
    auto file = std::make_unique<QFile>(QString::fromStdWString(target.wstring()));
    if (error || !file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit failed(tr("The download cannot be saved: %1").arg(file->errorString()));
        return;
    }
    m_file = std::move(file);
    m_hash.reset();
    m_received = 0;
    m_offer = offer;
    m_updating = id;
    m_download = new UpdateTransfer(
        network(), m_route, url, static_cast<qint64>(offer.size),
        tr("The download is larger than the update list says."),
        [this](const QByteArray& chunk) {
            m_hash.addData(chunk);
            m_received += chunk.size();
            emit downloadChanged();
            if (m_file->write(chunk) != chunk.size()) {
                return tr("The download cannot be saved: %1").arg(m_file->errorString());
            }
            return QString();
        },
        [this] {
            m_file->resize(0);
            m_file->seek(0);
            m_hash.reset();
            m_received = 0;
        },
        [this](const QString& problem, const QUrl& from) { downloaded(problem, from); }, this);
    qInfo("updates: downloading %s %s from %s", qUtf8Printable(id), offer.version.c_str(),
          qUtf8Printable(shownHost(url)));
    emit downloadChanged();
}

void ExtensionUpdates::skip(const QString& id)
{
    const auto found = m_states.find(id.toStdString());
    if (found == m_states.end() || !found->second.offer) {
        return;
    }
    found->second.skipped = found->second.offer->version;
    found->second.offer.reset();
    save();
    emit statesChanged();
}

void ExtensionUpdates::cancel()
{
    if (m_download == nullptr) {
        return;
    }
    m_download->cancel();
    m_download = nullptr;
    dropFile();
    m_updating.clear();
    m_received = 0;
    qInfo("updates: the download was cancelled");
    emit downloadChanged();
}

void ExtensionUpdates::release()
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

std::vector<ExtensionUpdates::Candidate> ExtensionUpdates::candidates() const
{
    std::vector<Candidate> found;
    for (const ExtensionHost::Extension& extension : m_host.extensions()) {
        if (extension.builtin || !extension.usable() || !extension.manifest->updates) {
            continue;
        }
        const ext::Manifest& manifest = *extension.manifest;
        const auto same = std::ranges::find(found, manifest.id, &Candidate::id);
        if (same == found.end()) {
            found.push_back({.id = manifest.id, .version = manifest.version, .index = manifest.updates->index});
        } else if (same->version < manifest.version) {
            *same = {.id = manifest.id, .version = manifest.version, .index = manifest.updates->index};
        }
    }
    return found;
}

QNetworkAccessManager& ExtensionUpdates::network()
{
    if (m_network == nullptr) {
        m_network = std::make_unique<QNetworkAccessManager>();
        aiUseSystemProxy(*m_network);
    }
    return *m_network;
}

void ExtensionUpdates::start(bool manual)
{
    m_manual = m_manual || manual;
    if (checking()) {
        return;
    }
    m_lastCheck = now();
    m_offered = 0;
    m_problems = 0;
    std::set<std::string> indexes;
    for (const Candidate& candidate : candidates()) {
        indexes.insert(candidate.index);
    }
    for (const std::string& index : indexes) {
        const QUrl url = urlOf(index);
        if (!ext::validUpdateUrl(index) || !secureUrl(url)) {
            settle(index, tr("The update address is not an https address."), {});
            continue;
        }
        m_bodies[index].clear();
        m_lists[index] = new UpdateTransfer(
            network(), m_route, url, static_cast<qint64>(ext::kMostUpdateIndexBytes),
            tr("The update list is larger than 1 MiB."),
            [this, index](const QByteArray& chunk) {
                m_bodies[index] += chunk;
                return QString();
            },
            [this, index] { m_bodies[index].clear(); },
            [this, index](const QString& problem, const QUrl&) {
                m_lists.erase(index);
                const QByteArray body = std::exchange(m_bodies[index], {});
                m_bodies.erase(index);
                settle(index, problem, body);
                if (m_lists.empty()) {
                    finishCheck();
                }
            },
            this);
    }
    qInfo("updates: checking %d update list(s)", static_cast<int>(indexes.size()));
    if (m_lists.empty()) {
        finishCheck();
        return;
    }
    emit checkingChanged();
}

void ExtensionUpdates::settle(const std::string& index, QString problem, const QByteArray& body)
{
    std::optional<ext::UpdateIndex> parsed;
    if (problem.isEmpty()) {
        ext::ReadIndex read =
            ext::readUpdateIndex(std::string_view(body.constData(), static_cast<std::size_t>(body.size())));
        if (read.index) {
            parsed = std::move(read.index);
        } else {
            problem = tr("The update list cannot be read: %1").arg(QString::fromStdString(read.problem));
        }
    }
    if (!problem.isEmpty()) {
        qWarning("updates: %s: %s", qUtf8Printable(shownHost(urlOf(index))), qUtf8Printable(problem));
    }
    const std::chrono::sys_seconds checked = now();
    for (const Candidate& candidate : candidates()) {
        if (candidate.index != index) {
            continue;
        }
        UpdateState& state = m_states[candidate.id];
        if (state.index != index) {
            state = UpdateState{.index = index, .checked = {}, .problem = {}, .offer = {}, .skipped = {}};
        }
        state.checked = checked;
        if (!problem.isEmpty()) {
            state.problem = problem.toStdString();
            ++m_problems;
            continue;
        }
        const ext::IndexedExtension* listed = parsed->find(candidate.id);
        if (listed == nullptr) {
            state.problem = tr("The update list does not name this extension.").toStdString();
            state.offer.reset();
            ++m_problems;
            continue;
        }
        state.problem.clear();
        const std::optional<ext::Version> skipped =
            state.skipped.empty() ? std::nullopt : ext::Version::parse(state.skipped);
        const ext::IndexedRelease* release =
            ext::offeredRelease(*listed, candidate.version, extensionHostApi(), skipped);
        if (release == nullptr) {
            state.offer.reset();
            continue;
        }
        state.offer = offerOf(*release);
        ++m_offered;
    }
}

void ExtensionUpdates::finishCheck()
{
    save();
    const bool manual = std::exchange(m_manual, false);
    qInfo("updates: %d update(s) found, %d list(s) with a problem", m_offered, m_problems);
    emit statesChanged();
    emit checkingChanged();
    emit checkFinished(m_offered, m_problems, manual);
}

void ExtensionUpdates::downloaded(const QString& problem, const QUrl& from)
{
    m_download = nullptr;
    const QString id = m_updating;
    QString refusal = problem;
    if (refusal.isEmpty() && m_received != static_cast<qint64>(m_offer.size)) {
        refusal = tr("The download is not the size the update list names.");
    }
    if (refusal.isEmpty() && m_hash.result().toHex() != QByteArray::fromStdString(m_offer.sha256)) {
        refusal = tr("The downloaded file is not the one the update list names: its SHA-256 does not match.");
    }
    fs::path package;
    if (m_file != nullptr) {
        package = m_file->fileName().toStdWString();
        m_file->close();
        m_file.reset();
    }
    if (refusal.isEmpty()) {
        refusal = verify(package, id.toStdString(), m_offer);
    }
    m_updating.clear();
    m_received = 0;
    if (!refusal.isEmpty()) {
        QFile::remove(QString::fromStdWString(package.wstring()));
        qWarning("updates: %s was not updated: %s", qUtf8Printable(id), qUtf8Printable(refusal));
        emit downloadChanged();
        emit failed(refusal);
        return;
    }
    m_ready = QString::fromStdWString(package.wstring());
    qInfo("updates: %s %s matches the update list", qUtf8Printable(id), m_offer.version.c_str());
    emit downloadChanged();
    emit packageReady(QUrl::fromLocalFile(m_ready).toString(), shownHost(from));
}

QString ExtensionUpdates::verify(const fs::path& package, const std::string& id, const UpdateOffer& offer) const
{
    const ext::PackageCheck check = ext::checkPackage(package, {.official = false});
    if (!check.manifest) {
        return tr("The download is not an extension package GameScrcpy can read.");
    }
    if (check.manifest->id != id || check.manifest->version.text() != offer.version) {
        return tr("The package holds %1 %2, not the update the list names.")
            .arg(QString::fromStdString(check.manifest->id), QString::fromStdString(check.manifest->version.text()));
    }
    const ext::SignatureCheck signature = ext::checkSignature(ext::archiveContents(package), check.manifest->authorKey);
    if (signature.state == ext::SignatureState::Invalid) {
        return tr("Its author signature does not match its files: it was changed after it was signed.");
    }
    const bool signedPackage = signature.state == ext::SignatureState::Valid && signature.key.has_value();
    if (!offer.publisher.empty()) {
        const std::optional<ext::MinisignKey> named = ext::parseMinisignKey(offer.publisher);
        if (!named) {
            return tr("The update list names a key that is not a minisign public key.");
        }
        if (!signedPackage) {
            return tr("The update list says key %1 signs this update, but the package is not signed.")
                .arg(keyIdOf(*named));
        }
        if (*signature.key != *named) {
            return tr("The update list says key %1 signs this update, but the package is signed with key %2.")
                .arg(keyIdOf(*named), keyIdOf(*signature.key));
        }
    }
    const std::optional<ext::MinisignKey> installed =
        m_paths.extensions.empty() ? std::nullopt : ext::publisherOf(m_paths.extensions, id);
    if (installed && !signedPackage) {
        return tr("The installed version is signed with key %1, but this update is not signed. Updates have to be "
                  "signed with the same key.")
            .arg(keyIdOf(*installed));
    }
    if (installed && *signature.key != *installed) {
        return tr("This update is signed with key %1, the installed version with key %2. To accept the new key, "
                  "remove the extension and install the package again.")
            .arg(keyIdOf(*signature.key), keyIdOf(*installed));
    }
    if (!ext::checkCompatibility(*check.manifest, extensionHostApi()).empty()) {
        return tr("This update does not run on this version of GameScrcpy.");
    }
    return {};
}

void ExtensionUpdates::follow()
{
    const std::vector<Candidate> found = candidates();
    bool changed = false;
    for (auto it = m_states.begin(); it != m_states.end();) {
        const auto candidate = std::ranges::find(found, it->first, &Candidate::id);
        if (candidate == found.end()) {
            it = m_states.erase(it);
            changed = true;
            continue;
        }
        UpdateState& state = it->second;
        if (state.index != candidate->index) {
            state = UpdateState{.index = candidate->index, .checked = {}, .problem = {}, .offer = {}, .skipped = {}};
            changed = true;
        } else if (state.offer) {
            const std::optional<ext::Version> offered = ext::Version::parse(state.offer->version);
            if (!offered || !(candidate->version < *offered)) {
                state.offer.reset();
                changed = true;
            }
        }
        ++it;
    }
    if (changed) {
        save();
    }
    emit statesChanged();
}

void ExtensionUpdates::dropFile()
{
    if (m_file != nullptr) {
        m_file->close();
        m_file->remove();
        m_file.reset();
    }
}

void ExtensionUpdates::load()
{
    if (m_paths.state.empty()) {
        return;
    }
    std::string contents;
    const std::error_code error = platform::readFile(m_paths.state, kMostStateBytes, contents);
    if (error) {
        if (!platform::fileMissing(error)) {
            qWarning("updates: the update record could not be read (error %d)", error.value());
        }
        return;
    }
    const nlohmann::json root = nlohmann::json::parse(contents, nullptr, false);
    if (!root.is_object() || textAt(root, "format", 64) != kFormat || !root.contains("version") ||
        !root.at("version").is_number_integer() || root.at("version").get<int>() != kVersion ||
        !root.contains("extensions") || !root.at("extensions").is_object()) {
        qWarning("updates: the update record is not one GameScrcpy can read; starting with none");
        return;
    }
    m_lastCheck = timeNamed(textAt(root, "checked", 32));
    for (const auto& [id, entry] : root.at("extensions").items()) {
        if (m_states.size() >= kMostStates) {
            break;
        }
        if (!ext::validExtensionId(id) || !entry.is_object()) {
            continue;
        }
        UpdateState state{.index = textAt(entry, "index", ext::kMaxUpdateIndexLength),
                          .checked = timeNamed(textAt(entry, "checked", 32)),
                          .problem = textAt(entry, "problem", kMostProblemBytes),
                          .offer = {},
                          .skipped = textAt(entry, "skipped", 64)};
        if (!ext::validUpdateUrl(state.index)) {
            continue;
        }
        if (!state.skipped.empty() && !ext::Version::parse(state.skipped)) {
            state.skipped.clear();
        }
        if (entry.contains("offer")) {
            state.offer = offerNamed(entry.at("offer"));
        }
        m_states.emplace(id, std::move(state));
    }
}

void ExtensionUpdates::save() const
{
    if (m_paths.state.empty()) {
        return;
    }
    nlohmann::json extensions = nlohmann::json::object();
    for (const auto& [id, state] : m_states) {
        nlohmann::json entry = {{"index", state.index}};
        if (state.checked) {
            entry["checked"] = timeText(*state.checked);
        }
        if (!state.problem.empty()) {
            entry["problem"] = state.problem;
        }
        if (!state.skipped.empty()) {
            entry["skipped"] = state.skipped;
        }
        if (state.offer) {
            entry["offer"] = offerJson(*state.offer);
        }
        extensions[id] = std::move(entry);
    }
    nlohmann::json root = {{"format", kFormat}, {"version", kVersion}, {"extensions", std::move(extensions)}};
    if (m_lastCheck) {
        root["checked"] = timeText(*m_lastCheck);
    }
    std::error_code error;
    fs::create_directories(m_paths.state.parent_path(), error);
    error = platform::writeFileAtomically(m_paths.state, root.dump(2) + "\n");
    if (error) {
        qWarning("updates: the update record could not be saved (error %d)", error.value());
    }
}

}
