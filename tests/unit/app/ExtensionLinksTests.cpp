#include "ExtensionLinks.h"

#include <doctest/doctest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUuid>

#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>

using gs::app::ExtensionLink;
using gs::app::ExtensionLinks;
using gs::app::LinkHandover;
using gs::app::SchemeRegistration;

namespace {

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

class RegistryScratch
{
public:
    RegistryScratch()
        : m_root(QStringLiteral("Software\\GameScrcpy-tests\\") + QUuid::createUuid().toString(QUuid::Id128))
    {
    }

    ~RegistryScratch()
    {
        RegDeleteTreeW(HKEY_CURRENT_USER, m_root.toStdWString().c_str());
        RegDeleteKeyW(HKEY_CURRENT_USER, m_root.toStdWString().c_str());
        RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\GameScrcpy-tests");
    }

    RegistryScratch(const RegistryScratch&) = delete;
    RegistryScratch& operator=(const RegistryScratch&) = delete;

    [[nodiscard]] QString classes() const { return m_root + QStringLiteral("\\Classes"); }

private:
    QString m_root;
};

QString installLink(const QString& query)
{
    return QStringLiteral("gamescrcpy://install?") + query;
}

QString launchedWith(const QString& rest)
{
    return QStringLiteral("\"C:\\Program Files\\GameScrcpy\\GameScrcpy.exe\" ") + rest;
}

QString commandLineOf(const QStringList& arguments)
{
    QStringList quoted;
    for (const QString& argument : arguments) {
        quoted.append(QLatin1Char('"') + argument + QLatin1Char('"'));
    }
    return launchedWith(quoted.join(QLatin1Char(' ')));
}

}

TEST_CASE("ADR-0112: a link installs a package from an https address with its SHA-256 or shows an extension")
{
    const QString hash(64, QLatin1Char('a'));
    const QString address = QStringLiteral("https%3A%2F%2Fexample.com%2Fext%2Fkit-1.0.0.gsext");
    const std::optional<ExtensionLink> install =
        gs::app::parseExtensionLink(installLink(QStringLiteral("url=") + address + QStringLiteral("&sha256=") + hash));
    REQUIRE(install.has_value());
    CHECK(install->kind == ExtensionLink::Kind::Install);
    CHECK(install->url == QUrl(QStringLiteral("https://example.com/ext/kit-1.0.0.gsext")));
    CHECK(install->sha256 == QByteArray(32, '\xaa'));
    CHECK(install->id.isEmpty());

    const std::optional<ExtensionLink> reordered = gs::app::parseExtensionLink(
        QStringLiteral("GameScrcpy://install/?sha256=") + hash.toUpper() + QStringLiteral("&url=") + address);
    REQUIRE(reordered.has_value());
    CHECK(reordered->sha256 == QByteArray(32, '\xaa'));
    CHECK(reordered->url == install->url);

    const std::optional<ExtensionLink> plain = gs::app::parseExtensionLink(
        installLink(QStringLiteral("url=https://example.com:8443/kit.gsext&sha256=") + hash));
    REQUIRE(plain.has_value());
    CHECK(plain->url.port() == 8443);
    CHECK(plain->url.host() == QStringLiteral("example.com"));

    const std::optional<ExtensionLink> show =
        gs::app::parseExtensionLink(QStringLiteral("gamescrcpy://extension/alice.kit"));
    REQUIRE(show.has_value());
    CHECK(show->kind == ExtensionLink::Kind::Show);
    CHECK(show->id == QStringLiteral("alice.kit"));
    CHECK(show->url.isEmpty());
    const std::optional<ExtensionLink> slashed =
        gs::app::parseExtensionLink(QStringLiteral("gamescrcpy://extension/community.esp32/"));
    REQUIRE(slashed.has_value());
    CHECK(slashed->id == QStringLiteral("community.esp32"));

    const QString shouted = QStringLiteral("GAMESCRCPY://extension/alice.kit");
    CHECK(gs::app::linkArgument({shouted}, commandLineOf({shouted})) == shouted);
}

TEST_CASE("ADR-0112: a link counts only as the one argument however the command line is split, so a link that breaks "
          "out of its quotes adds no options")
{
    const QString link = QStringLiteral("gamescrcpy://extension/alice.kit");
    CHECK(gs::app::linkArgument({link}, commandLineOf({link})) == link);
    const QList<QStringList> refused{
        {QStringLiteral("--safe-mode"), link},
        {link, QStringLiteral("--profile"), QStringLiteral("\\\\attacker\\share\\profile.json")},
        {QStringLiteral("gamescrcpy://x"), QStringLiteral("-platformpluginpath"), QStringLiteral("\\\\attacker\\share"),
         QStringLiteral("-platform"), QStringLiteral("evil")},
        {link, link},
    };
    for (const QStringList& arguments : refused) {
        CAPTURE(arguments.join(QLatin1Char(' ')).toStdString());
        CHECK(gs::app::linkArgument(arguments, commandLineOf(arguments)) == QString());
    }

    const QString oneForTheRuntime = QStringLiteral("gamescrcpy://x\" --profile \\\\attacker\\share\\profile.json");
    CHECK(gs::app::linkArgument({oneForTheRuntime},
                                launchedWith(QStringLiteral(
                                    "\"gamescrcpy://x\"\" --profile \\\\attacker\\share\\profile.json\""))) ==
          QString());
    const QStringList threeForTheRuntime{QStringLiteral("gamescrcpy://x\""), QStringLiteral("--profile"),
                                         QStringLiteral("\\\\attacker\\share\\profile.json")};
    CHECK(gs::app::linkArgument(threeForTheRuntime,
                                launchedWith(QStringLiteral(
                                    "\"gamescrcpy://x\"\"\" --profile \\\\attacker\\share\\profile.json\""))) ==
          QString());

    for (const QStringList& arguments :
         {QStringList{QStringLiteral("profile.json")}, QStringList{QStringLiteral("--safe-mode")}, QStringList{}}) {
        CAPTURE(arguments.join(QLatin1Char(' ')).toStdString());
        CHECK_FALSE(gs::app::linkArgument(arguments, commandLineOf(arguments)).has_value());
    }
}

TEST_CASE("ADR-0112: the address a package comes from is shown in its ASCII form, so a look-alike name passes for "
          "no other")
{
    const QUrl lookalike(QStringLiteral("https://g\u0456thub.com/kit.gsext"));
    REQUIRE(lookalike.isValid());
    CHECK(gs::app::shownHost(lookalike).startsWith(QStringLiteral("xn--")));
    CHECK(gs::app::shownHost(lookalike) != QStringLiteral("github.com"));
    CHECK(gs::app::shownHost(QUrl(QStringLiteral("https://b\u00fccher.de/kit.gsext"))) ==
          QStringLiteral("xn--bcher-kva.de"));
    CHECK(gs::app::shownHost(QUrl(QStringLiteral("https://Example.COM:8443/kit.gsext"))) ==
          QStringLiteral("example.com"));
}

TEST_CASE("ADR-0112: every malformed link is refused")
{
    const QString hash(64, QLatin1Char('a'));
    const QString address = QStringLiteral("https%3A%2F%2Fexample.com%2Fkit.gsext");
    const QString good = QStringLiteral("url=") + address + QStringLiteral("&sha256=") + hash;
    REQUIRE(gs::app::parseExtensionLink(installLink(good)).has_value());
    const QStringList refused{
        QString(),
        QStringLiteral("gamescrcpy:"),
        QStringLiteral("gamescrcpy://"),
        QStringLiteral("gamescrcpy:install?") + good,
        QStringLiteral("https://example.com/kit.gsext"),
        QStringLiteral("gamescrcpy://install"),
        QStringLiteral("gamescrcpy://install?"),
        installLink(QStringLiteral("url=") + address),
        installLink(QStringLiteral("sha256=") + hash),
        installLink(QStringLiteral("url=") + address + QStringLiteral("&sha256=") + hash.left(63)),
        installLink(QStringLiteral("url=") + address + QStringLiteral("&sha256=") + hash + QStringLiteral("a")),
        installLink(QStringLiteral("url=") + address + QStringLiteral("&sha256=") + QString(64, QLatin1Char('z'))),
        installLink(QStringLiteral("url=http%3A%2F%2Fexample.com%2Fkit.gsext&sha256=") + hash),
        installLink(QStringLiteral("url=ftp%3A%2F%2Fexample.com%2Fkit.gsext&sha256=") + hash),
        installLink(QStringLiteral("url=file%3A%2F%2F%2FC%3A%2Fkit.gsext&sha256=") + hash),
        installLink(QStringLiteral("url=https%3A%2F%2F%2Fkit.gsext&sha256=") + hash),
        installLink(QStringLiteral("url=https%3A%2F%2Fuser%3Apass%40example.com%2Fkit.gsext&sha256=") + hash),
        installLink(QStringLiteral("url=kit.gsext&sha256=") + hash),
        installLink(QStringLiteral("url=") + address + QStringLiteral("&") + good),
        installLink(good + QStringLiteral("&sha256=") + hash),
        installLink(good + QStringLiteral("&id=alice.kit")),
        installLink(good + QStringLiteral("&")),
        installLink(QStringLiteral("=x&") + good),
        installLink(QStringLiteral("url&sha256=") + hash),
        installLink(good + QStringLiteral("#top")),
        installLink(QStringLiteral("url=") + address + QStringLiteral(" &sha256=") + hash),
        installLink(QStringLiteral("url=https://example.com/kit.gsext?v=1&x=2&sha256=") + hash),
        installLink(QStringLiteral("url=https://example.com/") + QString(2100, QLatin1Char('k')) +
                    QStringLiteral("&sha256=") + hash),
        QStringLiteral("gamescrcpy://install/more?") + good,
        QStringLiteral("gamescrcpy://installer?") + good,
        QStringLiteral("gamescrcpy://extension/"),
        QStringLiteral("gamescrcpy://extension/alice"),
        QStringLiteral("gamescrcpy://extension/Alice.Kit"),
        QStringLiteral("gamescrcpy://extension/alice.kit?x=1"),
        QStringLiteral("gamescrcpy://extension/alice.kit/more"),
        QStringLiteral("gamescrcpy://extension/../alice.kit"),
        QStringLiteral("gamescrcpy://extension/alice.kit%2Fx"),
        QStringLiteral("gamescrcpy://extension/alice.kit\u00e9"),
        QStringLiteral("gamescrcpy://extension/alice.kit#card"),
        QStringLiteral("gamescrcpy://other/alice.kit"),
        QStringLiteral("gamescrcpy://") + QString(5000, QLatin1Char('a')),
    };
    for (const QString& link : refused) {
        CAPTURE(link.toStdString());
        CHECK_FALSE(gs::app::parseExtensionLink(link).has_value());
    }
}

TEST_CASE("ADR-0112: a second GameScrcpy hands its link to the running one over the pipe and hears back")
{
    ApplicationScope application;
    const QString unique = QUuid::createUuid().toString(QUuid::Id128);
    const QString name = QStringLiteral("GameScrcpy-links-test-") + unique;
    LinkHandover running(name);
    REQUIRE(running.listen());
    QStringList received;
    QObject::connect(&running, &LinkHandover::received, [&received](const QString& link) { received.append(link); });
    const QString link = QStringLiteral("gamescrcpy://extension/alice.kit");
    std::atomic<int> handed{-1};
    std::thread second([&name, &link, &handed] {
        handed = LinkHandover::send(name, link, std::chrono::seconds(10)) ? 1 : 0;
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while ((handed.load() < 0 || received.isEmpty()) && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents();
        std::this_thread::yield();
    }
    second.join();
    CHECK(handed.load() == 1);
    CHECK(received == QStringList{link});

    CHECK_FALSE(LinkHandover::send(QStringLiteral("GameScrcpy-links-nobody-") + unique, link,
                                   std::chrono::milliseconds(200)));
    CHECK_FALSE(LinkHandover::send(name, QString(), std::chrono::milliseconds(200)));
    CHECK_FALSE(LinkHandover::send(name, QStringLiteral("gamescrcpy://extension/a.b\ngamescrcpy://x"),
                                   std::chrono::milliseconds(200)));

    const QString pipe = gs::app::linkPipeName(std::filesystem::path(L"C:/Data/GameScrcpy"));
    CHECK(pipe.startsWith(QStringLiteral("GameScrcpy-links-")));
    CHECK(pipe == gs::app::linkPipeName(std::filesystem::path(L"c:\\data\\gamescrcpy")));
    CHECK(pipe != gs::app::linkPipeName(std::filesystem::path(L"C:/Portable/GameScrcpy/data")));
}

TEST_CASE("ADR-0112: gamescrcpy:// is registered for one copy under the current user and removed only by that copy")
{
    const RegistryScratch scratch;
    const QString classes = scratch.classes();
    const QString program = QStringLiteral("C:\\Portable\\GameScrcpy.exe");
    const QString another = QStringLiteral("D:\\Other\\GameScrcpy.exe");
    CHECK(gs::app::schemeCommand(program) == QStringLiteral("\"C:\\Portable\\GameScrcpy.exe\" \"%1\""));
    CHECK(gs::app::schemeCommand(QStringLiteral("C:/Portable/GameScrcpy.exe")) == gs::app::schemeCommand(program));
    CHECK(gs::app::schemeRegistration(classes, program) == SchemeRegistration::None);
    CHECK_FALSE(gs::app::unregisterScheme(classes, program));

    REQUIRE(gs::app::registerScheme(classes, program));
    CHECK(gs::app::schemeRegistration(classes, program) == SchemeRegistration::This);
    CHECK(gs::app::schemeRegistration(classes, QStringLiteral("c:/portable/gamescrcpy.exe")) == SchemeRegistration::This);
    CHECK(gs::app::schemeRegistration(classes, another) == SchemeRegistration::Other);
    const std::wstring base = (classes + QStringLiteral("\\gamescrcpy")).toStdWString();
    DWORD size = 0;
    CHECK(RegGetValueW(HKEY_CURRENT_USER, base.c_str(), L"URL Protocol", RRF_RT_REG_SZ, nullptr, nullptr, &size) ==
          ERROR_SUCCESS);
    CHECK_FALSE(gs::app::unregisterScheme(classes, another));
    CHECK(gs::app::schemeRegistration(classes, program) == SchemeRegistration::This);
    CHECK(gs::app::unregisterScheme(classes, program));
    CHECK(gs::app::schemeRegistration(classes, program) == SchemeRegistration::None);

    ExtensionLinks links(std::filesystem::path{});
    links.setClassesKey(classes);
    links.setProgram(program);
    CHECK(links.registration() == QStringLiteral("none"));
    CHECK(links.registerLinks());
    CHECK(links.registration() == QStringLiteral("this"));
    links.setProgram(another);
    CHECK(links.registration() == QStringLiteral("other"));
    CHECK_FALSE(links.unregisterLinks());
    links.setProgram(program);
    CHECK(links.unregisterLinks());
    CHECK(links.registration() == QStringLiteral("none"));
}

TEST_CASE("ADR-0112: opening a link asks to show the extension, and a link that cannot be opened only says so")
{
    ExtensionLinks links(std::filesystem::path{});
    QStringList shown;
    QStringList failures;
    QObject::connect(&links, &ExtensionLinks::extensionWanted, [&shown](const QString& id) { shown.append(id); });
    QObject::connect(&links, &ExtensionLinks::failed,
                     [&failures](const QString& message) { failures.append(message); });

    links.open(QStringLiteral("gamescrcpy://extension/alice.kit"));
    CHECK(shown == QStringList{QStringLiteral("alice.kit")});
    CHECK(failures.isEmpty());

    links.open(installLink(QStringLiteral("url=http%3A%2F%2Fexample.com%2Fkit.gsext&sha256=") +
                           QString(64, QLatin1Char('a'))));
    CHECK(failures.size() == 1);
    CHECK_FALSE(links.downloading());

    links.open(installLink(QStringLiteral("url=https%3A%2F%2Fexample.com%2Fkit.gsext&sha256=") +
                           QString(64, QLatin1Char('a'))));
    CHECK(failures.size() == 2);
    CHECK_FALSE(links.downloading());
    CHECK(links.progress() < 0.0);
    CHECK(shown.size() == 1);
    CHECK(ExtensionLinks::kMostPackageBytes == qint64{200} * 1024 * 1024);
}
