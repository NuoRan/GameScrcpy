#include "AppSettings.h"
#include "CrashReport.h"
#include "DiagnosticsBundle.h"

#include <doctest/doctest.h>

#include <miniz.h>

#include <QCoreApplication>
#include <QDate>
#include <QEventLoop>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using gs::app::AppSettings;
using gs::app::CrashReport;
using gs::app::DiagnosticsBundle;
using namespace gs::app::diagnostics;

namespace {

constexpr std::string_view kCrash = "crash-20261030-105812-345";

class TemporaryDirectory
{
public:
    TemporaryDirectory()
        : m_path(std::filesystem::temp_directory_path() /
                 ("gs-diagnostics-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                  "-" + std::to_string(s_next++)))
    {
        std::filesystem::create_directories(m_path);
    }
    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return m_path; }

private:
    static inline std::atomic<int> s_next = 0;
    std::filesystem::path m_path;
};

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

void write(const std::filesystem::path& file, std::string_view text)
{
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << text;
}

std::string utf16(std::string_view ascii)
{
    std::string bytes;
    for (const char c : ascii) {
        bytes.push_back(c);
        bytes.push_back('\0');
    }
    return bytes;
}

bool contains(const std::vector<std::string>& values, std::string_view value)
{
    return std::ranges::find(values, value) != values.end();
}

const std::string kLog =
    "2026-10-30 18:58:12.345 info     GameScrcpy 2.0.0 started\n"
    "2026-10-30 18:58:12.400 warning  profile C:\\Users\\Alice\\Documents\\fps.json: cannot be read\n"
    "2026-10-30 18:58:13.000 warning  video of R58M12345AB: the decoder stopped\n"
    "2026-10-30 18:58:13.100 warning  audio of 192.168.31.47:5555: no output device\n"
    "2026-10-30 18:58:14.000 warning  the clipboard of 300000 bytes is cut to 262000 for ZY2234ABCD\n"
    "2026-10-30 18:58:14.500 info     copied my secret note to the phone\n"
    "2026-10-30 18:58:15.000 info     [session] [5f3a9c] WiFi direct failed (fe80::1c2b:3a4d:5e6f:7a8b%12 refused); "
    "using the adb tunnel\n"
    "2026-10-30 18:58:16.000 info     paired adb-1A2B3C4D5E6F-AbCdEf._adb-tls-connect._tcp on SSID \"Home Net\"\n"
    "2026-10-30 18:58:17.000 info     camera locked for Pixel 7\n"
    "2026-10-30 18:58:18.000 info     share \\\\DESKTOP-K2J4H5\\games at aa:bb:cc:dd:ee:ff\n";

Secrets secretsForTests()
{
    return Secrets{.serials = {"R58M12345AB"},
                   .clipboard = {"my secret note"},
                   .userName = "Alice",
                   .computerName = "DESKTOP-K2J4H5"};
}

std::vector<std::string_view> leaks()
{
    return {"R58M12345AB", "ZY2234ABCD",     "192.168.31.47", "fe80::1c2b", "1A2B3C4D5E6F",
            "Alice",       "my secret note", "Home Net",      "DESKTOP-K2J4H5", "aa:bb:cc:dd:ee:ff"};
}

std::vector<std::pair<std::string, std::string>> unzipped(const std::string& archive)
{
    std::vector<std::pair<std::string, std::string>> files;
    mz_zip_archive reader{};
    REQUIRE(mz_zip_reader_init_mem(&reader, archive.data(), archive.size(), 0) != 0);
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&reader); ++i) {
        std::array<char, 512> name{};
        mz_zip_reader_get_filename(&reader, i, name.data(), static_cast<mz_uint>(name.size()));
        std::size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&reader, i, &size, 0);
        REQUIRE(data != nullptr);
        files.emplace_back(name.data(), std::string(static_cast<const char*>(data), size));
        mz_free(data);
    }
    mz_zip_reader_end(&reader);
    return files;
}

}

TEST_CASE("serial numbers are found in the app's own log lines, device names are not taken for them (REL-009)")
{
    const std::vector<std::string> found = serialsIn(
        "video of R58M12345AB: the decoder stopped\n"
        "audio of 192.168.31.47:5555: no output device\n"
        "the clipboard of 300000 bytes is cut to 262000 for ZY2234ABCD\n"
        "measuring the real latency of emulator-5554 with touch dots\n"
        "mirror overlay for HT7A1JT00123 not created: no engine\n"
        "camera locked for Pixel 7\n"
        "camera released for SM-G9910\n"
        "foreground app on Pixel: com.example.game\n"
        "filters on 0123ABCD take 9.10 ms a frame\n"
        "start panel closed for com.example.game2 on 9A8B7C6D5E\n"
        "start panel closed for com.example.game3 on Galaxy S21\n"
        "paired adb-1A2B3C4D5E6F-AbCdEf._adb-tls-connect._tcp\n"
        "device aid:ab3f31256ae3d44c is ready\n");
    for (const std::string_view serial : {"R58M12345AB", "192.168.31.47:5555", "ZY2234ABCD", "emulator-5554",
                                          "HT7A1JT00123", "SM-G9910", "0123ABCD", "9A8B7C6D5E",
                                          "adb-1A2B3C4D5E6F-AbCdEf", "1A2B3C4D5E6F", "aid:ab3f31256ae3d44c"}) {
        CAPTURE(serial);
        CHECK(contains(found, serial));
    }
    CHECK_FALSE(contains(found, "Pixel"));
    CHECK_FALSE(contains(found, "Galaxy"));
    CHECK_FALSE(contains(found, "com.example.game2"));
    CHECK_FALSE(contains(found, "tls-connect"));
}

TEST_CASE("known serial numbers become numbered placeholders in any case and inside file names (REL-009)")
{
    const std::string text = "video of R58M12345AB: stopped\n"
                             "r58m12345ab again\n"
                             "{\"devices\": {\"R58M12345AB\": {}}}\n"
                             "thumbs/R58M12345AB.png\n"
                             "adb-R58M12345AB-AbCdEf._adb-tls-connect._tcp\n"
                             "unknown aid:ab3f31256ae3d44c and adb-9Z8Y7X6W-QwErTy._adb-tls-pairing._tcp\n"
                             "adb-server and _adb-tls-connect stay\n";
    CHECK(scrubSerials(text, {"R58M12345AB", "adb-R58M12345AB-AbCdEf", "r58m12345ab", "ab"}) ==
          "video of <serial-1>: stopped\n"
          "<serial-1> again\n"
          "{\"devices\": {\"<serial-1>\": {}}}\n"
          "thumbs/<serial-1>.png\n"
          "<serial-2>._adb-tls-connect._tcp\n"
          "unknown <serial> and <serial>._adb-tls-pairing._tcp\n"
          "adb-server and _adb-tls-connect stay\n");
}

TEST_CASE("addresses with their ports and hardware addresses go, timestamps, versions and code locations stay "
          "(REL-009)")
{
    CHECK(scrubAddresses("connecting to 192.168.31.47:5555 now") == "connecting to <ip>:<port> now");
    CHECK(scrubAddresses("peer 10.0.0.2, gateway 10.0.0.1.") == "peer <ip>, gateway <ip>.");
    CHECK(scrubAddresses("link fe80::1c2b:3a4d:5e6f:7a8b%12 up") == "link <ip> up");
    CHECK(scrubAddresses("server [2001:db8::8a2e:370:7334]:37000 ready") == "server [<ip>]:<port> ready");
    CHECK(scrubAddresses("loopback ::1 and ::ffff:192.168.1.5") == "loopback <ip> and <ip>");
    CHECK(scrubAddresses("full 2001:0db8:85a3:0000:0000:8a2e:0370:7334") == "full <ip>");
    CHECK(scrubAddresses("wifi aa:bb:cc:dd:ee:ff or AA-BB-CC-DD-EE-FF") == "wifi <mac> or <mac>");
    CHECK(scrubAddresses("adb connect phone.lan:5555 and desktop-1.home:37099") ==
          "adb connect <host>:<port> and <host>:<port>");
    const std::string kept = "2026-10-30 18:58:12.345 info Qt 6.10.1, MSVC 19.44.35207, Windows 10.0.26100\n"
                             "qrc:/qt/qml/GameScrcpy/App/qml/Main.qml:120:5: warning\n"
                             "Main.qml:12 and fps.json:3 and http://example.com/x\n"
                             "QObject::connect: no such signal std::string::size\n"
                             "localhost:5037 at 10:58:12, hash deadbeef:cafe and 3f2a\n"
                             "2026-10-30, 1.2.3, v1.2.3.4, 256.1.1.1 and 1.2.3.4.5\n";
    CHECK(scrubAddresses(kept) == kept);
}

TEST_CASE("user names in paths become <user> in every spelling of a path (REL-009)")
{
    CHECK(scrubUserPaths("profile C:\\Users\\Alice\\Documents\\fps.json: cannot be read", "Alice") ==
          "profile C:\\Users\\<user>\\Documents\\fps.json: cannot be read");
    CHECK(scrubUserPaths("C:/Users/John Smith/AppData/Roaming/GameScrcpy", "") ==
          "C:/Users/<user>/AppData/Roaming/GameScrcpy");
    CHECK(scrubUserPaths("{\"folder\": \"C:\\\\Users\\\\alice\\\\Videos\"}", "Alice") ==
          "{\"folder\": \"C:\\\\Users\\\\<user>\\\\Videos\"}");
    CHECK(scrubUserPaths("file:///C:/Users/Alice/clip.media", "") == "file:///C:/Users/<user>/clip.media");
    CHECK(scrubUserPaths("D:\\Profiles\\Alice\\GameScrcpy and Alice's phone", "Alice") ==
          "D:\\Profiles\\<user>\\GameScrcpy and Alice's phone");
    CHECK(scrubUserPaths("the users of C:\\Users\\", "") == "the users of C:\\Users\\");
}

TEST_CASE("Wi-Fi names and the Wi-Fi QR payload are taken out (REL-009)")
{
    CHECK(scrubWifiNames("connected to SSID \"Home Net 5G\", signal -51 dBm") ==
          "connected to SSID \"<wifi>\", signal -51 dBm");
    CHECK(scrubWifiNames("{\"ssid\": \"CafeWiFi\", \"band\": 5}") == "{\"ssid\": \"<wifi>\", \"band\": 5}");
    CHECK(scrubWifiNames("ssid=OfficeNet, channel 6") == "ssid=<wifi>, channel 6");
    CHECK(scrubWifiNames("WIFI:T:ADB;S:gamescrcpy-k3j4;P:secret123;;") == "WIFI:T:ADB;S:<wifi>;P:<password>;;");
    CHECK(scrubWifiNames("the SSID is hidden") == "the SSID is hidden");
}

TEST_CASE("clipboard text is taken out, the sizes the app logs about it stay (REL-009)")
{
    const std::vector<std::string> clipboard{"my secret note\n  second line here  ", "short"};
    CHECK(scrubClipboard("copied my secret note to the phone\n"
                         "second line here\n"
                         "the clipboard of 300000 bytes is cut to 262000 for <serial-1>\n"
                         "clipboard from the phone: \"hello there\" and \u201Cmore\u201D\n"
                         "short words stay \"quoted\" outside clipboard lines? no: this line says clipboard\n"
                         "short words stay \"quoted\"\n",
                         clipboard) ==
          "copied <clipboard> to the phone\n"
          "<clipboard>\n"
          "the clipboard of 300000 bytes is cut to 262000 for <serial-1>\n"
          "clipboard from the phone: \"<clipboard>\" and \u201C<clipboard>\u201D\n"
          "short words stay \"<clipboard>\" outside clipboard lines? no: this line says clipboard\n"
          "short words stay \"quoted\"\n");
}

TEST_CASE("a realistic log keeps no serial, address, user name, Wi-Fi name or clipboard text (REL-009)")
{
    Secrets secrets = secretsForTests();
    const std::vector<std::string> found = serialsIn(kLog);
    secrets.serials.insert(secrets.serials.end(), found.begin(), found.end());
    const std::string scrubbed = scrub(kLog, secrets);
    for (const std::string_view leak : leaks()) {
        CAPTURE(leak);
        CHECK(scrubbed.find(leak) == std::string::npos);
    }
    CHECK(scrubbed.find("C:\\Users\\<user>\\Documents\\fps.json") != std::string::npos);
    CHECK(scrubbed.find("video of <serial-1>: the decoder stopped") != std::string::npos);
    CHECK(scrubbed.find("audio of <serial-") != std::string::npos);
    CHECK(scrubbed.find("cut to 262000 for <serial-") != std::string::npos);
    CHECK(scrubbed.find("WiFi direct failed (<ip> refused)") != std::string::npos);
    CHECK(scrubbed.find("._adb-tls-connect._tcp on SSID \"<wifi>\"") != std::string::npos);
    CHECK(scrubbed.find("camera locked for Pixel 7") != std::string::npos);
    CHECK(scrubbed.find("\\\\<computer>\\games at <mac>") != std::string::npos);
    CHECK(scrubbed.find("2026-10-30 18:58:12.345 info     GameScrcpy 2.0.0 started") != std::string::npos);
}

TEST_CASE("a crash dump keeps its size while serials, user names, clipboard text and addresses in it are masked "
          "(REL-009)")
{
    std::string dump("MDMP\x93\xa7\0\0", 8);
    dump += "....R58M12345AB....";
    dump += utf16("C:\\Users\\Alice\\Downloads\\GameScrcpy\\GameScrcpy.exe");
    dump += std::string("\0\0 192.168.31.47 \0", 18);
    dump += utf16("note: my secret note");
    dump += "DESKTOP-K2J4H5";
    const std::string masked = mask(dump, secretsForTests());
    CHECK(masked.size() == dump.size());
    CHECK(masked.starts_with(std::string_view("MDMP\x93\xa7\0\0", 8)));
    CHECK(masked.find("R58M12345AB") == std::string::npos);
    CHECK(masked.find(utf16("Alice")) == std::string::npos);
    CHECK(masked.find("192.168.31.47") == std::string::npos);
    CHECK(masked.find(utf16("my secret note")) == std::string::npos);
    CHECK(masked.find("DESKTOP-K2J4H5") == std::string::npos);
    CHECK(masked.find(utf16("\\Downloads\\GameScrcpy\\GameScrcpy.exe")) != std::string::npos);
    CHECK(masked.find(utf16("C:\\Users\\")) != std::string::npos);
}

TEST_CASE("the bundle holds the system, the devices, the settings, every log and the newest crash, all scrubbed "
          "(REL-009)")
{
    const TemporaryDirectory directory;
    const std::filesystem::path data = directory.path();
    const std::filesystem::path crash = data / "crashes" / std::string(kCrash);
    write(data / "logs" / "gamescrcpy.log", kLog);
    write(data / "logs" / "gamescrcpy.1.log", "2026-10-29 10:00:00.000 warning  video of R58M12345AB: older\n");
    write(data / "logs" / "notes.txt", "not a log\n");
    write(data / "settings.json",
          "{\n  \"wireless\": {\"recent\": {\"192.168.31.47:5555\": {\"name\": \"Pixel 7\"}}},\n"
          "  \"hints\": {\"crashNotice\": \"crash-20261029-080000-000\"}\n}\n");
    write(data / "devices.json", "{\n  \"devices\": {\"ZY2234ABCD\": {\"transport\": \"wifiDirect\"}}\n}\n");
    std::string dump("MDMP\0\0\0\0R58M12345AB", 19);
    dump += utf16("C:\\Users\\Alice\\GameScrcpy.exe");
    write(crash / (std::string(kCrash) + ".dmp"), dump);
    write(crash / "gamescrcpy.log", "2026-10-30 10:58:11.000 info     video of R58M12345AB: last words\n");
    write(crash / "crash.txt", "version 2.0.0 (abc)\nexception 0xC0000005 access violation writing 0x0\n");

    const Sources sources{.system = "GameScrcpy 2.0.0\ndata folder: C:\\Users\\Alice\\AppData\\Roaming\\GameScrcpy\n",
                          .devices = "Pixel 7\n  Android 15 / USB / R58M12345AB\n",
                          .settings = {data / "settings.json", data / "devices.json", data / "missing.json"},
                          .logs = data / "logs",
                          .crash = crash};
    const std::vector<Entry> found = entries(sources, secretsForTests());

    std::vector<std::string> names;
    for (const Entry& entry : found) {
        names.push_back(entry.name);
    }
    const std::string folder = "crash/" + std::string(kCrash) + "/";
    CHECK(names == std::vector<std::string>{"system.txt",
                                            "devices.txt",
                                            "settings/settings.json",
                                            "settings/devices.json",
                                            "logs/gamescrcpy.1.log",
                                            "logs/gamescrcpy.log",
                                            folder + std::string(kCrash) + ".dmp",
                                            folder + "crash.txt",
                                            folder + "gamescrcpy.log"});
    for (const Entry& entry : found) {
        CAPTURE(entry.name);
        for (const std::string_view leak : leaks()) {
            CAPTURE(leak);
            CHECK(entry.data.find(leak) == std::string::npos);
            CHECK(entry.data.find(utf16(leak)) == std::string::npos);
        }
    }
    CHECK(found[0].data.find("C:\\Users\\<user>\\AppData") != std::string::npos);
    CHECK(found[2].data.find("\"<serial-") != std::string::npos);
    CHECK(found[3].data.find("\"<serial-") != std::string::npos);
    CHECK(found[6].binary);
    CHECK(found[6].data.size() == dump.size());
    CHECK(found[7].data.find("access violation") != std::string::npos);

    const std::optional<std::string> archive = zip(found);
    REQUIRE(archive.has_value());
    const auto files = unzipped(*archive);
    REQUIRE(files.size() == found.size());
    for (std::size_t i = 0; i < files.size(); ++i) {
        CHECK(files[i].first == found[i].name);
        CHECK(files[i].second == found[i].data);
    }
}

TEST_CASE("the device list names each device with its details, state and latest metrics (REL-009)")
{
    QStandardItemModel model;
    model.setItemRoleNames({{Qt::UserRole + 1, "serial"},
                            {Qt::UserRole + 2, "name"},
                            {Qt::UserRole + 3, "facts"},
                            {Qt::UserRole + 4, "deviceState"},
                            {Qt::UserRole + 5, "status"},
                            {Qt::UserRole + 6, "problem"},
                            {Qt::UserRole + 7, "stats"},
                            {Qt::UserRole + 8, "deviceId"}});
    CHECK(describeDevices(model) == "No device is listed.\n");
    CHECK(serialsOf(model).empty());

    auto* phone = new QStandardItem;
    phone->setData(QStringLiteral("R58M12345AB"), Qt::UserRole + 1);
    phone->setData(QStringLiteral("Pixel 7"), Qt::UserRole + 2);
    phone->setData(QVariantMap{{QStringLiteral("android"), QStringLiteral("15")},
                               {QStringLiteral("attachment"), QStringLiteral("usb")},
                               {QStringLiteral("link"), QStringLiteral("adb-tunnel")}},
                   Qt::UserRole + 3);
    phone->setData(QStringLiteral("Online"), Qt::UserRole + 4);
    phone->setData(QStringLiteral("Mirroring (adb tunnel)"), Qt::UserRole + 5);
    phone->setData(QVariantMap{{QStringLiteral("fps"), 59.94},
                               {QStringLiteral("megabits"), 7.81},
                               {QStringLiteral("latencyMs"), 41.6}},
                   Qt::UserRole + 7);
    phone->setData(QStringLiteral("aid:ab3f31256ae3d44c"), Qt::UserRole + 8);
    model.appendRow(phone);
    auto* idle = new QStandardItem;
    idle->setData(QStringLiteral("192.168.31.47:5555"), Qt::UserRole + 1);
    idle->setData(QStringLiteral("Galaxy S21"), Qt::UserRole + 2);
    idle->setData(QStringLiteral("Waiting for authorization"), Qt::UserRole + 4);
    idle->setData(QStringLiteral("Allow USB debugging on the phone"), Qt::UserRole + 6);
    model.appendRow(idle);

    CHECK(describeDevices(model) == "Pixel 7\n"
                                    "  android: 15\n"
                                    "  attachment: usb\n"
                                    "  link: adb-tunnel\n"
                                    "  state: Online\n"
                                    "  status: Mirroring (adb tunnel)\n"
                                    "  latest: 59.9 fps, 7.8 Mb/s, 42 ms from input to picture\n"
                                    "Galaxy S21\n"
                                    "  state: Waiting for authorization\n"
                                    "  problem: Allow USB debugging on the phone\n");
    CHECK(serialsOf(model) ==
          std::vector<std::string>{"R58M12345AB", "192.168.31.47:5555", "aid:ab3f31256ae3d44c"});
}

TEST_CASE("an export is saved where asked as a zip of scrubbed files, and a path that is no local file is refused "
          "(REL-009)")
{
    const Application application;
    const TemporaryDirectory directory;
    const std::filesystem::path data = directory.path();
    write(data / "logs" / "gamescrcpy.log", kLog);
    std::filesystem::create_directories(data / "crashes" / std::string(kCrash));
    write(data / "crashes" / std::string(kCrash) / "crash.txt", "video of R58M12345AB: crashed\n");

    QStandardItemModel model;
    model.setItemRoleNames({{Qt::UserRole + 1, "serial"}, {Qt::UserRole + 2, "name"}});
    auto* phone = new QStandardItem;
    phone->setData(QStringLiteral("HT7A1JT00123"), Qt::UserRole + 1);
    phone->setData(QStringLiteral("Pixel 7"), Qt::UserRole + 2);
    model.appendRow(phone);

    AppSettings settings(data);
    settings.rememberConnection(QStringLiteral("phone.lan:5555"));
    gs::app::settings::DeviceSettings device;
    device.transport = gs::app::settings::Transport::WifiDirect;
    settings.setDevice("SERIAL0042XY", device);
    const CrashReport crashes(settings, data / "crashes");
    DiagnosticsBundle bundle(settings, data);
    bundle.setDevices(&model);
    bundle.setCrashReport(&crashes);
    bundle.setGraphics(QStringLiteral("NVIDIA GeForce RTX 3060"));

    const Secrets secrets = bundle.secrets();
    for (const std::string_view serial : {"HT7A1JT00123", "SERIAL0042XY", "phone.lan:5555"}) {
        CAPTURE(serial);
        CHECK(contains(secrets.serials, serial));
    }
    const Sources sources = bundle.sources();
    CHECK(sources.crash == data / "crashes" / std::string(kCrash));
    CHECK(sources.devices == "Pixel 7\n");
    CHECK(sources.system.find("graphics: NVIDIA GeForce RTX 3060") != std::string::npos);
    CHECK(bundle.suggestedName() ==
          QStringLiteral("GameScrcpy-diagnostics-%1.zip").arg(QDate::currentDate().toString(Qt::ISODate)));

    std::vector<QVariantMap> results;
    QObject::connect(&bundle, &DiagnosticsBundle::exportFinished, &bundle,
                     [&results](const QVariantMap& result) { results.push_back(result); });
    CHECK_FALSE(bundle.exportTo(QStringLiteral("relative.zip")));
    REQUIRE(results.size() == 1);
    CHECK_FALSE(results[0].value(QStringLiteral("ok")).toBool());

    const std::filesystem::path target = data / "out" / "diagnostics.zip";
    std::filesystem::create_directories(target.parent_path());
    QEventLoop loop;
    QObject::connect(&bundle, &DiagnosticsBundle::exportFinished, &loop, &QEventLoop::quit);
    QTimer::singleShot(std::chrono::seconds(60), &loop, &QEventLoop::quit);
    REQUIRE(bundle.exportTo(QUrl::fromLocalFile(QString::fromStdWString(target.wstring())).toString()));
    CHECK(bundle.exporting());
    loop.exec();
    REQUIRE(results.size() == 2);
    CHECK(results[1].value(QStringLiteral("ok")).toBool());
    CHECK(results[1].value(QStringLiteral("name")).toString() == QStringLiteral("diagnostics.zip"));
    CHECK_FALSE(bundle.exporting());

    std::ifstream in(target, std::ios::binary);
    const std::string archive{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    const auto files = unzipped(archive);
    std::vector<std::string> names;
    for (const auto& [name, contents] : files) {
        names.push_back(name);
        CAPTURE(name);
        for (const std::string_view leak : {"HT7A1JT00123", "SERIAL0042XY", "phone.lan", "R58M12345AB"}) {
            CAPTURE(leak);
            CHECK(contents.find(leak) == std::string::npos);
        }
    }
    CHECK(contains(names, "system.txt"));
    CHECK(contains(names, "devices.txt"));
    CHECK(contains(names, "logs/gamescrcpy.log"));
    CHECK(contains(names, "settings/settings.json"));
    CHECK(contains(names, "crash/" + std::string(kCrash) + "/crash.txt"));
}
