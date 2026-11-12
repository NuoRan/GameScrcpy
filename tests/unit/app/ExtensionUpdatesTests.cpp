#include "AppSettings.h"
#include "ExtensionHost.h"
#include "ExtensionUpdates.h"
#include "Settings.h"

#include "unit/ext/TestFiles.h"

#include <gs/ext/Archive.h>
#include <gs/ext/Minisign.h>
#include <gs/ext/Signing.h>
#include <gs/ext/UpdateIndex.h>

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

using gs::app::AppSettings;
using gs::app::ExtensionHost;
using gs::app::ExtensionUpdates;
using gs::app::UpdateStates;
using namespace gs::ext::test;
namespace fs = std::filesystem;
using Json = nlohmann::json;

namespace {

constexpr std::string_view kIndexUrl = "https://updates.test/ember/index.json";
constexpr std::string_view kIndexPath = "/ember/index.json";

class ApplicationScope
{
public:
    ApplicationScope()
    {
        if (QCoreApplication::instance() == nullptr) {
            m_application = std::make_unique<QCoreApplication>(m_argc, m_argv.data());
        }
    }

    ApplicationScope(const ApplicationScope&) = delete;
    ApplicationScope& operator=(const ApplicationScope&) = delete;

private:
    int m_argc = 1;
    std::string m_name = "gs_app_tests";
    std::array<char*, 2> m_argv{m_name.data(), nullptr};
    std::unique_ptr<QCoreApplication> m_application;
};

bool pumpUntil(const std::function<bool()>& done)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

QByteArray response(int status, const QByteArray& reason, const QByteArray& body, const QByteArray& headers = {})
{
    QByteArray made = "HTTP/1.1 ";
    made.append(QByteArray::number(status));
    made.append(' ');
    made.append(reason);
    made.append("\r\nContent-Length: ");
    made.append(QByteArray::number(body.size()));
    made.append("\r\nConnection: close\r\n");
    made.append(headers);
    made.append("\r\n");
    made.append(body);
    return made;
}

class HttpServer
{
public:
    HttpServer()
    {
        REQUIRE(m_server.listen(QHostAddress::LocalHost, 0));
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this] { accept(); });
    }

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void serve(const std::string& path, QByteArray reply) { m_replies[path] = std::move(reply); }

    [[nodiscard]] ExtensionUpdates::Route route() const
    {
        const quint16 port = m_server.serverPort();
        return [port](const QUrl& url) {
            QUrl routed = url;
            routed.setScheme(QStringLiteral("http"));
            routed.setHost(QStringLiteral("127.0.0.1"));
            routed.setPort(port);
            return routed;
        };
    }

    [[nodiscard]] std::size_t asked(std::string_view path) const
    {
        return static_cast<std::size_t>(std::ranges::count(paths, path));
    }

    std::vector<std::string> paths;
    std::vector<QByteArray> heads;

private:
    void accept()
    {
        while (QTcpSocket* socket = m_server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] { read(socket); });
        }
    }

    void read(QTcpSocket* socket)
    {
        QByteArray& buffer = m_buffers[socket];
        buffer.append(socket->readAll());
        const qsizetype end = buffer.indexOf("\r\n\r\n");
        if (end < 0) {
            return;
        }
        const QByteArray head = buffer.left(end);
        m_buffers.erase(socket);
        const QList<QByteArray> request = head.left(head.indexOf("\r\n")).split(' ');
        const std::string path = request.size() > 1 ? request.at(1).toStdString() : std::string();
        heads.push_back(head);
        paths.push_back(path);
        const auto found = m_replies.find(path);
        socket->write(found != m_replies.end() ? found->second : response(404, "Not Found", {}));
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    std::map<std::string, QByteArray> m_replies;
    std::map<QTcpSocket*, QByteArray> m_buffers;
};

gs::ext::MinisignSecretKey testKey(std::uint8_t number)
{
    gs::ext::Ed25519Seed seed{};
    seed.fill(number);
    gs::ext::MinisignKeyId id{};
    id.fill(number);
    return gs::ext::makeMinisignKey(seed, id);
}

std::string keyText(const gs::ext::MinisignSecretKey& key)
{
    return gs::ext::minisignKeyText(gs::ext::minisignPublicKey(key));
}

QString keyIdOf(const gs::ext::MinisignSecretKey& key)
{
    return QString::fromStdString(gs::ext::minisignKeyId(gs::ext::minisignPublicKey(key)));
}

std::string signedBy(const gs::ext::MinisignSecretKey& key)
{
    return R"(, "authorKey": ")" + keyText(key) + "\"";
}

std::string themeManifest(std::string_view id, std::string_view version, std::string_view more = {},
                          std::string_view index = kIndexUrl)
{
    const std::string updates =
        index.empty() ? std::string() : R"(, "updates": {"index": ")" + std::string(index) + "\"}";
    return R"({"schemaVersion": 1, "id": ")" + std::string(id) + R"(", "version": ")" + std::string(version) +
           R"(", "name": "Theme", "license": "MIT", "requires": {"theme": "^1.0"},
               "contributes": {"themes": [{"name": "Ember", "dark": {"accent": "#FF6B3D"}}]})" +
           updates + std::string(more) + "}";
}

QByteArray bytesOf(const fs::path& file)
{
    return QByteArray::fromStdString(readFile(file));
}

std::string sha256Of(const fs::path& file)
{
    return QCryptographicHash::hash(bytesOf(file), QCryptographicHash::Sha256).toHex().toStdString();
}

QString textOf(const fs::path& path)
{
    return QString::fromStdWString(path.wstring());
}

QStringList namesOf(const QVariantList& entries)
{
    QStringList names;
    for (const QVariant& entry : entries) {
        names.append(entry.toMap().value(QStringLiteral("name")).toString());
    }
    return names;
}

Json listedAt(const std::string& version)
{
    return {{"version", version},
            {"url", "https://updates.test/packages/" + version + ".gsext"},
            {"sha256", std::string(64, 'a')},
            {"size", 1024}};
}

Json releaseOf(const std::string& version, const fs::path& package, const std::string& path)
{
    return {{"version", version},
            {"url", "https://updates.test" + path},
            {"sha256", sha256Of(package)},
            {"size", fs::file_size(package)}};
}

Json entryOf(const std::string& id, Json releases)
{
    return {{"id", id}, {"versions", std::move(releases)}};
}

QByteArray listOf(Json extensions)
{
    const Json index = {{"format", "gamescrcpy.extension-index"}, {"version", 1}, {"extensions", std::move(extensions)}};
    return response(200, "OK", QByteArray::fromStdString(index.dump()));
}

struct Fixture {
    Scratch scratch;
    fs::path data = scratch / "data";
    fs::path user = scratch / "user";

    [[nodiscard]] fs::path package(std::string_view stem, const std::string& manifestText,
                                   const gs::ext::MinisignSecretKey* key = nullptr) const
    {
        const fs::path folder = scratch / ("theme-" + std::string(stem));
        writeFile(folder / "manifest.json", manifestText);
        const fs::path archive = scratch / (std::string(stem) + ".gsext");
        const std::vector<gs::ext::Diagnostic> problems = key != nullptr
                                                              ? gs::ext::packSigned(folder, archive, *key, "test")
                                                              : gs::ext::packDirectory(folder, archive);
        INFO(listed(problems));
        REQUIRE(problems.empty());
        return archive;
    }

    [[nodiscard]] gs::app::ExtensionUpdatePaths paths() const
    {
        return {.state = data / "extension-updates.json", .staging = user / ".staging", .extensions = user};
    }
};

struct Heard {
    explicit Heard(ExtensionUpdates& updates)
    {
        QObject::connect(&updates, &ExtensionUpdates::checkFinished, &context,
                         [this](int found, int failed, bool asked) {
                             ++checks;
                             offered = found;
                             problems = failed;
                             manual = asked;
                         });
        QObject::connect(&updates, &ExtensionUpdates::packageReady, &context,
                         [this](const QString& fileUrl, const QString& source) {
                             ready.append(fileUrl);
                             sources.append(source);
                         });
        QObject::connect(&updates, &ExtensionUpdates::failed, &context,
                         [this](const QString& message) { failures.append(message); });
    }

    Heard(const Heard&) = delete;
    Heard& operator=(const Heard&) = delete;

    int checks = 0;
    int offered = 0;
    int problems = 0;
    bool manual = false;
    QStringList ready;
    QStringList sources;
    QStringList failures;
    QObject context;
};

struct Bench {
    ApplicationScope application;
    Fixture fixture;
    HttpServer server;
    AppSettings settings{fixture.data};
    ExtensionHost host{settings, {fixture.user, {}}};
    std::unique_ptr<ExtensionUpdates> updates;
    std::unique_ptr<Heard> heard;

    Bench() { host.start(false); }

    Bench(const Bench&) = delete;
    Bench& operator=(const Bench&) = delete;

    void open()
    {
        heard.reset();
        updates.reset();
        updates = std::make_unique<ExtensionUpdates>(host, settings, fixture.paths());
        updates->setRoute(server.route());
        heard = std::make_unique<Heard>(*updates);
    }

    void install(std::string_view stem, const std::string& manifestText,
                 const gs::ext::MinisignSecretKey* key = nullptr)
    {
        const gs::ext::InstalledExtension installed = host.install(fixture.package(stem, manifestText, key));
        INFO(listed(installed.diagnostics));
        REQUIRE(installed.ok());
    }

    void serveIndex(Json extensions) { server.serve(std::string(kIndexPath), listOf(std::move(extensions))); }

    bool check()
    {
        const int before = heard->checks;
        updates->check();
        return pumpUntil([&] { return heard->checks > before; });
    }

    QString update(std::string_view id)
    {
        const qsizetype ready = heard->ready.size();
        const qsizetype failures = heard->failures.size();
        updates->update(QString::fromUtf8(id.data(), static_cast<qsizetype>(id.size())));
        REQUIRE(pumpUntil([&] { return heard->ready.size() > ready || heard->failures.size() > failures; }));
        return heard->failures.size() > failures ? heard->failures.constLast() : QString();
    }

    [[nodiscard]] std::string offered(std::string_view id) const
    {
        const auto found = updates->entries().find(id);
        return found != updates->entries().end() && found->second.offer ? found->second.offer->version
                                                                         : std::string();
    }

    QString offerAndUpdate(const std::string& id, const std::string& version, const fs::path& package,
                           const std::string& publisher = {})
    {
        const std::string path = "/packages/" + package.filename().string();
        server.serve(path, response(200, "OK", bytesOf(package)));
        Json release = releaseOf(version, package, path);
        if (!publisher.empty()) {
            release["publisher"] = publisher;
        }
        serveIndex(Json::array({entryOf(id, Json::array({release}))}));
        REQUIRE(check());
        REQUIRE(offered(id) == version);
        return update(id);
    }

    [[nodiscard]] std::size_t staged() const
    {
        std::size_t count = 0;
        std::error_code error;
        for (const fs::directory_entry& entry : fs::directory_iterator(fixture.user / ".staging", error)) {
            count += entry.path().extension() == ".gsext" ? 1 : 0;
        }
        return count;
    }
};

}

TEST_CASE("ADR-0112: checking extensions for updates is off until it is turned on, and then runs at most once a day")
{
    CHECK_FALSE(gs::app::settings::Settings{}.updates.extensions);
    const std::chrono::sys_seconds now{std::chrono::days{20'000}};
    CHECK_FALSE(gs::app::updateCheckDue(false, std::nullopt, now));
    CHECK(gs::app::updateCheckDue(true, std::nullopt, now));
    CHECK_FALSE(gs::app::updateCheckDue(true, now - std::chrono::hours{23}, now));
    CHECK(gs::app::updateCheckDue(true, now - std::chrono::hours{24}, now));
    CHECK(gs::app::updateCheckDue(true, now + std::chrono::hours{1}, now));
    CHECK_FALSE(gs::app::updateCheckDue(false, now - std::chrono::days{3}, now));

    Bench bench;
    bench.install("ember-1.0.0", themeManifest("alice.ember", "1.0.0"));
    bench.open();
    bench.serveIndex(Json::array({entryOf("alice.ember", Json::array({listedAt("1.1.0")}))}));
    CHECK_FALSE(bench.settings.effective({}).updates.extensions);
    CHECK_FALSE(bench.updates->checkWhenDue());
    CHECK(bench.server.paths.empty());
    CHECK_FALSE(bench.updates->lastCheck().has_value());

    REQUIRE(bench.settings.setValue(QStringLiteral("updates.extensions"), true));
    REQUIRE(bench.updates->checkWhenDue());
    REQUIRE(pumpUntil([&bench] { return bench.heard->checks == 1; }));
    CHECK_FALSE(bench.heard->manual);
    CHECK(bench.heard->offered == 1);
    CHECK(bench.offered("alice.ember") == "1.1.0");
    REQUIRE(bench.updates->lastCheck().has_value());
    CHECK_FALSE(bench.updates->checkWhenDue());

    bench.open();
    CHECK(bench.updates->lastCheck().has_value());
    CHECK_FALSE(bench.updates->checkWhenDue());
    CHECK(bench.server.paths.size() == 1);

    REQUIRE(bench.settings.setValue(QStringLiteral("updates.extensions"), false));
    REQUIRE(bench.check());
    CHECK(bench.heard->manual);
    CHECK(bench.server.paths.size() == 2);
    for (const QByteArray& head : bench.server.heads) {
        CHECK(head.startsWith("GET /ember/index.json HTTP/1.1\r\n"));
        CHECK_FALSE(head.contains("alice.ember"));
        CHECK_FALSE(head.toLower().contains("cookie"));
    }
}

TEST_CASE("ADR-0112: a check reads each update list once, offers the newest version that runs here and notes what went "
          "wrong on the extension")
{
    Bench bench;
    bench.install("ember-1.0.0", themeManifest("alice.ember", "1.0.0"));
    bench.install("dusk-2.0.0", themeManifest("alice.dusk", "2.0.0"));
    bench.install("other-1.0.0", themeManifest("bob.other", "1.0.0", {}, "https://updates.test/other/index.json"));
    bench.install("local-1.0.0", themeManifest("carol.local", "1.0.0", {}, {}));
    bench.open();
    CHECK(bench.updates->sources() == 2);

    Json tooNew = listedAt("1.3.0");
    tooNew["requires"] = {{"theme", "^9.0"}};
    const Json both = Json::array(
        {entryOf("alice.ember", Json::array({listedAt("0.9.0"), listedAt("1.1.0"), listedAt("1.2.0"), tooNew})),
         entryOf("alice.dusk", Json::array({listedAt("2.0.0")}))});
    bench.serveIndex(both);
    bench.server.serve("/other/index.json", response(500, "Internal Server Error", {}));
    REQUIRE(bench.check());
    CHECK(bench.heard->manual);
    CHECK(bench.heard->offered == 1);
    CHECK(bench.heard->problems == 1);
    CHECK(bench.server.asked(kIndexPath) == 1);
    CHECK(bench.server.asked("/other/index.json") == 1);
    CHECK(bench.offered("alice.ember") == "1.2.0");
    CHECK(bench.offered("alice.dusk").empty());
    {
        const UpdateStates& states = bench.updates->entries();
        CHECK(states.at("alice.dusk").problem.empty());
        CHECK(states.at("bob.other").problem == "The server answered 500.");
        CHECK_FALSE(states.contains("carol.local"));
        CHECK(states.at("alice.ember").checked.has_value());
    }
    const QVariantMap ember = bench.updates->states().value(QStringLiteral("alice.ember")).toMap();
    CHECK(ember.value(QStringLiteral("version")).toString() == QStringLiteral("1.2.0"));
    CHECK(ember.value(QStringLiteral("host")).toString() == QStringLiteral("updates.test"));
    CHECK_FALSE(ember.value(QStringLiteral("checked")).toString().isEmpty());
    CHECK(ember.value(QStringLiteral("problem")).toString().isEmpty());

    bench.serveIndex(Json::array({both[0]}));
    REQUIRE(bench.check());
    CHECK(bench.updates->entries().at("alice.dusk").problem == "The update list does not name this extension.");

    bench.server.serve(std::string(kIndexPath),
                       response(302, "Found", {}, "Location: http://updates.test/ember/plain.json\r\n"));
    REQUIRE(bench.check());
    CHECK(bench.updates->entries().at("alice.ember").problem ==
          "The server moved the file to an address that is not https, so it was not followed.");
    CHECK(bench.offered("alice.ember") == "1.2.0");
    CHECK(bench.server.asked("/ember/plain.json") == 0);

    bench.server.serve(std::string(kIndexPath), response(301, "Moved Permanently", {}, "Location: /ember/moved.json\r\n"));
    bench.server.serve("/ember/moved.json", listOf(both));
    REQUIRE(bench.check());
    CHECK(bench.server.asked("/ember/moved.json") == 1);
    CHECK(bench.updates->entries().at("alice.ember").problem.empty());

    bench.server.serve(std::string(kIndexPath),
                       response(200, "OK", QByteArray(static_cast<qsizetype>(gs::ext::kMostUpdateIndexBytes) + 1, ' ')));
    REQUIRE(bench.check());
    CHECK(bench.updates->entries().at("alice.ember").problem == "The update list is larger than 1 MiB.");

    bench.server.serve(std::string(kIndexPath), response(200, "OK", "{\"format\":"));
    REQUIRE(bench.check());
    CHECK(bench.updates->entries().at("alice.ember").problem == "The update list cannot be read: $: not JSON");
    CHECK(bench.offered("alice.ember") == "1.2.0");
}

TEST_CASE("ADR-0112: an update downloads only when asked, has to match the size and SHA-256 its list names, and then "
          "goes to the install dialog")
{
    Bench bench;
    bench.install("ember-1.0.0", themeManifest("alice.ember", "1.0.0"));
    bench.open();
    const fs::path next = bench.fixture.package("ember-1.1.0", themeManifest("alice.ember", "1.1.0"));
    const std::string path = "/packages/ember-1.1.0.gsext";
    bench.server.serve(path, response(200, "OK", bytesOf(next)));
    bench.serveIndex(Json::array({entryOf("alice.ember", Json::array({releaseOf("1.1.0", next, path)}))}));
    REQUIRE(bench.check());
    CHECK(bench.server.asked(path) == 0);
    CHECK(bench.updates->downloading().isEmpty());
    CHECK(bench.updates->progress() < 0.0);
    CHECK(bench.update("bob.none") == QStringLiteral("There is no update for this extension."));

    CHECK(bench.update("alice.ember").isEmpty());
    REQUIRE(bench.heard->ready.size() == 1);
    CHECK(bench.heard->sources == QStringList{QStringLiteral("updates.test")});
    CHECK(bench.updates->downloading().isEmpty());
    const fs::path ready = QUrl(bench.heard->ready.constFirst()).toLocalFile().toStdWString();
    CHECK(readFile(ready) == readFile(next));
    CHECK(bench.update("alice.ember") == QStringLiteral("Finish installing the last update first."));
    const QVariantMap inspected = bench.host.inspect(bench.heard->ready.constFirst());
    CHECK(inspected.value(QStringLiteral("ok")).toBool());
    CHECK(inspected.value(QStringLiteral("changes")).toMap().value(QStringLiteral("from")).toString() ==
          QStringLiteral("1.0.0"));
    bench.updates->release();
    CHECK_FALSE(fs::exists(ready));
    CHECK(bench.offered("alice.ember") == "1.1.0");

    bench.updates->update(QStringLiteral("alice.ember"));
    CHECK(bench.updates->downloading() == QStringLiteral("alice.ember"));
    bench.updates->cancel();
    CHECK(bench.updates->downloading().isEmpty());
    CHECK(bench.staged() == 0);
    CHECK(bench.update("alice.ember").isEmpty());
    bench.updates->release();

    const auto refusedWith = [&bench, &next, &path](const std::function<void(Json&)>& change) {
        Json release = releaseOf("1.1.0", next, path);
        change(release);
        bench.serveIndex(Json::array({entryOf("alice.ember", Json::array({release}))}));
        REQUIRE(bench.check());
        return bench.update("alice.ember");
    };
    CHECK(refusedWith([](Json& release) { release["size"] = release["size"].get<std::uint64_t>() + 1; }) ==
          QStringLiteral("The download is not the size the update list names."));
    CHECK(refusedWith([](Json& release) { release["size"] = release["size"].get<std::uint64_t>() - 1; }) ==
          QStringLiteral("The download is larger than the update list says."));
    CHECK(refusedWith([](Json& release) { release["sha256"] = std::string(64, 'a'); }) ==
          QStringLiteral("The downloaded file is not the one the update list names: its SHA-256 does not match."));

    const auto moved = [](Json& release) { release["url"] = "https://updates.test/packages/moved.gsext"; };
    bench.server.serve("/packages/moved.gsext",
                       response(302, "Found", {}, "Location: http://updates.test/packages/ember-1.1.0.gsext\r\n"));
    CHECK(refusedWith(moved) ==
          QStringLiteral("The server moved the file to an address that is not https, so it was not followed."));
    bench.server.serve("/packages/moved.gsext", response(307, "Temporary Redirect", {},
                                                         "Location: https://updates.test/packages/ember-1.1.0.gsext\r\n"));
    CHECK(refusedWith(moved).isEmpty());
    bench.updates->release();

    const fs::path wrong = bench.fixture.package("ember-1.2.0", themeManifest("alice.ember", "1.2.0"));
    bench.server.serve("/packages/wrong.gsext", response(200, "OK", bytesOf(wrong)));
    bench.serveIndex(
        Json::array({entryOf("alice.ember", Json::array({releaseOf("1.1.0", wrong, "/packages/wrong.gsext")}))}));
    REQUIRE(bench.check());
    CHECK(bench.update("alice.ember") ==
          QStringLiteral("The package holds alice.ember 1.2.0, not the update the list names."));

    bench.server.serve(path, response(404, "Not Found", {}));
    CHECK(refusedWith([](Json&) {}) == QStringLiteral("The server answered 404."));
    CHECK(bench.staged() == 0);
    CHECK(bench.updates->downloading().isEmpty());
}

TEST_CASE("ADR-0112: an update has to be signed with the key of the installed version and with the key its list names")
{
    Bench bench;
    const gs::ext::MinisignSecretKey alice = testKey(0x31);
    const gs::ext::MinisignSecretKey mallory = testKey(0x32);
    bench.install("signed-1.0.0", themeManifest("alice.ember", "1.0.0", signedBy(alice)), &alice);
    bench.install("plain-1.0.0", themeManifest("bob.plain", "1.0.0"));
    bench.open();

    const fs::path same =
        bench.fixture.package("signed-1.1.0", themeManifest("alice.ember", "1.1.0", signedBy(alice)), &alice);
    CHECK(bench.offerAndUpdate("alice.ember", "1.1.0", same).isEmpty());
    bench.updates->release();
    CHECK(bench.offerAndUpdate("alice.ember", "1.1.0", same, keyText(alice)).isEmpty());
    bench.updates->release();

    const fs::path rekeyed =
        bench.fixture.package("rekeyed-1.1.0", themeManifest("alice.ember", "1.1.0", signedBy(mallory)), &mallory);
    CHECK(bench.offerAndUpdate("alice.ember", "1.1.0", rekeyed) ==
          QStringLiteral("This update is signed with key %1, the installed version with key %2. To accept the new key, "
                         "remove the extension and install the package again.")
              .arg(keyIdOf(mallory), keyIdOf(alice)));
    CHECK(bench.offerAndUpdate("alice.ember", "1.1.0", rekeyed, keyText(mallory)) ==
          QStringLiteral("This update is signed with key %1, the installed version with key %2. To accept the new key, "
                         "remove the extension and install the package again.")
              .arg(keyIdOf(mallory), keyIdOf(alice)));

    const fs::path stripped = bench.fixture.package("stripped-1.1.0", themeManifest("alice.ember", "1.1.0"));
    CHECK(bench.offerAndUpdate("alice.ember", "1.1.0", stripped) ==
          QStringLiteral("The installed version is signed with key %1, but this update is not signed. Updates have to "
                         "be signed with the same key.")
              .arg(keyIdOf(alice)));

    CHECK(bench.offerAndUpdate("alice.ember", "1.1.0", same, keyText(mallory)) ==
          QStringLiteral("The update list says key %1 signs this update, but the package is signed with key %2.")
              .arg(keyIdOf(mallory), keyIdOf(alice)));

    const fs::path plainUpdate = bench.fixture.package("plain-1.1.0", themeManifest("bob.plain", "1.1.0"));
    CHECK(bench.offerAndUpdate("bob.plain", "1.1.0", plainUpdate, keyText(alice)) ==
          QStringLiteral("The update list says key %1 signs this update, but the package is not signed.")
              .arg(keyIdOf(alice)));
    CHECK(bench.offerAndUpdate("bob.plain", "1.1.0", plainUpdate).isEmpty());
    bench.updates->release();
    const fs::path nowSigned =
        bench.fixture.package("plain-1.2.0", themeManifest("bob.plain", "1.2.0", signedBy(alice)), &alice);
    CHECK(bench.offerAndUpdate("bob.plain", "1.2.0", nowSigned, keyText(alice)).isEmpty());
    bench.updates->release();
    CHECK(bench.staged() == 0);
}

TEST_CASE("ADR-0112: a skipped version is not offered again but a newer one is, and the record outlives a restart")
{
    Bench bench;
    bench.install("ember-1.0.0", themeManifest("alice.ember", "1.0.0"));
    bench.open();
    bench.serveIndex(Json::array({entryOf("alice.ember", Json::array({listedAt("1.1.0")}))}));
    REQUIRE(bench.check());
    REQUIRE(bench.offered("alice.ember") == "1.1.0");

    bench.updates->skip(QStringLiteral("alice.ember"));
    CHECK(bench.offered("alice.ember").empty());
    CHECK(bench.updates->entries().at("alice.ember").skipped == "1.1.0");
    CHECK(bench.updates->states()
              .value(QStringLiteral("alice.ember"))
              .toMap()
              .value(QStringLiteral("skipped"))
              .toString() == QStringLiteral("1.1.0"));
    REQUIRE(bench.check());
    CHECK(bench.heard->offered == 0);
    CHECK(bench.offered("alice.ember").empty());

    bench.serveIndex(Json::array({entryOf("alice.ember", Json::array({listedAt("1.1.0"), listedAt("1.2.0")}))}));
    REQUIRE(bench.check());
    CHECK(bench.offered("alice.ember") == "1.2.0");

    const UpdateStates before = bench.updates->entries();
    const std::optional<std::chrono::sys_seconds> checkedAt = bench.updates->lastCheck();
    bench.open();
    CHECK(bench.updates->entries() == before);
    CHECK(bench.updates->lastCheck() == checkedAt);

    bench.install("ember-1.2.0", themeManifest("alice.ember", "1.2.0"));
    CHECK(bench.offered("alice.ember").empty());
    CHECK(bench.updates->entries().at("alice.ember").skipped == "1.1.0");
    REQUIRE(bench.host.remove(QStringLiteral("alice.ember"), QStringLiteral("1.2.0")));
    CHECK_FALSE(bench.updates->entries().contains("alice.ember"));

    bench.updates.reset();
    writeFile(bench.fixture.data / "extension-updates.json", "{\"format\": \"something else\"}");
    bench.open();
    CHECK(bench.updates->entries().empty());
    CHECK_FALSE(bench.updates->lastCheck().has_value());
}

TEST_CASE("ADR-0112: the install dialog for an update lists what it adds and drops, and only more than before asks again")
{
    ApplicationScope application;
    const Fixture fixture;
    const auto script = [&fixture](const std::string& version, std::string_view more) {
        const fs::path folder = fixture.scratch / ("deck-" + version);
        writeFile(folder / "manifest.json",
                  R"({"schemaVersion": 1, "id": "alice.deck", "version": ")" + version +
                      R"(", "name": "Deck", "license": "MIT", "requires": {"script": "^1.0"},
                      "contributes": {"scripts": [{"name": "app", "entry": "app.js", "run": "app"}]})" +
                      std::string(more) + "}");
        writeFile(folder / "app.js", "log.info(\"deck\");\n");
        const fs::path archive = fixture.scratch / ("deck-" + version + ".gsext");
        REQUIRE(gs::ext::packDirectory(folder, archive).empty());
        return archive;
    };
    const std::string notify = R"(, "permissions": [{"name": "notify", "reason": "Says when a deck is ready."}])";
    const fs::path first = script("1.0.0", notify);
    const fs::path base = fixture.package("base-1.0.0", themeManifest("bob.base", "1.0.0"));
    const fs::path wider = script("2.0.0", R"(, "permissions": [{"name": "files.pick", "reason": "Opens a picture."}],
                                              "dependencies": {"bob.base": "^1.0"})");
    const fs::path same = script("1.0.1", notify);
    const fs::path fresh = fixture.package("carol-1.0.0", themeManifest("carol.fresh", "1.0.0"));
    AppSettings settings(fixture.data);
    ExtensionHost host(settings, {fixture.user, {}});
    host.start(false);
    for (const fs::path& package : {first, base}) {
        const gs::ext::InstalledExtension installed = host.install(package);
        INFO(listed(installed.diagnostics));
        REQUIRE(installed.ok());
    }

    const QVariantMap offered = host.inspect(textOf(wider));
    REQUIRE(offered.contains(QStringLiteral("changes")));
    const QVariantMap changes = offered.value(QStringLiteral("changes")).toMap();
    CHECK(changes.value(QStringLiteral("from")).toString() == QStringLiteral("1.0.0"));
    CHECK(changes.value(QStringLiteral("needsConsent")).toBool());
    CHECK(namesOf(changes.value(QStringLiteral("addedPermissions")).toList()) ==
          QStringList{QStringLiteral("files.pick")});
    CHECK(namesOf(changes.value(QStringLiteral("removedPermissions")).toList()) ==
          QStringList{QStringLiteral("notify")});
    CHECK(changes.value(QStringLiteral("addedDependencies")).toStringList() == QStringList{QStringLiteral("bob.base")});
    CHECK(changes.value(QStringLiteral("removedDependencies")).toStringList().isEmpty());
    CHECK(changes.value(QStringLiteral("interfaces")).toList().isEmpty());
    const QVariantMap added = changes.value(QStringLiteral("addedPermissions")).toList().constFirst().toMap();
    CHECK(added.value(QStringLiteral("reason")).toString() == QStringLiteral("Opens a picture."));
    CHECK_FALSE(added.value(QStringLiteral("title")).toString().isEmpty());

    const QVariantMap quiet = host.inspect(textOf(same)).value(QStringLiteral("changes")).toMap();
    CHECK(quiet.value(QStringLiteral("from")).toString() == QStringLiteral("1.0.0"));
    CHECK_FALSE(quiet.value(QStringLiteral("needsConsent")).toBool());
    CHECK(quiet.value(QStringLiteral("addedPermissions")).toList().isEmpty());
    CHECK(quiet.value(QStringLiteral("removedPermissions")).toList().isEmpty());
    CHECK(quiet.value(QStringLiteral("addedDependencies")).toStringList().isEmpty());

    CHECK_FALSE(host.inspect(textOf(fresh)).contains(QStringLiteral("changes")));
}
