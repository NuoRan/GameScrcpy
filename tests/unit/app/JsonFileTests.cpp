#include "JsonFile.h"

#include <doctest/doctest.h>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

using namespace std::chrono_literals;
using namespace gs::app::settings;
using SettingsFile = gs::app::JsonFile<Settings>;
using DevicesFile = gs::app::JsonFile<Devices>;

namespace {

class TemporaryDirectory
{
public:
    TemporaryDirectory()
        : m_path(std::filesystem::temp_directory_path() /
                 ("gs-settings-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                  std::to_string(s_next++)))
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

    [[nodiscard]] std::filesystem::path file(const char* name) const { return m_path / name; }

private:
    static inline std::atomic<int> s_next = 0;
    std::filesystem::path m_path;
};

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void writeFile(const std::filesystem::path& path, const std::string& contents)
{
    std::ofstream(path, std::ios::binary) << contents;
}

Settings withTheme(Settings settings, ThemeMode theme)
{
    settings.appearance.theme = theme;
    return settings;
}

}

TEST_CASE("without a file the settings are the defaults, and nothing is written until they change")
{
    const TemporaryDirectory directory;
    const auto path = directory.file("settings.json");
    {
        SettingsFile file(path, 20ms);
        CHECK(file.document() == Settings{});
        CHECK(file.loadDiagnostics().empty());
        file.update(file.document());
        file.flush();
        CHECK(file.writes() == 0);
    }
    CHECK_FALSE(std::filesystem::exists(path));
}

TEST_CASE("a burst of changes is written once")
{
    const TemporaryDirectory directory;
    const auto path = directory.file("settings.json");
    SettingsFile file(path, 300ms);
    for (int x = 0; x < 20; ++x) {
        Settings settings = file.document();
        settings.hubWindow.x = x;
        file.update(settings);
    }
    CHECK(file.writes() == 0);
    file.flush();
    CHECK(file.writes() == 1);
    CHECK(load<Settings>(readFile(path)).document.hubWindow.x == 19);
}

TEST_CASE("changes are written once they settle, without a flush")
{
    const TemporaryDirectory directory;
    const auto path = directory.file("settings.json");
    SettingsFile file(path, 20ms);
    file.update(withTheme(file.document(), ThemeMode::Light));
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (file.writes() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(5ms);
    }
    CHECK(file.writes() == 1);
    CHECK(load<Settings>(readFile(path)).document.appearance.theme == ThemeMode::Light);
}

TEST_CASE("pending changes are written when the file is closed")
{
    const TemporaryDirectory directory;
    const auto path = directory.file("settings.json");
    {
        SettingsFile file(path, std::chrono::hours(1));
        file.update(withTheme(file.document(), ThemeMode::System));
    }
    CHECK(load<Settings>(readFile(path)).document.appearance.theme == ThemeMode::System);
}

TEST_CASE("saved settings are read back, into a directory made for them")
{
    const TemporaryDirectory directory;
    const auto path = directory.file("data") / "settings.json";
    Settings saved;
    saved.appearance.accent = Accent::Rose;
    saved.hubWindow = {40, 30, 1200, 800, true};
    {
        SettingsFile file(path, 20ms);
        file.update(saved);
    }
    const SettingsFile reopened(path, 20ms);
    CHECK(reopened.document() == saved);
    CHECK(reopened.loadDiagnostics().empty());
}

TEST_CASE("an unreadable file is set aside and the defaults are used")
{
    const TemporaryDirectory directory;
    const auto path = directory.file("settings.json");
    const auto aside = directory.file("settings.json.bad");
    writeFile(path, "{ not json");
    SettingsFile file(path, 20ms);
    CHECK(file.document() == Settings{});
    CHECK_FALSE(file.loadDiagnostics().empty());
    CHECK_FALSE(std::filesystem::exists(path));
    CHECK(readFile(aside) == "{ not json");
    file.update(withTheme(file.document(), ThemeMode::Light));
    file.flush();
    CHECK(load<Settings>(readFile(path)).document.appearance.theme == ThemeMode::Light);
    CHECK(readFile(aside) == "{ not json");
}

TEST_CASE("a file from a newer GameScrcpy is left alone")
{
    const TemporaryDirectory directory;
    const auto path = directory.file("settings.json");
    const std::string newer = R"({"format": "gamescrcpy.settings", "version": 2})";
    writeFile(path, newer);
    {
        SettingsFile file(path, 20ms);
        CHECK(file.readOnly());
        file.update(withTheme(file.document(), ThemeMode::Light));
        CHECK(file.document().appearance.theme == ThemeMode::Light);
        file.flush();
        CHECK(file.writes() == 0);
    }
    CHECK(readFile(path) == newer);
    CHECK_FALSE(std::filesystem::exists(directory.file("settings.json.bad")));
}

TEST_CASE("device settings go to devices.json the same way")
{
    const TemporaryDirectory directory;
    const auto path = directory.file("devices.json");
    {
        DevicesFile file(path, 20ms);
        Devices devices = file.document();
        devices.byId["aid:ab3f31256ae3d44c"].transport = Transport::WifiDirect;
        file.update(devices);
    }
    const DevicesFile reopened(path, 20ms);
    CHECK(reopened.document().byId.at("aid:ab3f31256ae3d44c").transport == Transport::WifiDirect);
}

TEST_CASE("a save that another program blocks is reported once and kept, then written when the file closes")
{
    const TemporaryDirectory directory;
    const auto path = directory.file("settings.json");
    {
        SettingsFile first(path, 20ms);
        first.update(withTheme(first.document(), ThemeMode::Light));
        first.flush();
        REQUIRE(first.writes() == 1);
    }
    std::atomic<int> busy = 0;
    {
        SettingsFile file(path, 20ms, [&] { ++busy; });
        HANDLE reader = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        REQUIRE(reader != INVALID_HANDLE_VALUE);
        file.update(withTheme(file.document(), ThemeMode::Dark));
        file.flush();
        CHECK(busy == 1);
        CHECK(file.writes() == 0);
        ::CloseHandle(reader);
        CHECK(load<Settings>(readFile(path)).document.appearance.theme == ThemeMode::Light);
    }
    CHECK(busy == 1);
    CHECK(load<Settings>(readFile(path)).document.appearance.theme == ThemeMode::Dark);
}
