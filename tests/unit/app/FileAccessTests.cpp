#include "FileAccess.h"

#include <doctest/doctest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <windows.h>
#include <winioctl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using gs::app::DeclaredFolder;
using gs::app::FileAccess;
using gs::app::FileReplies;
using gs::app::LogicCaller;

namespace {

class Application
{
public:
    Application()
    {
        if (QCoreApplication::instance() == nullptr) {
            m_app = std::make_unique<QCoreApplication>(m_argc, m_argv.data());
        }
    }

private:
    int m_argc = 1;
    char m_name[5] = "test";
    std::array<char*, 2> m_argv{m_name, nullptr};
    std::unique_ptr<QCoreApplication> m_app;
};

const LogicCaller kOwn{.logic = 1};
const LogicCaller kDeck{.logic = 2, .extension = QStringLiteral("com.example.deck")};
const LogicCaller kOther{.logic = 3};

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

void writeBytes(const std::filesystem::path& file, std::string_view bytes)
{
    std::ofstream out(file, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string readBytes(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

QString native(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

constexpr std::size_t kMostJunctionPath = 1024;

struct MountPoint {
    DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
    WORD length = 0;
    WORD reserved = 0;
    WORD substituteOffset = 0;
    WORD substituteLength = 0;
    WORD printOffset = 0;
    WORD printLength = 0;
    WCHAR path[kMostJunctionPath] = {};
};

bool makeJunction(const std::filesystem::path& link, const std::filesystem::path& target)
{
    std::error_code error;
    static_cast<void>(std::filesystem::create_directory(link, error));
    const std::wstring substitute = L"\\??\\" + std::filesystem::absolute(target, error).wstring();
    if (error || substitute.size() + 2 > kMostJunctionPath) {
        return false;
    }
    MountPoint point;
    const auto bytes = static_cast<WORD>(substitute.size() * sizeof(WCHAR));
    std::copy(substitute.begin(), substitute.end(), point.path);
    point.substituteLength = bytes;
    point.printOffset = static_cast<WORD>(bytes + sizeof(WCHAR));
    point.length = static_cast<WORD>(4 * sizeof(WORD) + bytes + 2 * sizeof(WCHAR));
    const HANDLE handle = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD returned = 0;
    const BOOL made = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, &point, static_cast<DWORD>(point.length) + 8,
                                      nullptr, 0, &returned, nullptr);
    CloseHandle(handle);
    return made != FALSE;
}

struct Fixture {
    Application app;
    QTemporaryDir temp;
    std::filesystem::path root{temp.path().toStdWString()};
    std::vector<std::pair<quint64, QString>> answers;
    std::vector<std::pair<quint64, QString>> refusals;
    std::vector<QVariantMap> prompts;
    std::deque<QString> choices;
    std::unique_ptr<FileAccess> files;

    Fixture()
    {
        REQUIRE(temp.isValid());
        files = std::make_unique<FileAccess>(root / "data" / "logic-folders.json");
        files->setReplies(FileReplies{
            .answer = [this](std::uint64_t, quint64 request, const QString& json) { answers.emplace_back(request, json); },
            .refuse = [this](std::uint64_t, quint64 request,
                             const QString& message) { refusals.emplace_back(request, message); },
        });
        files->setChooser([this](const QVariantMap& prompt) {
            prompts.push_back(prompt);
            if (choices.empty()) {
                return QString();
            }
            QString next = choices.front();
            choices.pop_front();
            return next;
        });
    }

    void ask(const LogicCaller& caller, std::string_view service, quint64 id, std::string_view json)
    {
        files->request(caller, service, id, QString::fromUtf8(json.data(), static_cast<qsizetype>(json.size())));
    }

    [[nodiscard]] bool settled(quint64 id) const
    {
        return std::ranges::any_of(answers, [id](const auto& entry) { return entry.first == id; }) ||
               std::ranges::any_of(refusals, [id](const auto& entry) { return entry.first == id; });
    }

    [[nodiscard]] QString answerOf(quint64 id) const
    {
        const auto found = std::ranges::find(answers, id, &std::pair<quint64, QString>::first);
        return found == answers.end() ? QString() : found->second;
    }

    [[nodiscard]] QJsonObject objectOf(quint64 id) const
    {
        return QJsonDocument::fromJson(answerOf(id).toUtf8()).object();
    }

    [[nodiscard]] QString refusalOf(quint64 id) const
    {
        const auto found = std::ranges::find(refusals, id, &std::pair<quint64, QString>::first);
        return found == refusals.end() ? QString() : found->second;
    }

    [[nodiscard]] bool waitFor(quint64 id)
    {
        return pumpUntil([this, id] { return settled(id); });
    }
};

std::function<std::string(const std::string&)> handleJson(const QString& handle)
{
    return [handle](const std::string& rest) { return R"({"handle":")" + handle.toStdString() + "\"" + rest + "}"; };
}

}

TEST_CASE("ADR-0112: files.pick gives a handle to the file chosen in the dialog, which reads in pieces and writes the "
          "whole file")
{
    Fixture fixture;
    fixture.files->setLimits(FileAccess::kMostFileBytes, 5);
    const std::filesystem::path picked = fixture.root / "notes.txt";
    writeBytes(picked, "\xEF\xBB\xBF" "h\xC3\xA9llo");
    fixture.choices.push_back(native(picked));
    fixture.ask(kOwn, "files.pick", 1,
                R"({"save":false,"filters":[{"name":"Notes","extensions":["txt","md"]}],"suggestedName":"draft.txt"})");
    REQUIRE(fixture.settled(1));
    INFO(fixture.refusalOf(1).toStdString());
    REQUIRE(fixture.prompts.size() == 1);
    const QVariantMap asked = fixture.prompts.front();
    CHECK(asked.value(QStringLiteral("kind")).toString() == QStringLiteral("open"));
    CHECK(asked.value(QStringLiteral("own")).toBool());
    CHECK(asked.value(QStringLiteral("filters")).toStringList() == QStringList{QStringLiteral("Notes (*.txt *.md)")});
    CHECK(asked.value(QStringLiteral("suggestedUrl")).toUrl().fileName() == QStringLiteral("draft.txt"));
    const QJsonObject made = fixture.objectOf(1);
    CHECK(made.value(u"name").toString() == QStringLiteral("notes.txt"));
    const auto json = handleJson(made.value(u"handle").toString());

    fixture.ask(kOwn, "files.pick.read", 2, json(R"(,"offset":0)"));
    REQUIRE(fixture.waitFor(2));
    const QJsonObject first = fixture.objectOf(2);
    CHECK(first.value(u"size").toInt() == 9);
    CHECK(first.value(u"modified").toDouble() > 0);
    CHECK(QByteArray::fromBase64(first.value(u"data").toString().toLatin1()) == QByteArray("\xEF\xBB\xBF" "h\xC3"));
    fixture.ask(kOwn, "files.pick.read", 3, json(R"(,"offset":5)"));
    REQUIRE(fixture.waitFor(3));
    CHECK(QByteArray::fromBase64(fixture.objectOf(3).value(u"data").toString().toLatin1()) == QByteArray("\xA9llo"));

    fixture.ask(kOwn, "files.pick.read", 4, json(R"(,"offset":0,"text":true)"));
    REQUIRE(fixture.waitFor(4));
    CHECK(fixture.objectOf(4).value(u"text").toString() == QString::fromUtf8("\xEF\xBB\xBF" "h"));
    CHECK(fixture.objectOf(4).value(u"next").toInt() == 4);
    fixture.ask(kOwn, "files.pick.read", 5, json(R"(,"offset":4,"text":true)"));
    REQUIRE(fixture.waitFor(5));
    CHECK(fixture.objectOf(5).value(u"text").toString() == QString::fromUtf8("\xC3\xA9llo"));
    CHECK(fixture.objectOf(5).value(u"next").toInt() == 9);

    fixture.ask(kOwn, "files.pick.write", 6, json(R"(,"text":"new")"));
    REQUIRE(fixture.waitFor(6));
    CHECK(fixture.answerOf(6) == QStringLiteral("null"));
    CHECK(readBytes(picked) == "new");
    CHECK(fixture.files->call(kOwn, "files.part", R"({"stage":3,"data":"AAE="})").code.empty());
    fixture.ask(kOwn, "files.pick.write", 7, json(R"(,"stage":3,"data":"Ag==")"));
    REQUIRE(fixture.waitFor(7));
    CHECK(readBytes(picked) == std::string("\x00\x01\x02", 3));

    CHECK(fixture.files->call(kOwn, "files.pick.close", json("")).code.empty());
    fixture.ask(kOwn, "files.pick.read", 8, json(R"(,"offset":0)"));
    CHECK(fixture.refusalOf(8).startsWith(QStringLiteral("GS-API-164 ")));
    fixture.ask(kOwn, "files.pick", 9, "{}");
    CHECK(fixture.answerOf(9) == QStringLiteral("null"));
    CHECK(fixture.files->handleCount() == 0);
}

TEST_CASE("ADR-0112: a file named in the save dialog is made at its first write, and an extension needs files.pick")
{
    Fixture fixture;
    const std::filesystem::path made = fixture.root / "made.bin";
    fixture.choices.push_back(native(made));
    fixture.ask(kOwn, "files.pick", 1, R"({"save":true,"suggestedName":"made.bin"})");
    REQUIRE(fixture.settled(1));
    CHECK(fixture.prompts.back().value(QStringLiteral("kind")).toString() == QStringLiteral("save"));
    const auto json = handleJson(fixture.objectOf(1).value(u"handle").toString());
    fixture.ask(kOwn, "files.pick.read", 2, json(R"(,"offset":0)"));
    REQUIRE(fixture.waitFor(2));
    CHECK(fixture.refusalOf(2).startsWith(QStringLiteral("GS-API-162 ")));
    fixture.ask(kOwn, "files.pick.write", 3, json(R"(,"data":"AAEC")"));
    REQUIRE(fixture.waitFor(3));
    CHECK(readBytes(made) == std::string("\x00\x01\x02", 3));

    fixture.ask(kDeck, "files.pick", 4, "{}");
    CHECK(fixture.refusalOf(4).startsWith(QStringLiteral("GS-API-160 ")));
    fixture.files->setExtensionNames({{QStringLiteral("com.example.deck"), QStringLiteral("Deck")}});
    fixture.files->setPermitted({QStringLiteral("com.example.deck")}, {});
    fixture.choices.push_back(native(made));
    fixture.ask(kDeck, "files.pick", 5, "{}");
    REQUIRE(fixture.settled(5));
    CHECK(fixture.prompts.back().value(QStringLiteral("requester")).toString() == QStringLiteral("Deck"));
    CHECK_FALSE(fixture.prompts.back().value(QStringLiteral("own")).toBool());
    const auto deck = handleJson(fixture.objectOf(5).value(u"handle").toString());

    quint64 id = 10;
    for (const std::string_view wrong : {R"({"save":"yes"})", R"j({"filters":[{"name":"A(b)","extensions":["png"]}]})j",
                                         R"({"filters":[{"name":"A","extensions":["p.ng"]}]})",
                                         R"({"filters":[{"name":"A","extensions":[]}]})", R"({"suggestedName":"a/b.txt"})",
                                         R"({"suggestedName":"con.txt"})"}) {
        CAPTURE(std::string(wrong));
        fixture.ask(kOwn, "files.pick", id, wrong);
        CHECK(fixture.refusalOf(id).startsWith(QStringLiteral("GS-API-161 ")));
        ++id;
    }

    CHECK(fixture.files->call(kDeck, "files.part", R"({"stage":1,"text":"abc"})").code.empty());
    fixture.files->setPermitted({}, {});
    fixture.ask(kDeck, "files.pick.read", 20, deck(R"(,"offset":0)"));
    CHECK(fixture.refusalOf(20).startsWith(QStringLiteral("GS-API-164 ")));
    fixture.ask(kDeck, "files.pick", 21, "{}");
    CHECK(fixture.refusalOf(21).startsWith(QStringLiteral("GS-API-160 ")));
    CHECK(fixture.files->call(kDeck, "files.part", R"({"stage":2,"text":"abc"})").code == "GS-API-160");
    fixture.files->setPermitted({}, {{QStringLiteral("com.example.deck"), {}}});
    CHECK(fixture.files->call(kDeck, "files.part", R"({"stage":3,"text":"abc"})").code == "GS-API-160");
    fixture.files->setPermitted(
        {}, {{QStringLiteral("com.example.deck"), {DeclaredFolder{QStringLiteral("exports"), QStringLiteral("A.")}}}});
    CHECK(fixture.files->call(kDeck, "files.part", R"({"stage":4,"text":"abc"})").code.empty());
    CHECK(fixture.files->call(kOwn, "files.part", R"({"stage":5,"text":"abc"})").code.empty());
}

TEST_CASE("ADR-0112: files.folder asks once for a declared folder, remembers it and keeps every path inside it")
{
    Fixture fixture;
    const std::filesystem::path exports = fixture.root / "exports";
    const std::filesystem::path outside = fixture.root / "outside";
    static_cast<void>(std::filesystem::create_directories(exports / "saves"));
    static_cast<void>(std::filesystem::create_directories(outside));
    writeBytes(exports / "saves" / "a.txt", "abc");
    writeBytes(outside / "secret.txt", "secret");
    const QString deck = QStringLiteral("com.example.deck");

    fixture.ask(kDeck, "files.folder", 1, R"({"name":"exports"})");
    CHECK(fixture.refusalOf(1).startsWith(QStringLiteral("GS-API-160 ")));
    fixture.files->setPermitted(
        {}, {{deck, {DeclaredFolder{QStringLiteral("exports"), QStringLiteral("Where it saves.")}}}});
    fixture.choices.push_back(native(exports));
    fixture.ask(kDeck, "files.folder", 2, R"({"name":"exports"})");
    REQUIRE(fixture.settled(2));
    INFO(fixture.refusalOf(2).toStdString());
    REQUIRE(fixture.prompts.size() == 1);
    CHECK(fixture.prompts[0].value(QStringLiteral("kind")).toString() == QStringLiteral("folder"));
    CHECK(fixture.prompts[0].value(QStringLiteral("folder")).toString() == QStringLiteral("exports"));
    CHECK(fixture.prompts[0].value(QStringLiteral("purpose")).toString() == QStringLiteral("Where it saves."));
    CHECK(fixture.objectOf(2).value(u"name").toString() == QStringLiteral("exports"));
    CHECK(fixture.files->folderOf(deck, QStringLiteral("exports")) == exports);
    const QVariantList shown = fixture.files->folderMap().value(deck).toList();
    REQUIRE(shown.size() == 1);
    CHECK(shown[0].toMap().value(QStringLiteral("path")).toString() == QDir::toNativeSeparators(native(exports)));
    CHECK(shown[0].toMap().value(QStringLiteral("purpose")).toString() == QStringLiteral("Where it saves."));

    fixture.ask(kDeck, "files.folder", 3, R"({"name":"exports"})");
    REQUIRE(fixture.settled(3));
    CHECK(fixture.prompts.size() == 1);
    {
        const FileAccess again(fixture.root / "data" / "logic-folders.json");
        CHECK(again.folderOf(deck, QStringLiteral("exports")) == exports);
    }

    const auto json = handleJson(fixture.objectOf(2).value(u"handle").toString());
    fixture.ask(kDeck, "files.folder.list", 4, json(R"(,"path":"")"));
    REQUIRE(fixture.waitFor(4));
    const QJsonArray top = QJsonDocument::fromJson(fixture.answerOf(4).toUtf8()).array();
    REQUIRE(top.size() == 1);
    CHECK(top[0].toObject().value(u"name").toString() == QStringLiteral("saves"));
    CHECK(top[0].toObject().value(u"folder").toBool());
    fixture.ask(kDeck, "files.folder.list", 5, json(R"(,"path":"saves/")"));
    REQUIRE(fixture.waitFor(5));
    const QJsonArray inner = QJsonDocument::fromJson(fixture.answerOf(5).toUtf8()).array();
    REQUIRE(inner.size() == 1);
    CHECK(inner[0].toObject().value(u"path").toString() == QStringLiteral("saves/a.txt"));
    CHECK(inner[0].toObject().value(u"size").toInt() == 3);
    CHECK_FALSE(inner[0].toObject().value(u"folder").toBool());

    fixture.ask(kDeck, "files.folder.read", 6, json(R"(,"path":"saves/a.txt","offset":0,"text":true)"));
    REQUIRE(fixture.waitFor(6));
    CHECK(fixture.objectOf(6).value(u"text").toString() == QStringLiteral("abc"));
    fixture.ask(kDeck, "files.folder.write", 7, json(R"(,"path":"new/deep/b.txt","text":"bee")"));
    REQUIRE(fixture.waitFor(7));
    INFO(fixture.refusalOf(7).toStdString());
    CHECK(readBytes(exports / "new" / "deep" / "b.txt") == "bee");
    fixture.ask(kDeck, "files.folder.remove", 8, json(R"(,"path":"new/deep/b.txt")"));
    REQUIRE(fixture.waitFor(8));
    CHECK(fixture.answerOf(8) == QStringLiteral("true"));
    fixture.ask(kDeck, "files.folder.remove", 9, json(R"(,"path":"new/deep/b.txt")"));
    REQUIRE(fixture.waitFor(9));
    CHECK(fixture.answerOf(9) == QStringLiteral("false"));
    fixture.ask(kDeck, "files.folder.remove", 10, json(R"(,"path":"new")"));
    REQUIRE(fixture.waitFor(10));
    CHECK(fixture.refusalOf(10).startsWith(QStringLiteral("GS-API-162 ")));
    fixture.ask(kDeck, "files.folder.remove", 11, json(R"(,"path":"new/deep")"));
    REQUIRE(fixture.waitFor(11));
    CHECK(fixture.answerOf(11) == QStringLiteral("true"));

    fixture.ask(kDeck, "files.folder.read", 12, json(R"(,"path":"","offset":0)"));
    CHECK(fixture.refusalOf(12).startsWith(QStringLiteral("GS-API-161 ")));
    quint64 next = 13;
    for (const std::string_view wrong :
         {R"(,"path":"../outside/secret.txt","offset":0)", R"(,"path":"C:/x.txt","offset":0)",
          R"(,"path":"saves\\a.txt","offset":0)", R"(,"path":"con.txt","offset":0)",
          R"(,"path":"saves/./a.txt","offset":0)"}) {
        CAPTURE(std::string(wrong));
        fixture.ask(kDeck, "files.folder.read", next, json(std::string(wrong)));
        REQUIRE(fixture.waitFor(next));
        CHECK(fixture.refusalOf(next).startsWith(QStringLiteral("GS-API-161 ")));
        ++next;
    }

    if (makeJunction(exports / "link", outside)) {
        fixture.ask(kDeck, "files.folder.read", 30, json(R"(,"path":"link/secret.txt","offset":0)"));
        REQUIRE(fixture.waitFor(30));
        CHECK(fixture.refusalOf(30).startsWith(QStringLiteral("GS-API-161 ")));
        fixture.ask(kDeck, "files.folder.write", 31, json(R"(,"path":"link/planted.txt","text":"x")"));
        REQUIRE(fixture.waitFor(31));
        CHECK(fixture.refusalOf(31).startsWith(QStringLiteral("GS-API-161 ")));
        CHECK_FALSE(std::filesystem::exists(outside / "planted.txt"));
        std::error_code error;
        static_cast<void>(std::filesystem::remove(exports / "link", error));
    } else {
        MESSAGE("no junction could be made here, so a link out of the folder was not tried");
    }

    CHECK(fixture.files->forgetFolder(deck, QStringLiteral("exports")));
    CHECK_FALSE(fixture.files->folderOf(deck, QStringLiteral("exports")).has_value());
    fixture.ask(kDeck, "files.folder.list", 40, json(R"(,"path":"")"));
    CHECK(fixture.refusalOf(40).startsWith(QStringLiteral("GS-API-164 ")));
    fixture.ask(kDeck, "files.folder", 41, R"({"name":"exports"})");
    CHECK(fixture.refusalOf(41).startsWith(QStringLiteral("GS-API-164 ")));
    CHECK(fixture.prompts.size() == 2);
    fixture.choices.push_back(native(outside));
    fixture.ask(kOwn, "files.folder", 42, R"({"name":"anything-1"})");
    REQUIRE(fixture.settled(42));
    CHECK(fixture.files->folderOf(QStringLiteral("mine"), QStringLiteral("anything-1")) == outside);
    fixture.ask(kOwn, "files.folder", 43, R"({"name":"Bad_Name"})");
    CHECK(fixture.refusalOf(43).startsWith(QStringLiteral("GS-API-161 ")));
}

TEST_CASE("ADR-0112: file handles keep to the size limit, belong to one logic and close with it, and requests wait "
          "their turn")
{
    Fixture fixture;
    fixture.files->setLimits(16, 4);
    const std::filesystem::path picked = fixture.root / "small.txt";
    writeBytes(picked, "0123456789abcdefg");
    fixture.choices.push_back(native(picked));
    fixture.ask(kOwn, "files.pick", 1, "{}");
    REQUIRE(fixture.settled(1));
    const auto json = handleJson(fixture.objectOf(1).value(u"handle").toString());
    fixture.ask(kOwn, "files.pick.read", 2, json(R"(,"offset":0)"));
    REQUIRE(fixture.waitFor(2));
    CHECK(fixture.refusalOf(2).startsWith(QStringLiteral("GS-API-163 ")));
    fixture.ask(kOwn, "files.pick.write", 3, json(R"(,"text":"0123456789abcdefg")"));
    CHECK(fixture.refusalOf(3).startsWith(QStringLiteral("GS-API-163 ")));
    CHECK(fixture.files->call(kOwn, "files.part", R"({"stage":1,"text":"0123456789abcdefg"})").code == "GS-API-163");
    fixture.ask(kOwn, "files.pick.write", 4, json(R"(,"text":"0123456789abcdef")"));
    REQUIRE(fixture.waitFor(4));
    CHECK(readBytes(picked) == "0123456789abcdef");

    fixture.ask(kOther, "files.pick.read", 5, json(R"(,"offset":0)"));
    CHECK(fixture.refusalOf(5).startsWith(QStringLiteral("GS-API-164 ")));
    CHECK(fixture.files->call(kOther, "files.pick.close", json("")).code.empty());
    CHECK(fixture.files->handleCount() == 1);
    fixture.files->endLogic(kOwn.logic);
    CHECK(fixture.files->handleCount() == 0);
    fixture.ask(kOwn, "files.pick.read", 6, json(R"(,"offset":0)"));
    CHECK(fixture.refusalOf(6).startsWith(QStringLiteral("GS-API-164 ")));

    fixture.files->setChooser({});
    for (quint64 id = 10; id < 18; ++id) {
        fixture.ask(kOther, "files.pick", id, "{}");
    }
    fixture.ask(kOther, "files.pick", 18, "{}");
    CHECK(fixture.refusalOf(18).startsWith(QStringLiteral("GS-API-163 ")));
    CHECK(fixture.files->prompt().value(QStringLiteral("ticket")).toString() == QStringLiteral("3:10"));
    fixture.files->cancelPrompt(QStringLiteral("3:10"));
    CHECK(fixture.answerOf(10) == QStringLiteral("null"));
    fixture.files->drop(kOther, 11);
    CHECK(fixture.files->prompt().value(QStringLiteral("ticket")).toString() == QStringLiteral("3:12"));
    fixture.files->endLogic(kOther.logic);
    CHECK(fixture.files->prompt().isEmpty());
}

TEST_CASE("ADR-0112: each logic may have 64 files and folders open, whatever other logic has open")
{
    Fixture fixture;
    const std::filesystem::path exports = fixture.root / "exports";
    static_cast<void>(std::filesystem::create_directories(exports));
    fixture.choices.push_back(native(exports));
    for (quint64 id = 1; id <= FileAccess::kMostHandles; ++id) {
        CAPTURE(id);
        fixture.ask(kOwn, "files.folder", id, R"({"name":"exports"})");
        REQUIRE(fixture.settled(id));
        CHECK(fixture.refusalOf(id).isEmpty());
    }
    CHECK(fixture.prompts.size() == 1);
    CHECK(fixture.files->handleCount() == FileAccess::kMostHandles);
    fixture.ask(kOwn, "files.folder", 100, R"({"name":"exports"})");
    CHECK(fixture.refusalOf(100).startsWith(QStringLiteral("GS-API-163 ")));

    fixture.ask(kOther, "files.folder", 101, R"({"name":"exports"})");
    REQUIRE(fixture.settled(101));
    CHECK(fixture.refusalOf(101).isEmpty());
    fixture.ask(kOther, "files.pick", 102, "{}");
    REQUIRE(fixture.settled(102));
    CHECK(fixture.answerOf(102) == QStringLiteral("null"));
    CHECK(fixture.files->handleCount() == FileAccess::kMostHandles + 1);
}
