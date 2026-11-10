#include "FileTransfers.h"
#include "PhoneFiles.h"

#include "unit/adb/FakeAdbServer.h"

#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using gs::adb::AdbClient;
using gs::adb::test::FakeAdbServer;
using gs::adb::test::FakeConnection;
using gs::adb::test::FakeStorage;
using gs::app::checkName;
using gs::app::DropFeedback;
using gs::app::DropNotice;
using gs::app::FilesAvailability;
using gs::app::FileTransfers;
using gs::app::NameProblem;
using gs::app::PhoneEntry;
using gs::app::PhoneFileModel;
using gs::app::PhoneFiles;
using gs::app::TransferState;

namespace {

class FilesApplication
{
public:
    FilesApplication()
    {
        if (QCoreApplication::instance() == nullptr) {
            m_application = std::make_unique<QCoreApplication>(m_argc, m_argv.data());
        }
    }

private:
    int m_argc = 1;
    std::string m_name = "gs_app_tests";
    std::array<char*, 2> m_argv{m_name.data(), nullptr};
    std::unique_ptr<QCoreApplication> m_application;
};

template <typename Predicate>
bool waitUntil(Predicate done)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        std::this_thread::yield();
    }
    return true;
}

std::filesystem::path pathOf(const QTemporaryDir& dir)
{
    return std::filesystem::path(dir.path().toStdWString());
}

void writeFile(const QString& path, const QByteArray& content)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(content);
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray pattern(int size)
{
    QByteArray content(size, '\0');
    for (int i = 0; i < size; ++i) {
        content[i] = static_cast<char>((i * 13 + 5) & 0xFF);
    }
    return content;
}

QStringList shownNames(const PhoneFileModel& model)
{
    QStringList shown;
    for (int row = 0; row < model.rowCount(); ++row) {
        shown.append(model.data(model.index(row), PhoneFileModel::NameRole).toString());
    }
    return shown;
}

int rowOf(const PhoneFileModel& model, const QString& name)
{
    return static_cast<int>(shownNames(model).indexOf(name));
}

PhoneEntry file(const char* name, quint64 size = 5)
{
    return PhoneEntry{.name = QString::fromUtf8(name), .directory = false, .size = size, .modified = 1700000000};
}

PhoneEntry folder(const char* name)
{
    return PhoneEntry{.name = QString::fromUtf8(name), .directory = true, .size = 0, .modified = 1700000000};
}

QVariantMap pushJob(qint64 id, const QString& state, double done, double total, const QString& message = {})
{
    return {{QStringLiteral("id"), id},       {QStringLiteral("state"), state},
            {QStringLiteral("done"), done},   {QStringLiteral("total"), total},
            {QStringLiteral("name"), QString()}, {QStringLiteral("message"), message}};
}

QVariantMap installJob(qint64 id, const QString& name, const QString& state, double done = 0, double total = 0)
{
    return {{QStringLiteral("id"), id},     {QStringLiteral("name"), name}, {QStringLiteral("state"), state},
            {QStringLiteral("done"), done}, {QStringLiteral("total"), total}};
}

PhoneFiles::Source onlinePhone(const bool& online)
{
    return {.availability = [&online](const QString&) {
        return FilesAvailability{.name = QStringLiteral("Pixel 8"), .online = online};
    }};
}

std::optional<gs::app::TransferJob> finished(const FileTransfers& transfers, qint64 id)
{
    const std::optional<gs::app::TransferJob> job = transfers.job(id);
    return job && job->finished() ? job : std::nullopt;
}

}

TEST_CASE("phone files: folders come first and names sort the way Explorer sorts them")
{
    PhoneFileModel model;
    model.reset(QStringLiteral("/sdcard/Download"),
                {file("b.txt"), file("A.txt", 2048), folder("Photos"), file("file10.txt"), file("file9.txt"),
                 folder("alpha"), folder("Zeta")});
    CHECK(shownNames(model) == QStringList{QStringLiteral("alpha"), QStringLiteral("Photos"), QStringLiteral("Zeta"),
                                           QStringLiteral("A.txt"), QStringLiteral("b.txt"),
                                           QStringLiteral("file9.txt"), QStringLiteral("file10.txt")});
    const QModelIndex photos = model.index(1);
    CHECK(model.data(photos, PhoneFileModel::DirectoryRole).toBool());
    CHECK(model.data(photos, PhoneFileModel::SizeTextRole).toString().isEmpty());
    CHECK(model.data(photos, PhoneFileModel::PathRole).toString() == QStringLiteral("/sdcard/Download/Photos"));
    const QModelIndex a = model.index(3);
    CHECK(model.data(a, PhoneFileModel::SizeTextRole).toString() == QStringLiteral("2.0 KB"));
    CHECK(model.data(a, PhoneFileModel::ModifiedRole).toDateTime().toSecsSinceEpoch() == 1700000000);
    CHECK_FALSE(model.data(a, PhoneFileModel::SelectedRole).toBool());
    CHECK(model.roleNames().value(PhoneFileModel::SelectedRole) == "selected");
    CHECK(model.roleNames().value(PhoneFileModel::SizeTextRole) == "sizeText");
}

TEST_CASE("phone files: a click selects one row, Ctrl adds or removes, Shift selects a range")
{
    PhoneFileModel model;
    model.reset(QStringLiteral("/sdcard"),
                {folder("DCIM"), file("a.txt", 10), file("b.txt", 20), file("c.txt", 30), file("d.txt", 40)});
    int changes = 0;
    QObject::connect(&model, &PhoneFileModel::selectionChanged, [&changes] { ++changes; });

    model.select(1, Qt::NoModifier);
    CHECK(model.selectedNames() == QStringList{QStringLiteral("a.txt")});
    model.select(3, Qt::ShiftModifier);
    CHECK(model.selectedNames() == QStringList{QStringLiteral("a.txt"), QStringLiteral("b.txt"), QStringLiteral("c.txt")});
    CHECK(model.selectedBytes() == 60);
    model.select(0, Qt::ControlModifier);
    CHECK(model.selectionHasFolders());
    model.select(2, Qt::ControlModifier);
    CHECK(model.selectedNames() == QStringList{QStringLiteral("DCIM"), QStringLiteral("a.txt"), QStringLiteral("c.txt")});
    model.select(4, Qt::NoModifier);
    CHECK(model.selectedNames() == QStringList{QStringLiteral("d.txt")});
    CHECK_FALSE(model.selectionHasFolders());
    model.selectAll();
    CHECK(model.selected().size() == 5);
    model.select(-1, Qt::NoModifier);
    CHECK(model.selected().empty());
    model.selectNames({QStringLiteral("b.txt"), QStringLiteral("gone.txt")});
    CHECK(model.selectedNames() == QStringList{QStringLiteral("b.txt")});
    const int before = changes;
    model.select(2, Qt::NoModifier);
    CHECK(changes == before);
    CHECK(model.data(model.index(2), PhoneFileModel::SelectedRole).toBool());
}

TEST_CASE("phone files: folders stay inside /sdcard and the crumbs lead back up")
{
    CHECK(gs::app::phoneFolder(QStringLiteral("/sdcard")) == QStringLiteral("/sdcard"));
    CHECK(gs::app::phoneFolder(QStringLiteral("/sdcard/")) == QStringLiteral("/sdcard"));
    CHECK(gs::app::phoneFolder(QStringLiteral("/sdcard//DCIM/Camera/")) == QStringLiteral("/sdcard/DCIM/Camera"));
    CHECK_FALSE(gs::app::phoneFolder(QStringLiteral("/sdcard/../data")).has_value());
    CHECK_FALSE(gs::app::phoneFolder(QStringLiteral("/sdcard/./x")).has_value());
    CHECK_FALSE(gs::app::phoneFolder(QStringLiteral("/data")).has_value());
    CHECK_FALSE(gs::app::phoneFolder(QStringLiteral("/sdcardx")).has_value());

    const QVariantList crumbs = gs::app::folderCrumbs(QStringLiteral("/sdcard/DCIM/Camera"));
    REQUIRE(crumbs.size() == 3);
    CHECK(crumbs[0].toMap().value(QStringLiteral("name")).toString().isEmpty());
    CHECK(crumbs[0].toMap().value(QStringLiteral("path")).toString() == QStringLiteral("/sdcard"));
    CHECK(crumbs[2].toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Camera"));
    CHECK(crumbs[2].toMap().value(QStringLiteral("path")).toString() == QStringLiteral("/sdcard/DCIM/Camera"));
    CHECK(gs::app::folderCrumbs(QStringLiteral("/sdcard")).size() == 1);

    CHECK(gs::app::remoteChild(QStringLiteral("/sdcard"), QStringLiteral("a b")) == QStringLiteral("/sdcard/a b"));
    CHECK(gs::app::remoteParent(QStringLiteral("/sdcard/DCIM")) == QStringLiteral("/sdcard"));
    CHECK(gs::app::remoteName(QStringLiteral("/sdcard/DCIM/x.jpg")) == QStringLiteral("x.jpg"));
}

TEST_CASE("phone files: names are checked before a folder is made or something is renamed")
{
    const QStringList taken{QStringLiteral("Photos"), QStringLiteral("photo.jpg")};
    CHECK(checkName(QString(), taken) == NameProblem::Empty);
    CHECK(checkName(QStringLiteral("   "), taken) == NameProblem::Empty);
    CHECK(checkName(QStringLiteral("a/b"), taken) == NameProblem::Slash);
    CHECK(checkName(QStringLiteral(".."), taken) == NameProblem::Dots);
    CHECK(checkName(QString(256, u'x'), taken) == NameProblem::TooLong);
    CHECK(checkName(QString(86, QChar(0x7167)), taken) == NameProblem::TooLong);
    CHECK(checkName(QString(85, QChar(0x7167)), taken) == NameProblem::None);
    CHECK(checkName(QStringLiteral("photos"), taken) == NameProblem::Taken);
    CHECK(checkName(QStringLiteral("Photo.jpg"), taken, QStringLiteral("photo.jpg")) == NameProblem::None);
    CHECK(checkName(QStringLiteral("新建 文件夹"), taken) == NameProblem::None);
}

TEST_CASE("file names: copies keep their extension and names unsafe on Windows are made safe")
{
    CHECK(gs::app::numberedName(QStringLiteral("photo.jpg"), 2, false) == QStringLiteral("photo (2).jpg"));
    CHECK(gs::app::numberedName(QStringLiteral("archive.tar.gz"), 3, false) == QStringLiteral("archive.tar (3).gz"));
    CHECK(gs::app::numberedName(QStringLiteral(".nomedia"), 2, false) == QStringLiteral(".nomedia (2)"));
    CHECK(gs::app::numberedName(QStringLiteral("v1.2"), 2, true) == QStringLiteral("v1.2 (2)"));
    CHECK(gs::app::numberedName(QStringLiteral("a%2b.txt"), 2, false) == QStringLiteral("a%2b (2).txt"));
    CHECK(gs::app::windowsName(QStringLiteral("a:b?.txt")) == QStringLiteral("a_b_.txt"));
    CHECK(gs::app::windowsName(QStringLiteral("con.txt")) == QStringLiteral("_con.txt"));
    CHECK(gs::app::windowsName(QStringLiteral("name. ")) == QStringLiteral("name"));
    CHECK(gs::app::windowsName(QStringLiteral("...")) == QStringLiteral("_"));
    CHECK(gs::app::windowsName(QStringLiteral("照片.jpg")) == QStringLiteral("照片.jpg"));
}

TEST_CASE("file sizes read in B, KB, MB and GB")
{
    CHECK(gs::app::fileSizeText(0) == QStringLiteral("0 B"));
    CHECK(gs::app::fileSizeText(1023) == QStringLiteral("1023 B"));
    CHECK(gs::app::fileSizeText(1536) == QStringLiteral("1.5 KB"));
    CHECK(gs::app::fileSizeText(5ULL * 1024 * 1024) == QStringLiteral("5.0 MB"));
    CHECK(gs::app::fileSizeText(3ULL * 1024 * 1024 * 1024) == QStringLiteral("3.0 GB"));
}

TEST_CASE("phone files: opening a phone lists /sdcard, and folders can be entered and left")
{
    const FilesApplication application;
    FakeStorage storage;
    storage.addFolder("/sdcard/DCIM");
    storage.addFile("/sdcard/DCIM/a.jpg", "aaa");
    storage.addFile("/sdcard/notes.txt", "hello");
    FakeAdbServer server([&storage](FakeConnection& c) { storage.serve(c); });
    FileTransfers transfers{AdbClient(server.port())};
    const QTemporaryDir temp;
    bool online = true;
    PhoneFiles files(AdbClient(server.port()), transfers, onlinePhone(online), pathOf(temp) / L"drag");

    files.open(QStringLiteral("emulator-5556"));
    CHECK(files.loading());
    REQUIRE(waitUntil([&] { return !files.loading(); }));
    CHECK(files.phoneName() == QStringLiteral("Pixel 8"));
    CHECK(files.folder() == QStringLiteral("/sdcard"));
    CHECK(files.problem().isEmpty());
    CHECK_FALSE(files.canGoUp());
    CHECK(shownNames(files.model()) == QStringList{QStringLiteral("DCIM"), QStringLiteral("notes.txt")});
    CHECK(files.itemCount() == 2);

    files.enter(rowOf(files.model(), QStringLiteral("DCIM")));
    CHECK(files.folder() == QStringLiteral("/sdcard/DCIM"));
    REQUIRE(waitUntil([&] { return !files.loading(); }));
    CHECK(shownNames(files.model()) == QStringList{QStringLiteral("a.jpg")});
    CHECK(files.canGoUp());
    CHECK(files.crumbs().size() == 2);

    files.enter(rowOf(files.model(), QStringLiteral("a.jpg")));
    CHECK(files.folder() == QStringLiteral("/sdcard/DCIM"));
    files.up();
    REQUIRE(waitUntil([&] { return !files.loading(); }));
    CHECK(files.folder() == QStringLiteral("/sdcard"));
    files.openFolder(QStringLiteral("/data"));
    CHECK(files.folder() == QStringLiteral("/sdcard"));

    online = false;
    files.refreshAvailability();
    CHECK_FALSE(files.online());
    CHECK_FALSE(files.problem().isEmpty());
    CHECK_FALSE(files.makeFolder(QStringLiteral("x")));
    online = true;
    files.refreshAvailability();
    REQUIRE(waitUntil([&] { return !files.loading(); }));
    CHECK(files.problem().isEmpty());

    files.close();
    CHECK(files.serial().isEmpty());
    CHECK(files.itemCount() == 0);
}

TEST_CASE("phone files: a new folder, a rename and a delete go through the phone's shell and the list follows")
{
    const FilesApplication application;
    FakeStorage storage;
    storage.addFolder("/sdcard/DCIM");
    storage.addFile("/sdcard/notes.txt", "hello");
    FakeAdbServer server([&storage](FakeConnection& c) { storage.serve(c); });
    FileTransfers transfers{AdbClient(server.port())};
    const QTemporaryDir temp;
    const bool online = true;
    PhoneFiles files(AdbClient(server.port()), transfers, onlinePhone(online), pathOf(temp) / L"drag");
    QStringList notices;
    QObject::connect(&files, &PhoneFiles::notice,
                     [&notices](const QString& text, const QString& tone) { notices.append(tone + u' ' + text); });
    files.open(QStringLiteral("emulator-5556"));
    REQUIRE(waitUntil([&] { return !files.loading(); }));
    const auto settled = [&] { return !files.busy() && !files.loading(); };

    CHECK_FALSE(files.nameProblem(QStringLiteral("dcim")).isEmpty());
    CHECK_FALSE(files.makeFolder(QStringLiteral("dcim")));
    REQUIRE(files.makeFolder(QStringLiteral("  新建 文件夹 ")));
    CHECK(files.busy());
    REQUIRE(waitUntil(settled));
    CHECK(storage.exists("/sdcard/新建 文件夹"));
    CHECK(storage.commands().back() ==
          AdbClient::makeFoldersCommand(std::vector<std::string>{"/sdcard/新建 文件夹"}));
    CHECK(rowOf(files.model(), QStringLiteral("新建 文件夹")) >= 0);
    REQUIRE_FALSE(notices.isEmpty());
    CHECK(notices.back().startsWith(QStringLiteral("ok ")));

    files.select(rowOf(files.model(), QStringLiteral("notes.txt")), 0);
    CHECK(files.selectedName() == QStringLiteral("notes.txt"));
    REQUIRE(files.rename(QStringLiteral("notes.txt"), QStringLiteral("it's notes.txt")));
    REQUIRE(waitUntil(settled));
    CHECK(storage.exists("/sdcard/it's notes.txt"));
    CHECK_FALSE(storage.exists("/sdcard/notes.txt"));
    CHECK(rowOf(files.model(), QStringLiteral("it's notes.txt")) >= 0);

    files.select(rowOf(files.model(), QStringLiteral("it's notes.txt")), 0);
    files.select(rowOf(files.model(), QStringLiteral("新建 文件夹")), Qt::ControlModifier);
    CHECK(files.selectedCount() == 2);
    REQUIRE(files.removeSelected());
    REQUIRE(waitUntil(settled));
    CHECK_FALSE(storage.exists("/sdcard/it's notes.txt"));
    CHECK_FALSE(storage.exists("/sdcard/新建 文件夹"));
    CHECK(shownNames(files.model()) == QStringList{QStringLiteral("DCIM")});
    CHECK(files.selectedCount() == 0);
}

TEST_CASE("file transfers: each transfer reports queued, running and done with its bytes, both ways")
{
    const FilesApplication application;
    FakeStorage storage;
    FakeAdbServer server([&storage](FakeConnection& c) { storage.serve(c); });
    FileTransfers transfers{AdbClient(server.port())};
    std::vector<QVariantMap> updates;
    QObject::connect(&transfers, &FileTransfers::updated, [&updates](const QVariantMap& job) { updates.push_back(job); });
    const QTemporaryDir temp;
    const QByteArray content = pattern(150000);
    const QString local = temp.filePath(QStringLiteral("report.pdf"));
    writeFile(local, content);

    const qint64 id = transfers.push(QStringLiteral("emulator-5556"), local, QStringLiteral("/sdcard/Download/"),
                                     QStringLiteral("files"));
    REQUIRE_FALSE(updates.empty());
    const QVariantMap queued = updates.front();
    CHECK(queued.value(QStringLiteral("id")).toLongLong() == id);
    CHECK(queued.value(QStringLiteral("state")).toString() == QStringLiteral("queued"));
    CHECK(queued.value(QStringLiteral("direction")).toString() == QStringLiteral("push"));
    CHECK(queued.value(QStringLiteral("serial")).toString() == QStringLiteral("emulator-5556"));
    CHECK(queued.value(QStringLiteral("name")).toString() == QStringLiteral("report.pdf"));
    for (const char* key : {"done", "total", "message"}) {
        CHECK_MESSAGE(queued.contains(QString::fromLatin1(key)), key);
    }
    REQUIRE(waitUntil([&] { return finished(transfers, id).has_value(); }));
    const QVariantMap done = updates.back();
    CHECK(done.value(QStringLiteral("state")).toString() == QStringLiteral("done"));
    CHECK(done.value(QStringLiteral("done")).toDouble() == 150000);
    CHECK(done.value(QStringLiteral("total")).toDouble() == 150000);
    CHECK(done.value(QStringLiteral("remote")).toString() == QStringLiteral("/sdcard/Download/report.pdf"));
    CHECK(std::ranges::any_of(updates, [](const QVariantMap& job) {
        return job.value(QStringLiteral("state")).toString() == QStringLiteral("running");
    }));
    const auto stored = storage.node("/sdcard/Download/report.pdf");
    REQUIRE(stored.has_value());
    CHECK(QByteArray::fromStdString(stored->content) == content);

    const qint64 back = transfers.pull(QStringLiteral("emulator-5556"), QStringLiteral("/sdcard/Download/report.pdf"),
                                       temp.filePath(QStringLiteral("back")));
    REQUIRE(waitUntil([&] { return finished(transfers, back).has_value(); }));
    const auto pulled = transfers.job(back);
    REQUIRE(pulled.has_value());
    CHECK(pulled->state == TransferState::Done);
    CHECK(pulled->direction == gs::app::TransferDirection::Pull);
    CHECK(pulled->local.endsWith(QStringLiteral("/back/report.pdf")));
    CHECK(readFile(pulled->local) == content);
    CHECK(transfers.jobs(QStringLiteral("emulator-5556")).front().id == back);
}

TEST_CASE("file transfers: a name already taken is kept and the copy gets a number")
{
    const FilesApplication application;
    FakeStorage storage;
    storage.addFile("/sdcard/Download/a.txt", "phone");
    FakeAdbServer server([&storage](FakeConnection& c) { storage.serve(c); });
    FileTransfers transfers{AdbClient(server.port())};
    const QTemporaryDir temp;
    writeFile(temp.filePath(QStringLiteral("a.txt")), "computer");

    const qint64 up = transfers.push(QStringLiteral("emulator-5556"), temp.filePath(QStringLiteral("a.txt")),
                                     QStringLiteral("/sdcard/Download"));
    REQUIRE(waitUntil([&] { return finished(transfers, up).has_value(); }));
    CHECK(transfers.job(up)->remote == QStringLiteral("/sdcard/Download/a (2).txt"));
    CHECK(storage.node("/sdcard/Download/a.txt")->content == "phone");
    CHECK(storage.node("/sdcard/Download/a (2).txt")->content == "computer");

    const qint64 down =
        transfers.pull(QStringLiteral("emulator-5556"), QStringLiteral("/sdcard/Download/a.txt"), temp.path());
    REQUIRE(waitUntil([&] { return finished(transfers, down).has_value(); }));
    CHECK(transfers.job(down)->local.endsWith(QStringLiteral("/a (2).txt")));
    CHECK(readFile(temp.filePath(QStringLiteral("a.txt"))) == "computer");
    CHECK(readFile(temp.filePath(QStringLiteral("a (2).txt"))) == "phone");
}

TEST_CASE("file transfers: folders are copied with everything in them, both ways")
{
    const FilesApplication application;
    FakeStorage storage;
    FakeAdbServer server([&storage](FakeConnection& c) { storage.serve(c); });
    FileTransfers transfers{AdbClient(server.port())};
    const QTemporaryDir temp;
    writeFile(temp.filePath(QStringLiteral("Trip/day1/a.jpg")), pattern(70000));
    writeFile(temp.filePath(QStringLiteral("Trip/b.txt")), "bbb");
    QDir().mkpath(temp.filePath(QStringLiteral("Trip/empty")));

    const qint64 up = transfers.push(QStringLiteral("emulator-5556"), temp.filePath(QStringLiteral("Trip")),
                                     QStringLiteral("/sdcard/Download"));
    REQUIRE(waitUntil([&] { return finished(transfers, up).has_value(); }));
    const auto pushed = transfers.job(up);
    CHECK(pushed->state == TransferState::Done);
    CHECK(pushed->total == 70003);
    CHECK(storage.node("/sdcard/Download/Trip/day1/a.jpg")->content.size() == 70000);
    CHECK(storage.node("/sdcard/Download/Trip/b.txt")->content == "bbb");
    CHECK(storage.node("/sdcard/Download/Trip/empty").value_or(FakeStorage::Node{}).directory);

    const qint64 down = transfers.pull(QStringLiteral("emulator-5556"), QStringLiteral("/sdcard/Download/Trip"),
                                       temp.filePath(QStringLiteral("copy")));
    REQUIRE(waitUntil([&] { return finished(transfers, down).has_value(); }));
    CHECK(transfers.job(down)->state == TransferState::Done);
    CHECK(readFile(temp.filePath(QStringLiteral("copy/Trip/day1/a.jpg"))) == pattern(70000));
    CHECK(readFile(temp.filePath(QStringLiteral("copy/Trip/b.txt"))) == "bbb");
    CHECK(QFileInfo(temp.filePath(QStringLiteral("copy/Trip/empty"))).isDir());
}

TEST_CASE("file transfers: a waiting transfer is cancelled at once and a running one stops")
{
    const FilesApplication application;
    FakeStorage storage;
    storage.addFile("/sdcard/big.bin", pattern(400000).toStdString());
    storage.addFile("/sdcard/next.bin", "next");
    storage.setStall(true);
    FakeAdbServer server([&storage](FakeConnection& c) { storage.serve(c); });
    FileTransfers transfers{AdbClient(server.port())};
    const QTemporaryDir temp;

    const qint64 running = transfers.pull(QStringLiteral("emulator-5556"), QStringLiteral("/sdcard/big.bin"), temp.path());
    const qint64 waiting = transfers.pull(QStringLiteral("emulator-5556"), QStringLiteral("/sdcard/next.bin"), temp.path());
    REQUIRE(waitUntil([&] { return transfers.job(running)->state == TransferState::Running; }));
    CHECK(transfers.job(waiting)->state == TransferState::Queued);

    CHECK(transfers.cancel(waiting));
    CHECK(transfers.job(waiting)->state == TransferState::Cancelled);
    CHECK(transfers.cancel(running));
    REQUIRE(waitUntil([&] { return finished(transfers, running).has_value(); }));
    CHECK(transfers.job(running)->state == TransferState::Cancelled);
    CHECK(transfers.job(running)->message.isEmpty());
    CHECK_FALSE(QFileInfo::exists(temp.filePath(QStringLiteral("big.bin"))));
    CHECK_FALSE(QFileInfo::exists(temp.filePath(QStringLiteral("big.bin.gs-part"))));
    CHECK_FALSE(QFileInfo::exists(temp.filePath(QStringLiteral("next.bin"))));
    CHECK_FALSE(transfers.cancel(running));
    CHECK_FALSE(transfers.cancel(12345));
}

TEST_CASE("file transfers: a missing file fails with a reason, and a request without a phone fails at once")
{
    const FilesApplication application;
    FakeStorage storage;
    FakeAdbServer server([&storage](FakeConnection& c) { storage.serve(c); });
    FileTransfers transfers{AdbClient(server.port())};
    const QTemporaryDir temp;

    const qint64 missing =
        transfers.pull(QStringLiteral("emulator-5556"), QStringLiteral("/sdcard/none.bin"), temp.path());
    REQUIRE(waitUntil([&] { return finished(transfers, missing).has_value(); }));
    CHECK(transfers.job(missing)->state == TransferState::Failed);
    CHECK_FALSE(transfers.job(missing)->message.isEmpty());

    const qint64 nowhere = transfers.push(QString(), temp.path(), QStringLiteral("/sdcard/Download"));
    CHECK(transfers.job(nowhere)->state == TransferState::Failed);
    CHECK_FALSE(transfers.job(nowhere)->message.isEmpty());

    transfers.clearFinished(QStringLiteral("emulator-5556"));
    CHECK(transfers.jobs(QStringLiteral("emulator-5556")).empty());
}

TEST_CASE("phone files: dragging out downloads to a temporary folder first, up to 200 MB and without folders")
{
    const FilesApplication application;
    FakeStorage storage;
    storage.addFolder("/sdcard/DCIM");
    storage.addFile("/sdcard/one.txt", "first");
    storage.addFile("/sdcard/two.txt", "second");
    storage.addFile("/sdcard/movie.mkv", "m");
    storage.setReportedSize("/sdcard/movie.mkv", 300ULL * 1024 * 1024);
    FakeAdbServer server([&storage](FakeConnection& c) { storage.serve(c); });
    FileTransfers transfers{AdbClient(server.port())};
    const QTemporaryDir temp;
    const bool online = true;
    PhoneFiles files(AdbClient(server.port()), transfers, onlinePhone(online), pathOf(temp) / L"drag");
    files.open(QStringLiteral("emulator-5556"));
    REQUIRE(waitUntil([&] { return !files.loading(); }));

    CHECK_FALSE(files.prepareDrag().isEmpty());
    files.select(rowOf(files.model(), QStringLiteral("movie.mkv")), 0);
    CHECK_FALSE(files.prepareDrag().isEmpty());
    files.select(rowOf(files.model(), QStringLiteral("DCIM")), 0);
    CHECK_FALSE(files.prepareDrag().isEmpty());
    CHECK(files.dragPhase() == PhoneFiles::kDragIdle);

    files.select(rowOf(files.model(), QStringLiteral("one.txt")), 0);
    files.select(rowOf(files.model(), QStringLiteral("two.txt")), Qt::ControlModifier);
    REQUIRE(files.prepareDrag().isEmpty());
    CHECK(files.dragPhase() == PhoneFiles::kDragPreparing);
    CHECK(files.dragCount() == 2);
    REQUIRE(waitUntil([&] { return files.dragPhase() == PhoneFiles::kDragReady; }));
    const QStringList urls = files.dragUrls();
    REQUIRE(urls.size() == 2);
    const QString first = QUrl(urls[0]).toLocalFile();
    CHECK(first.endsWith(QStringLiteral("/one.txt")));
    CHECK(QDir::cleanPath(first).startsWith(QDir::cleanPath(temp.filePath(QStringLiteral("drag")))));
    CHECK(readFile(first) == "first");
    CHECK(readFile(QUrl(urls[1]).toLocalFile()) == "second");
    CHECK(files.transferCount() == 2);

    files.finishDrag();
    CHECK(files.dragPhase() == PhoneFiles::kDragIdle);
    CHECK(files.dragUrls().isEmpty());
}

TEST_CASE("drops on the mirror: APKs are installed and the other local files and folders are pushed")
{
    const QTemporaryDir temp;
    writeFile(temp.filePath(QStringLiteral("game.apk")), "apk");
    writeFile(temp.filePath(QStringLiteral("SHOUT.APK")), "apk");
    writeFile(temp.filePath(QStringLiteral("notes.txt")), "txt");
    QDir().mkpath(temp.filePath(QStringLiteral("pack.apk")));
    QDir().mkpath(temp.filePath(QStringLiteral("Photos")));
    const gs::app::DropPlan plan = gs::app::planDrop(
        {QUrl::fromLocalFile(temp.filePath(QStringLiteral("game.apk"))),
         QUrl::fromLocalFile(temp.filePath(QStringLiteral("notes.txt"))),
         QUrl::fromLocalFile(temp.filePath(QStringLiteral("SHOUT.APK"))),
         QUrl::fromLocalFile(temp.filePath(QStringLiteral("pack.apk"))),
         QUrl::fromLocalFile(temp.filePath(QStringLiteral("Photos"))), QUrl(QStringLiteral("https://example.com/x.apk")),
         QUrl::fromLocalFile(temp.filePath(QStringLiteral("gone.apk")))});
    CHECK(plan.apks == QStringList{temp.filePath(QStringLiteral("game.apk")), temp.filePath(QStringLiteral("SHOUT.APK"))});
    CHECK(plan.files == QStringList{temp.filePath(QStringLiteral("notes.txt")), temp.filePath(QStringLiteral("pack.apk")),
                                    temp.filePath(QStringLiteral("Photos"))});
    CHECK(plan.skipped == 2);
}

TEST_CASE("drops on the mirror: one progress card while pushing, one result, and install progress by name")
{
    DropFeedback feedback;
    const DropNotice start = feedback.startPush({1, 2}, {QStringLiteral("a.txt"), QStringLiteral("b.txt")});
    CHECK(start.source == QStringLiteral("gs-files-push"));
    CHECK_FALSE(start.title.isEmpty());
    CHECK(start.progress == doctest::Approx(0));
    CHECK_FALSE(feedback.idle());

    const auto halfway = feedback.pushUpdated(pushJob(1, QStringLiteral("running"), 50, 100));
    REQUIRE(halfway.has_value());
    CHECK(halfway->progress == doctest::Approx(0.5));
    CHECK_FALSE(halfway->clear);
    CHECK_FALSE(feedback.pushUpdated(pushJob(7, QStringLiteral("done"), 1, 1)).has_value());
    const auto first = feedback.pushUpdated(pushJob(1, QStringLiteral("done"), 100, 100));
    REQUIRE(first.has_value());
    CHECK(first->toast.isEmpty());
    const auto last = feedback.pushUpdated(pushJob(2, QStringLiteral("done"), 10, 10));
    REQUIRE(last.has_value());
    CHECK(last->clear);
    CHECK(last->toast.contains(QStringLiteral("2")));
    CHECK(last->icon == QStringLiteral("check"));
    CHECK(feedback.idle());

    (void)feedback.startPush({3}, {QStringLiteral("c.txt")});
    const auto failed = feedback.pushUpdated(pushJob(3, QStringLiteral("failed"), 0, 5, QStringLiteral("no space")));
    REQUIRE(failed.has_value());
    CHECK(failed->toast.contains(QStringLiteral("c.txt")));
    CHECK(failed->toast.contains(QStringLiteral("no space")));
    CHECK(failed->icon == QStringLiteral("triangle-alert"));

    const DropNotice installing = feedback.startInstall(QStringLiteral("game.apk"));
    CHECK(installing.source == QStringLiteral("gs-files-install-game.apk"));
    CHECK(installing.title.contains(QStringLiteral("game.apk")));
    const auto copying = feedback.installUpdated(installJob(9, QStringLiteral("game.apk"), QStringLiteral("copying"), 25, 100));
    REQUIRE(copying.has_value());
    CHECK(copying->progress == doctest::Approx(0.25));
    CHECK_FALSE(feedback.installUpdated(installJob(10, QStringLiteral("other.apk"), QStringLiteral("done"))).has_value());
    const auto installed = feedback.installUpdated(installJob(9, QStringLiteral("game.apk"), QStringLiteral("done")));
    REQUIRE(installed.has_value());
    CHECK(installed->clear);
    CHECK(installed->toast.contains(QStringLiteral("game.apk")));
    CHECK(feedback.idle());
}
