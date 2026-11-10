#include "ControlChannel.h"
#include "Settings.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace gs::app::settings;

namespace {

template <typename Document>
bool reports(const Loaded<Document>& result, Problem problem, std::string_view path)
{
    return std::any_of(result.diagnostics.begin(), result.diagnostics.end(),
                       [&](const Diagnostic& d) { return d.problem == problem && d.path == path; });
}

}

TEST_CASE("unchanged settings are written as their format and version alone")
{
    CHECK(save(Settings{}) == "{\n  \"format\": \"gamescrcpy.settings\",\n  \"version\": 1\n}\n");
}

TEST_CASE("changed settings survive a save and a load, and only they are written")
{
    Settings settings;
    settings.appearance.theme = ThemeMode::Light;
    settings.appearance.accent = Accent::Teal;
    settings.hubWindow = {120, -40, 1300, 820, true};
    const std::string json = save(settings);
    CHECK(json.find("\"theme\": \"light\"") != std::string::npos);
    CHECK(json.find("reducedMotion") == std::string::npos);
    const Loaded<Settings> loaded = load<Settings>(json);
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    CHECK(loaded.document == settings);
}

TEST_CASE("a wrong value resets that setting alone and says where")
{
    const Loaded<Settings> loaded = load<Settings>(R"({"format": "gamescrcpy.settings", "version": 1,
        "appearance": {"theme": "purple", "accent": 3, "reducedMotion": true},
        "hubWindow": {"x": 10, "width": -5, "maximized": "yes"}})");
    CHECK(loaded.usable);
    CHECK(loaded.document.appearance.theme == ThemeMode::Dark);
    CHECK(loaded.document.appearance.accent == Accent::Ember);
    CHECK(loaded.document.appearance.reducedMotion == SystemToggle::On);
    CHECK(loaded.document.hubWindow.x == 10);
    CHECK(loaded.document.hubWindow.width == 0);
    CHECK_FALSE(loaded.document.hubWindow.maximized);
    CHECK(reports(loaded, Problem::UnknownName, "$.appearance.theme"));
    CHECK(reports(loaded, Problem::WrongType, "$.appearance.accent"));
    CHECK(reports(loaded, Problem::OutOfRange, "$.hubWindow.width"));
    CHECK(reports(loaded, Problem::WrongType, "$.hubWindow.maximized"));
}

TEST_CASE("unknown fields are ignored with a warning")
{
    const Loaded<Settings> loaded = load<Settings>(
        R"({"format": "gamescrcpy.settings", "version": 1, "future": 1, "appearance": {"glow": true}})");
    CHECK(loaded.usable);
    CHECK(reports(loaded, Problem::UnknownField, "$.future"));
    CHECK(reports(loaded, Problem::UnknownField, "$.appearance.glow"));
    CHECK(std::all_of(loaded.diagnostics.begin(), loaded.diagnostics.end(),
                      [](const Diagnostic& d) { return d.severity == Severity::Warning; }));
}

TEST_CASE("a file that cannot be read as a whole gives the defaults")
{
    const std::pair<std::string, Problem> cases[] = {
        {"", Problem::MalformedJson},
        {R"({"format": "gamescrcpy.settings", "version":)", Problem::MalformedJson},
        {"[]", Problem::WrongFormat},
        {R"({"version": 1, "appearance": {"theme": "light"}})", Problem::WrongFormat},
        {R"({"format": "gamescrcpy.devices", "version": 1, "appearance": {"theme": "light"}})", Problem::WrongFormat},
        {R"({"format": "gamescrcpy.settings", "version": 0, "appearance": {"theme": "light"}})", Problem::WrongVersion},
        {std::string(1024 * 1024 + 1, ' '), Problem::TooLarge},
        {std::string(65, '[') + std::string(65, ']'), Problem::TooLarge},
    };
    for (const auto& [json, problem] : cases) {
        CAPTURE(json.substr(0, 80));
        const Loaded<Settings> loaded = load<Settings>(json);
        CHECK_FALSE(loaded.usable);
        CHECK_FALSE(loaded.newer);
        CHECK(loaded.document == Settings{});
        CHECK(std::any_of(loaded.diagnostics.begin(), loaded.diagnostics.end(),
                          [&](const Diagnostic& d) { return d.problem == problem && d.severity == Severity::Error; }));
    }
}

TEST_CASE("a file from a newer GameScrcpy is recognised")
{
    const Loaded<Settings> loaded =
        load<Settings>(R"({"format": "gamescrcpy.settings", "version": 2, "appearance": {"theme": "light"}})");
    CHECK_FALSE(loaded.usable);
    CHECK(loaded.newer);
    CHECK(loaded.document == Settings{});
    CHECK(reports(loaded, Problem::WrongVersion, "$.version"));
}

TEST_CASE("devices keep their settings under their IDs, and only what differs from the defaults")
{
    Devices devices;
    devices.byId["R58M123ABC"].transport = Transport::WifiDirect;
    devices.byId["aid:ab3f31256ae3d44c"].transport = Transport::Auto;
    devices.byId["aid:ab3f31256ae3d44c"].profile = "shooter-2";
    devices.byId["aid:ab3f31256ae3d44c"].apps = {{"com.example.shooter", "shooter-2"}, {"com.example.launcher", ""}};
    devices.byId["0123456789ABCDEF"] = DeviceSettings{};
    const std::string json = save(devices);
    CHECK(json.find("0123456789ABCDEF") == std::string::npos);
    const Loaded<Devices> loaded = load<Devices>(json);
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    CHECK(loaded.document.byId.size() == 2);
    CHECK(loaded.document.byId.at("R58M123ABC").transport == Transport::WifiDirect);
    CHECK(loaded.document.byId.at("R58M123ABC").profile.empty());
    CHECK(loaded.document.byId.at("aid:ab3f31256ae3d44c").transport == Transport::Auto);
    CHECK(loaded.document.byId.at("aid:ab3f31256ae3d44c").profile == "shooter-2");
    CHECK(loaded.document.byId.at("aid:ab3f31256ae3d44c").apps ==
          std::map<std::string, std::string, std::less<>>{{"com.example.shooter", "shooter-2"},
                                                         {"com.example.launcher", ""}});
    CHECK(loaded.document.byId.at("R58M123ABC").apps.empty());
    CHECK(save(Devices{}) == "{\n  \"format\": \"gamescrcpy.devices\",\n  \"version\": 1\n}\n");
}

TEST_CASE("a device remembers the games whose start panel was closed, newest last and within a limit")
{
    DeviceSettings device;
    CHECK(rememberClosedStart(device, "com.example.shooter"));
    CHECK(rememberClosedStart(device, "com.example.racer"));
    CHECK_FALSE(rememberClosedStart(device, "com.example.shooter"));
    CHECK_FALSE(rememberClosedStart(device, ""));
    CHECK_FALSE(rememberClosedStart(device, std::string(DeviceSettings::kMaxPackageLength + 1, 'p')));
    CHECK(device.closedStarts == std::vector<std::string>{"com.example.shooter", "com.example.racer"});

    Devices devices;
    devices.byId["aid:ab3f31256ae3d44c"] = device;
    const Loaded<Devices> loaded = load<Devices>(save(devices));
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    CHECK(loaded.document.byId.at("aid:ab3f31256ae3d44c").closedStarts == device.closedStarts);

    for (std::size_t i = 0; i < DeviceSettings::kMaxClosedStarts; ++i) {
        CHECK(rememberClosedStart(device, "com.example.game" + std::to_string(i)));
    }
    CHECK(device.closedStarts.size() == DeviceSettings::kMaxClosedStarts);
    CHECK(device.closedStarts.front() == "com.example.game0");
    CHECK(device.closedStarts.back() ==
          "com.example.game" + std::to_string(DeviceSettings::kMaxClosedStarts - 1));
    CHECK(rememberClosedStart(device, "com.example.shooter"));
    CHECK(device.closedStarts.size() == DeviceSettings::kMaxClosedStarts);
    CHECK(device.closedStarts.front() == "com.example.game1");
}

TEST_CASE("a wrong closed start entry is left out and the others are kept")
{
    const Loaded<Devices> loaded = load<Devices>(R"({"format": "gamescrcpy.devices", "version": 1, "devices": {
        "R58M123ABC": {"closedStarts": ["com.example.shooter", 7, "", "com.example.racer"]},
        "HW-77AB": {"closedStarts": "com.example.shooter"}}})");
    CHECK(loaded.usable);
    CHECK(loaded.document.byId.at("R58M123ABC").closedStarts ==
          std::vector<std::string>{"com.example.shooter", "com.example.racer"});
    CHECK(reports(loaded, Problem::WrongType, "$.devices[\"R58M123ABC\"].closedStarts[1]"));
    CHECK(reports(loaded, Problem::OutOfRange, "$.devices[\"R58M123ABC\"].closedStarts[2]"));
    CHECK(loaded.document.byId.at("HW-77AB").closedStarts.empty());
    CHECK(reports(loaded, Problem::WrongType, "$.devices[\"HW-77AB\"].closedStarts"));
}

TEST_CASE("a wrong device entry is left out and its path names the device")
{
    const Loaded<Devices> loaded = load<Devices>(R"({"format": "gamescrcpy.devices", "version": 1, "devices": {
        "R58M123ABC": {"transport": "wifiDirect"},
        "HW-77AB": {"transport": "carrier pigeon"},
        "SM-1": "adbTunnel",
        "": {"transport": "auto"}}})");
    CHECK(loaded.usable);
    CHECK(loaded.document.byId.at("R58M123ABC").transport == Transport::WifiDirect);
    CHECK(loaded.document.byId.at("HW-77AB").transport == Transport::Default);
    CHECK_FALSE(loaded.document.byId.contains("SM-1"));
    CHECK_FALSE(loaded.document.byId.contains(""));
    CHECK(reports(loaded, Problem::UnknownName, R"($.devices["HW-77AB"].transport)"));
    CHECK(reports(loaded, Problem::WrongType, R"($.devices["SM-1"])"));
    CHECK(reports(loaded, Problem::OutOfRange, R"($.devices[""])"));
}

TEST_CASE("recent wireless connections survive a save and a load")
{
    Settings settings;
    settings.wireless.collapsed = true;
    rememberConnection(settings.wireless, "192.168.1.5:41234", 1'760'000'000);
    settings.wireless.recent.at("192.168.1.5:41234").name = "Pixel 7 · café";
    const std::string json = save(settings);
    const Loaded<Settings> loaded = load<Settings>(json);
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    CHECK(loaded.document == settings);

    const Loaded<Settings> wrong = load<Settings>(R"({"format": "gamescrcpy.settings", "version": 1,
        "wireless": {"recent": {"10.0.0.2:5555": {"name": 7, "lastUsed": 1760000000}}}})");
    CHECK(wrong.document.wireless.recent.at("10.0.0.2:5555").lastUsed == 1'760'000'000);
    CHECK(wrong.document.wireless.recent.at("10.0.0.2:5555").name.empty());
    CHECK(reports(wrong, Problem::WrongType, R"($.wireless.recent["10.0.0.2:5555"].name)"));
}

TEST_CASE("changed shortcuts survive a save and a load, an emptied one included, and bad entries are named")
{
    Settings settings;
    settings.shortcuts = {{"device.mute", "Ctrl+M"}, {"settings.search", ""}};
    const std::string json = save(settings);
    CHECK(json.find(R"("settings.search": "")") != std::string::npos);
    const Loaded<Settings> loaded = load<Settings>(json);
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    CHECK(loaded.document == settings);

    const std::string longId(Settings::kMaxCommandIdLength + 1, 'a');
    const std::string longKeys(Settings::kMaxKeysLength + 1, 'F');
    const Loaded<Settings> wrong = load<Settings>(R"({"format": "gamescrcpy.settings", "version": 1, "shortcuts": {
        "device.mute": "Ctrl+M", "device.home": 3, "device.back": ")" + longKeys + R"(", ")" + longId + R"(": "F1",
        "": "F2"}})");
    CHECK(wrong.usable);
    CHECK(wrong.document.shortcuts == std::map<std::string, std::string, std::less<>>{{"device.mute", "Ctrl+M"}});
    CHECK(reports(wrong, Problem::WrongType, R"($.shortcuts["device.home"])"));
    CHECK(reports(wrong, Problem::OutOfRange, R"($.shortcuts["device.back"])"));
    CHECK(reports(wrong, Problem::OutOfRange, R"($.shortcuts[")" + longId + R"("])"));
    CHECK(reports(wrong, Problem::OutOfRange, R"($.shortcuts[""])"));

    const Loaded<Settings> notAnObject =
        load<Settings>(R"({"format": "gamescrcpy.settings", "version": 1, "shortcuts": ["Ctrl+M"]})");
    CHECK(notAnObject.document.shortcuts.empty());
    CHECK(reports(notAnObject, Problem::WrongType, "$.shortcuts"));
}

TEST_CASE("the palette's recent commands survive a save and a load in order, and bad entries are named")
{
    Settings settings;
    settings.palette.recent = {"hub.settings", "app.about", "device.transport"};
    const Loaded<Settings> loaded = load<Settings>(save(settings));
    CHECK(loaded.diagnostics.empty());
    CHECK(loaded.document.palette.recent == settings.palette.recent);
    CHECK(save(Settings{}).find("palette") == std::string::npos);

    const std::string longId(kMaxCommandIdLength + 1, 'a');
    const Loaded<Settings> wrong = load<Settings>(
        R"({"format": "gamescrcpy.settings", "version": 1, "palette": {"recent": ["a.b", 3, "", ")" + longId +
        R"(", "c.d", "e.f", "g.h", "i.j", "k.l", "m.n", "o.p", "q.r"]}})");
    CHECK(wrong.usable);
    CHECK(wrong.document.palette.recent ==
          std::vector<std::string>{"a.b", "c.d", "e.f", "g.h", "i.j", "k.l", "m.n", "o.p"});
    CHECK(reports(wrong, Problem::WrongType, "$.palette.recent[1]"));
    CHECK(reports(wrong, Problem::OutOfRange, "$.palette.recent[2]"));
    CHECK(reports(wrong, Problem::OutOfRange, "$.palette.recent[3]"));
    CHECK(reports(wrong, Problem::OutOfRange, "$.palette.recent[11]"));

    const Loaded<Settings> notAList =
        load<Settings>(R"({"format": "gamescrcpy.settings", "version": 1, "palette": {"recent": "a.b"}})");
    CHECK(notAList.document.palette.recent.empty());
    CHECK(reports(notAList, Problem::WrongType, "$.palette.recent"));
}

TEST_CASE("extension states and per-device extension values survive a save and a load, and bad values are named")
{
    Settings settings;
    settings.extensions["alice.glow"] = {.enabled = true,
                                         .version = "1.2.0",
                                         .granted = {"notify"},
                                         .values = {{"strength", 0.25},
                                                    {"steps", std::int64_t{3}},
                                                    {"mode", std::string("soft")},
                                                    {"on", true}}};
    settings.extensions["bob.empty"] = {};
    const std::string json = save(settings);
    CHECK(json.find("bob.empty") == std::string::npos);
    const Loaded<Settings> loaded = load<Settings>(json);
    CHECK(loaded.diagnostics.empty());
    REQUIRE(loaded.document.extensions.size() == 1);
    CHECK(loaded.document.extensions.at("alice.glow") == settings.extensions.at("alice.glow"));

    Devices devices;
    devices.byId["phone"].extensions["alice.glow"].values = {{"strength", 0.75}};
    const Loaded<Devices> loadedDevices = load<Devices>(save(devices));
    CHECK(loadedDevices.diagnostics.empty());
    CHECK(loadedDevices.document == devices);

    const Loaded<Settings> wrong = load<Settings>(R"({"format": "gamescrcpy.settings", "version": 1, "extensions": {
        "alice.glow": {"enabled": true, "values": {"ok": 1.5, "list": [1], "big": 18446744073709551615, "": 1}}}})");
    CHECK(wrong.usable);
    CHECK(wrong.document.extensions.at("alice.glow").enabled);
    CHECK(wrong.document.extensions.at("alice.glow").values == Scalars{{"ok", 1.5}});
    CHECK(reports(wrong, Problem::WrongType, R"($.extensions["alice.glow"].values["list"])"));
    CHECK(reports(wrong, Problem::OutOfRange, R"($.extensions["alice.glow"].values["big"])"));
    CHECK(reports(wrong, Problem::OutOfRange, R"($.extensions["alice.glow"].values[""])"));
}

TEST_CASE("an extension's add-ons for every profile are kept with their parameters (ADR-0073 revision 2026-11-08)")
{
    Settings settings;
    settings.extensions["alice.feel"] = {.enabled = true,
                                         .version = "1.0.0",
                                         .granted = {},
                                         .values = {},
                                         .addOns = {{"randomLanding", {.on = true, .params = R"({"radius":2.5})"}},
                                                    {"smoothing", {.on = false, .params = {}}}}};
    const std::string json = save(settings);
    CHECK(json.find("randomLanding") != std::string::npos);
    CHECK(json.find("smoothing") == std::string::npos);
    const Loaded<Settings> loaded = load<Settings>(json);
    CHECK(loaded.diagnostics.empty());
    const ExtensionState& state = loaded.document.extensions.at("alice.feel");
    REQUIRE(state.addOns.size() == 1);
    CHECK(state.addOns.at("randomLanding").on);
    CHECK(state.addOns.at("randomLanding").params == R"({"radius":2.5})");

    const Loaded<Settings> wrong = load<Settings>(R"({"format": "gamescrcpy.settings", "version": 1, "extensions": {
        "alice.feel": {"enabled": true, "addOns": {"smoothing": {"on": "yes"}, "": {"on": true}}}}})");
    CHECK(wrong.usable);
    CHECK(wrong.document.extensions.at("alice.feel").enabled);
    CHECK_FALSE(wrong.document.extensions.at("alice.feel").addOns.at("smoothing").on);
    CHECK(reports(wrong, Problem::WrongType, R"($.extensions["alice.feel"].addOns["smoothing"].on)"));
    CHECK(reports(wrong, Problem::OutOfRange, R"($.extensions["alice.feel"].addOns[""])"));
}

TEST_CASE("the settings that ADR-0036 removed are not declared (UI-018)")
{
    constexpr std::array removed{"autorefresh",   "reverseconnect", "renderexpired",  "desktopopengl", "keepalive",
                                 "simplemode",    "record",         "maxtouchpoints", "adbpath",       "serverpath",
                                 "codecoptions",  "frameless",      "showtoolbar",    "windowtitle",   "traymessage",
                                 "maxfpsuser",    "aoa",            "channel",        "tutorial",      "guide"};
    for (const Entry& entry : catalog()) {
        std::string path = entry.path;
        std::ranges::transform(path, path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (const std::string_view name : removed) {
            CHECK_MESSAGE(path.find(name) == std::string::npos, entry.path);
        }
    }
}

TEST_CASE("remembering a connection keeps its name and only the latest eight")
{
    Wireless wireless;
    for (int i = 0; i < 9; ++i) {
        rememberConnection(wireless, "10.0.0." + std::to_string(i) + ":5555", 1000 + i);
    }
    CHECK(wireless.recent.size() == Wireless::kMaxRecent);
    CHECK_FALSE(wireless.recent.contains("10.0.0.0:5555"));

    wireless.recent.at("10.0.0.1:5555").name = "Pad Air";
    rememberConnection(wireless, "10.0.0.1:5555", 2000);
    CHECK(wireless.recent.at("10.0.0.1:5555").lastUsed == 2000);
    CHECK(wireless.recent.at("10.0.0.1:5555").name == "Pad Air");
    rememberConnection(wireless, "10.0.0.9:5555", 2001);
    CHECK(wireless.recent.contains("10.0.0.1:5555"));
    CHECK_FALSE(wireless.recent.contains("10.0.0.2:5555"));

    rememberConnection(wireless, "", 3000);
    rememberConnection(wireless, std::string(Wireless::kMaxAddressLength + 1, '1'), 3000);
    CHECK(wireless.recent.size() == Wireless::kMaxRecent);
}

TEST_CASE("the catalog lists the settings the settings page shows, with their facets and defaults")
{
    const Entry* theme = findSetting("appearance.theme");
    REQUIRE(theme != nullptr);
    CHECK(theme->kind == Kind::Choice);
    CHECK(theme->page == Page::Appearance);
    CHECK(theme->facets.scope == Scope::Global);
    CHECK(theme->facets.live);
    CHECK(theme->choices == std::vector<std::string_view>{"dark", "light", "system"});
    CHECK(theme->defaultValue == Value(std::string("dark")));

    const Entry* maxSize = findSetting("video.maxSize");
    REQUIRE(maxSize != nullptr);
    CHECK(maxSize->kind == Kind::Integer);
    CHECK(maxSize->page == Page::Video);
    CHECK(maxSize->facets.scope == Scope::Device);
    CHECK_FALSE(maxSize->facets.live);
    CHECK(maxSize->min == 0);
    CHECK(maxSize->max == 8192);
    CHECK(maxSize->defaultValue == Value(std::int64_t{0}));

    const Entry* language = findSetting("general.language");
    REQUIRE(language != nullptr);
    CHECK(language->page == Page::Appearance);
    CHECK(language->facets.scope == Scope::Global);
    CHECK(language->facets.live);
    CHECK(language->choices == std::vector<std::string_view>{"system", "zh_CN", "en"});
    CHECK(language->defaultValue == Value(std::string("system")));

    const Entry* tray = findSetting("tray.closeToTray");
    REQUIRE(tray != nullptr);
    CHECK(tray->kind == Kind::Boolean);
    CHECK(tray->page == Page::General);
    CHECK(tray->facets.scope == Scope::Global);
    CHECK(tray->facets.live);
    CHECK(tray->defaultValue == Value(true));

    const Entry* capsule = findSetting("display.statusCapsule");
    REQUIRE(capsule != nullptr);
    CHECK(capsule->kind == Kind::Choice);
    CHECK(capsule->page == Page::Video);
    CHECK(capsule->facets.scope == Scope::Global);
    CHECK(capsule->facets.live);
    CHECK(capsule->choices == std::vector<std::string_view>{"auto", "always", "hidden"});
    CHECK(capsule->defaultValue == Value(std::string("auto")));
    CHECK(findSetting("display.showStatus") == nullptr);

    const Entry* toolbar = findSetting("display.toolbar");
    REQUIRE(toolbar != nullptr);
    CHECK(toolbar->kind == Kind::Choice);
    CHECK(toolbar->page == Page::Video);
    CHECK(toolbar->facets.scope == Scope::Global);
    CHECK(toolbar->facets.live);
    CHECK(toolbar->choices == std::vector<std::string_view>{"dwell", "pinned", "shortcut"});
    CHECK(toolbar->defaultValue == Value(std::string("pinned")));

    CHECK(findSetting("hubWindow.x") == nullptr);
    CHECK(findSetting("wireless.collapsed") == nullptr);
    CHECK(findSetting("hints.mirrorToolbar") == nullptr);
    CHECK(findSetting("video") == nullptr);
    CHECK(findSetting("nonsense") == nullptr);
}

TEST_CASE("every shown setting takes what its declaration allows, through the file, and nothing else (UI-016)")
{
    REQUIRE_FALSE(catalog().empty());
    for (const Entry& entry : catalog()) {
        CAPTURE(entry.path);
        CHECK_FALSE(entry.facets.title.empty());
        CHECK_FALSE(entry.facets.description.empty());
        CHECK(entry.page != Page::None);
        CHECK(valueOf(Settings{}, entry.path) == entry.defaultValue);

        std::vector<Value> allowed;
        std::vector<Value> refused;
        switch (entry.kind) {
        case Kind::Choice:
            for (const std::string_view name : entry.choices) {
                allowed.emplace_back(std::string(name));
            }
            refused = {std::string("no such name"), std::int64_t{1}};
            break;
        case Kind::Integer:
            allowed = {entry.min, entry.max};
            refused = {entry.min - 1, entry.max + 1, std::string("8")};
            break;
        case Kind::Boolean:
            allowed = {true, false};
            refused = {std::int64_t{1}};
            break;
        case Kind::Text:
            allowed = {std::string(), std::string(entry.maxLength, 'x')};
            refused = {std::string(entry.maxLength + 1, 'x'), false};
            break;
        }
        for (const Value& value : allowed) {
            Settings settings;
            REQUIRE(assign(settings, entry.path, value));
            CHECK(valueOf(settings, entry.path) == value);
            const Loaded<Settings> loaded = load<Settings>(save(settings));
            CHECK(loaded.diagnostics.empty());
            CHECK(valueOf(loaded.document, entry.path) == value);
        }
        for (const Value& value : refused) {
            Settings settings;
            CHECK_FALSE(assign(settings, entry.path, value));
            CHECK(settings == Settings{});
        }
    }
    Settings settings;
    CHECK_FALSE(assign(settings, "hubWindow.x", std::int64_t{5}));
}

TEST_CASE("a device's own values survive the file; others are dropped and their place is named")
{
    Devices devices;
    auto& overrides = devices.byId["R58M123ABC"].overrides;
    overrides["video.maxSize"] = std::int64_t{1920};
    overrides["video.codec"] = std::string("h265");
    overrides["audio.enabled"] = false;
    const Loaded<Devices> loaded = load<Devices>(save(devices));
    CHECK(loaded.diagnostics.empty());
    CHECK(loaded.document == devices);

    const Loaded<Devices> wrong = load<Devices>(R"({"format": "gamescrcpy.devices", "version": 1, "devices": {"X": {
        "overrides": {"video.maxSize": 1280, "appearance.theme": "light", "nonsense": 1, "video.maxFps": "fast",
                      "video.bitrateKbps": 1, "video.codec": "vp9"}}}})");
    CHECK(wrong.usable);
    const auto& kept = wrong.document.byId.at("X").overrides;
    CHECK(kept.size() == 1);
    CHECK(kept.at("video.maxSize") == Value(std::int64_t{1280}));
    CHECK(reports(wrong, Problem::UnknownField, R"($.devices["X"].overrides["appearance.theme"])"));
    CHECK(reports(wrong, Problem::UnknownField, R"($.devices["X"].overrides["nonsense"])"));
    CHECK(reports(wrong, Problem::WrongType, R"($.devices["X"].overrides["video.maxFps"])"));
    CHECK(reports(wrong, Problem::OutOfRange, R"($.devices["X"].overrides["video.bitrateKbps"])"));
    CHECK(reports(wrong, Problem::UnknownName, R"($.devices["X"].overrides["video.codec"])"));
}

TEST_CASE("the settings for a device are the global ones with its own values in their place")
{
    Settings global;
    global.video.maxFps = 30;
    global.video.bitrateKbps = 4000;
    DeviceSettings device;
    device.overrides["video.bitrateKbps"] = std::int64_t{12000};
    const Settings effective = effectiveSettings(global, device);
    CHECK(effective.video.maxFps == 30);
    CHECK(effective.video.bitrateKbps == 12000);
    CHECK(effectiveSettings(global, DeviceSettings{}) == global);
}

TEST_CASE("a device takes its own value only for a per-device setting in range, and gives it back")
{
    DeviceSettings device;
    CHECK(setOverride(device, "video.maxFps", Value(std::int64_t{90})));
    CHECK(setOverride(device, "video.codec", Value(std::string("h265"))));
    CHECK(device.overrides.at("video.maxFps") == Value(std::int64_t{90}));
    CHECK_FALSE(setOverride(device, "appearance.theme", Value(std::string("light"))));
    CHECK_FALSE(setOverride(device, "video.maxFps", Value(std::int64_t{999})));
    CHECK_FALSE(setOverride(device, "video.maxFps", Value(true)));
    CHECK_FALSE(setOverride(device, "video.codec", Value(std::string("vp9"))));
    CHECK_FALSE(setOverride(device, "nonsense", Value(true)));
    CHECK(device.overrides.size() == 2);
    CHECK(effectiveSettings(Settings{}, device).video.maxFps == 90);

    CHECK(clearOverride(device, "video.maxFps"));
    CHECK_FALSE(clearOverride(device, "video.maxFps"));
    CHECK(clearOverride(device, "video.codec"));
    CHECK(device == DeviceSettings{});
}

TEST_CASE("the touch method is normal by default, set on the input page and per phone, and the test option wins (HID-005)")
{
    const Entry* method = findSetting("touch.method");
    REQUIRE(method != nullptr);
    CHECK(method->page == Page::Input);
    CHECK(method->kind == Kind::Choice);
    CHECK(method->facets.scope == Scope::Device);
    CHECK(method->choices == std::vector<std::string_view>{"inject", "uhid", "aoa", "none"});
    CHECK(method->defaultValue == Value(std::string("inject")));
    CHECK(Settings{}.touch.method == TouchMethod::Inject);
    CHECK(save(Settings{}).find("\"touch\"") == std::string::npos);

    DeviceSettings device;
    CHECK(setOverride(device, "touch.method", Value(std::string("uhid"))));
    CHECK_FALSE(setOverride(device, "touch.method", Value(std::string("esp32"))));
    CHECK(effectiveSettings(Settings{}, device).touch.method == TouchMethod::Uhid);
    CHECK(effectiveSettings(Settings{}, DeviceSettings{}).touch.method == TouchMethod::Inject);

    using gs::app::TouchBackend;
    CHECK(gs::app::touchBackendOf(TouchBackend::Injection, std::nullopt) == TouchBackend::Injection);
    CHECK(gs::app::touchBackendOf(TouchBackend::Uhid, std::nullopt) == TouchBackend::Uhid);
    CHECK(gs::app::touchBackendOf(TouchBackend::Aoa, std::nullopt) == TouchBackend::Aoa);
    CHECK(gs::app::touchBackendOf(TouchBackend::Uhid, TouchBackend::Injection) == TouchBackend::Injection);
    CHECK(gs::app::touchBackendOf(TouchBackend::Injection, TouchBackend::Uhid) == TouchBackend::Uhid);
    CHECK(gs::app::touchBackendOf(TouchBackend::Injection, TouchBackend::Aoa) == TouchBackend::Aoa);
}

TEST_CASE("AOA is the third touch method, chosen for every phone or for one, and survives the files (HID-005, "
          "HID-006)")
{
    Settings settings;
    REQUIRE(assign(settings, "touch.method", Value(std::string("aoa"))));
    CHECK(settings.touch.method == TouchMethod::Aoa);
    const std::string json = save(settings);
    CHECK(json.find("\"method\": \"aoa\"") != std::string::npos);
    const Loaded<Settings> loaded = load<Settings>(json);
    CHECK(loaded.diagnostics.empty());
    CHECK(loaded.document.touch.method == TouchMethod::Aoa);

    DeviceSettings device;
    REQUIRE(setOverride(device, "touch.method", Value(std::string("aoa"))));
    CHECK(effectiveSettings(Settings{}, device).touch.method == TouchMethod::Aoa);
    Settings uhid;
    REQUIRE(assign(uhid, "touch.method", Value(std::string("uhid"))));
    CHECK(effectiveSettings(uhid, device).touch.method == TouchMethod::Aoa);
    CHECK(effectiveSettings(settings, DeviceSettings{}).touch.method == TouchMethod::Aoa);
    CHECK(clearOverride(device, "touch.method"));
    CHECK(effectiveSettings(uhid, device).touch.method == TouchMethod::Uhid);
}

TEST_CASE("ADR-0108: no touch is the fourth touch method, chosen for every phone or for one, and survives the files")
{
    const Entry* method = findSetting("touch.method");
    REQUIRE(method != nullptr);
    CHECK(method->facets.description.find("None only shows the picture") != std::string_view::npos);

    Settings settings;
    REQUIRE(assign(settings, "touch.method", Value(std::string("none"))));
    CHECK(settings.touch.method == TouchMethod::None);
    const std::string json = save(settings);
    CHECK(json.find("\"method\": \"none\"") != std::string::npos);
    const Loaded<Settings> loaded = load<Settings>(json);
    CHECK(loaded.diagnostics.empty());
    CHECK(loaded.document.touch.method == TouchMethod::None);

    DeviceSettings device;
    REQUIRE(setOverride(device, "touch.method", Value(std::string("none"))));
    CHECK(effectiveSettings(Settings{}, device).touch.method == TouchMethod::None);
    CHECK(gs::app::touchBackendOf(gs::app::TouchBackend::None, std::nullopt) == gs::app::TouchBackend::None);
    CHECK(gs::app::touchBackendOf(gs::app::TouchBackend::None, gs::app::TouchBackend::Uhid) ==
          gs::app::TouchBackend::Uhid);
}

TEST_CASE("ADR-0108: the picture source is phone mirroring unless changed, set on the input page and per phone, and "
          "survives the files")
{
    const Entry* source = findSetting("picture.source");
    REQUIRE(source != nullptr);
    CHECK(source->page == Page::Input);
    CHECK(source->kind == Kind::Choice);
    CHECK(source->facets.scope == Scope::Device);
    CHECK(source->choices == std::vector<std::string_view>{"phone", "capture", "none"});
    CHECK(source->defaultValue == Value(std::string("phone")));
    CHECK(Settings{}.picture.source == PictureSource::Phone);
    CHECK(save(Settings{}).find("\"picture\"") == std::string::npos);

    Settings settings;
    REQUIRE(assign(settings, "picture.source", Value(std::string("capture"))));
    CHECK_FALSE(assign(settings, "picture.source", Value(std::string("hdmi"))));
    CHECK(settings.picture.source == PictureSource::Capture);
    const std::string json = save(settings);
    CHECK(json.find("\"picture\": {\n    \"source\": \"capture\"\n  }") != std::string::npos);
    const Loaded<Settings> loaded = load<Settings>(json);
    CHECK(loaded.diagnostics.empty());
    CHECK(loaded.document.picture.source == PictureSource::Capture);

    DeviceSettings device;
    REQUIRE(setOverride(device, "picture.source", Value(std::string("none"))));
    CHECK_FALSE(setOverride(device, "picture.source", Value(std::int64_t{1})));
    CHECK(effectiveSettings(Settings{}, device).picture.source == PictureSource::None);
    CHECK(effectiveSettings(settings, DeviceSettings{}).picture.source == PictureSource::Capture);
    Devices devices;
    devices.byId["R58M123ABC"] = device;
    const Loaded<Devices> kept = load<Devices>(save(devices));
    CHECK(kept.diagnostics.empty());
    CHECK(effectiveSettings(settings, kept.document.byId.at("R58M123ABC")).picture.source == PictureSource::None);
    CHECK(clearOverride(device, "picture.source"));
    CHECK(effectiveSettings(settings, device).picture.source == PictureSource::Capture);
}

TEST_CASE("ADR-0108: a capture card remembers whether it shows a phone, is used on its own or is not decided yet")
{
    CHECK(CaptureSettings{}.phone.empty());
    DeviceSettings shows;
    shows.capture.phone = "R58M123ABC";
    DeviceSettings alone;
    alone.capture.phone = "standalone";
    Devices devices;
    devices.byId["capture-1a2b"] = shows;
    devices.byId["capture-3c4d"] = alone;
    devices.byId["capture-5e6f"] = DeviceSettings{};
    const std::string json = save(devices);
    CHECK(json.find("\"phone\": \"R58M123ABC\"") != std::string::npos);
    CHECK(json.find("\"phone\": \"standalone\"") != std::string::npos);
    const Loaded<Devices> loaded = load<Devices>(json);
    CHECK(loaded.diagnostics.empty());
    CHECK(loaded.document.byId.at("capture-1a2b").capture.phone == "R58M123ABC");
    CHECK(loaded.document.byId.at("capture-3c4d").capture.phone == "standalone");
    CHECK_FALSE(loaded.document.byId.contains("capture-5e6f"));
    CHECK(findSetting("capture.phone") == nullptr);

    const Loaded<Devices> tooLong =
        load<Devices>(std::string(R"({"format": "gamescrcpy.devices", "version": 1,)") +
                      R"( "devices": {"capture-1": {"capture": {"phone": ")" +
                      std::string(CaptureSettings::kMaxPhoneLength + 1, 'x') + R"("}}}})");
    CHECK(tooLong.usable);
    REQUIRE(tooLong.document.byId.contains("capture-1"));
    CHECK(tooLong.document.byId.at("capture-1").capture.phone.empty());
    CHECK(reports(tooLong, Problem::OutOfRange, R"($.devices["capture-1"].capture.phone)"));
}

TEST_CASE("ADR-0108: files written before picture sources read as they did")
{
    const Loaded<Settings> settings = load<Settings>(R"({"format": "gamescrcpy.settings", "version": 1,)"
                                                     R"( "touch": {"method": "uhid"}, "capture": {"folder": "D:/V"}})");
    CHECK(settings.usable);
    CHECK(settings.diagnostics.empty());
    CHECK(settings.document.touch.method == TouchMethod::Uhid);
    CHECK(settings.document.capture.folder == "D:/V");
    CHECK(settings.document.picture.source == PictureSource::Phone);

    const Loaded<Devices> devices = load<Devices>(
        R"({"format": "gamescrcpy.devices", "version": 1, "devices": {)"
        R"( "capture-1a2b": {"capture": {"touch": "aoa", "aoaPhone": "R58M", "sound": false,)"
        R"( "landscape": "right"}}, "R58M123ABC": {"overrides": {"touch.method": "aoa"}}}})");
    CHECK(devices.usable);
    CHECK(devices.diagnostics.empty());
    const DeviceSettings& card = devices.document.byId.at("capture-1a2b");
    CHECK(card.capture.phone.empty());
    CHECK(card.capture.touch == "aoa");
    CHECK(card.capture.aoaPhone == "R58M");
    CHECK_FALSE(card.capture.sound);
    CHECK(card.capture.landscape == CaptureLandscape::Right);
    const Settings phone = effectiveSettings(Settings{}, devices.document.byId.at("R58M123ABC"));
    CHECK(phone.touch.method == TouchMethod::Aoa);
    CHECK(phone.picture.source == PictureSource::Phone);
}

TEST_CASE("the ESP32 settings sit on the connection page, can be set for one phone, and survive the files (ADR-0105, "
          "HID-008)")
{
    const Entry* port = findSetting("esp32.port");
    const Entry* link = findSetting("esp32.link");
    const Entry* address = findSetting("esp32.address");
    REQUIRE(port != nullptr);
    REQUIRE(link != nullptr);
    REQUIRE(address != nullptr);
    for (const Entry* entry : {port, link, address}) {
        CHECK(entry->page == Page::Connection);
        CHECK(entry->facets.scope == Scope::Device);
    }
    CHECK(port->kind == Kind::Text);
    CHECK(port->defaultValue == Value(std::string("auto")));
    CHECK(link->kind == Kind::Choice);
    CHECK(link->choices == std::vector<std::string_view>{"serial", "wifi"});
    CHECK(link->defaultValue == Value(std::string("serial")));
    CHECK(address->kind == Kind::Text);
    CHECK(address->defaultValue == Value(std::string()));

    DeviceSettings device;
    CHECK(setOverride(device, "esp32.link", Value(std::string("wifi"))));
    CHECK(setOverride(device, "esp32.address", Value(std::string("192.168.1.20:4545"))));
    CHECK_FALSE(setOverride(device, "esp32.link", Value(std::string("bluetooth"))));
    const Settings mine = effectiveSettings(Settings{}, device);
    CHECK(mine.esp32.link == Esp32Connection::Wifi);
    CHECK(mine.esp32.address == "192.168.1.20:4545");
    CHECK(mine.esp32.port == "auto");
    CHECK(effectiveSettings(Settings{}, DeviceSettings{}).esp32.link == Esp32Connection::Serial);

    Settings settings;
    REQUIRE(assign(settings, "esp32.port", Value(std::string("COM7"))));
    const Loaded<Settings> loaded = load<Settings>(save(settings));
    CHECK(loaded.diagnostics.empty());
    CHECK(loaded.document.esp32.port == "COM7");
    CHECK(clearOverride(device, "esp32.link"));
    CHECK(effectiveSettings(settings, device).esp32.link == Esp32Connection::Serial);
    CHECK(effectiveSettings(settings, device).esp32.port == "COM7");
}

TEST_CASE("a phone remembers how its control window was held and its screen size, and only when they differ "
          "(HID-006)")
{
    DeviceSettings device;
    CHECK(device.controlOrientation == ControlOrientation::Profile);
    device.controlOrientation = ControlOrientation::LandscapeRight;
    device.screenWidth = 1080;
    device.screenHeight = 2400;
    Devices devices;
    devices.byId["R58M123ABC"] = device;
    const std::string json = save(devices);
    CHECK(json.find("\"controlOrientation\": \"landscapeRight\"") != std::string::npos);
    CHECK(json.find("\"screenWidth\": 1080") != std::string::npos);
    const Loaded<Devices> loaded = load<Devices>(json);
    CHECK(loaded.diagnostics.empty());
    CHECK(loaded.document.byId.at("R58M123ABC") == device);
    CHECK(save(Devices{}).find("controlOrientation") == std::string::npos);

    const Loaded<Devices> wrong = load<Devices>(R"({"format": "gamescrcpy.devices", "version": 1, "devices": {
        "X": {"controlOrientation": "upsideDown", "screenWidth": 99999}}})");
    REQUIRE(wrong.document.byId.contains("X"));
    CHECK(wrong.document.byId.at("X").controlOrientation == ControlOrientation::Profile);
    CHECK(wrong.document.byId.at("X").screenWidth == 0);
    CHECK_FALSE(wrong.diagnostics.empty());
}

TEST_CASE("the status capsule is automatic until changed, which survives a save and a load (UI-007)")
{
    CHECK(Settings{}.display.statusCapsule == StatusCapsule::Auto);
    CHECK(save(Settings{}).find("statusCapsule") == std::string::npos);

    Settings settings;
    REQUIRE(assign(settings, "display.statusCapsule", Value(std::string("hidden"))));
    const std::string json = save(settings);
    CHECK(json.find("\"statusCapsule\": \"hidden\"") != std::string::npos);
    const Loaded<Settings> loaded = load<Settings>(json);
    CHECK(loaded.diagnostics.empty());
    CHECK(loaded.document.display.statusCapsule == StatusCapsule::Hidden);
    CHECK(valueOf(loaded.document, "display.statusCapsule") == Value(std::string("hidden")));
    CHECK(loaded.document == settings);

    Settings unchanged;
    CHECK_FALSE(assign(unchanged, "display.statusCapsule", Value(std::string("sometimes"))));
    CHECK(unchanged == Settings{});

    DeviceSettings device;
    CHECK_FALSE(setOverride(device, "display.statusCapsule", Value(std::string("always"))));
}

TEST_CASE("reduce motion and reduce transparency follow Windows unless set, and old switches carry over")
{
    CHECK(Settings{}.appearance.reducedMotion == SystemToggle::System);
    CHECK(Settings{}.appearance.reducedTransparency == SystemToggle::System);
    const Entry* motion = findSetting("appearance.reducedMotion");
    REQUIRE(motion != nullptr);
    CHECK(motion->kind == Kind::Choice);
    CHECK(motion->choices == std::vector<std::string_view>{"system", "on", "off"});
    CHECK(motion->defaultValue == Value(std::string("system")));

    const Loaded<Settings> old = load<Settings>(R"({"format": "gamescrcpy.settings", "version": 1,)"
                                                R"( "appearance": {"reducedMotion": true, "reducedTransparency": false}})");
    CHECK(old.usable);
    CHECK(old.diagnostics.empty());
    CHECK(old.document.appearance.reducedMotion == SystemToggle::On);
    CHECK(old.document.appearance.reducedTransparency == SystemToggle::System);

    const Loaded<Settings> current = load<Settings>(R"({"format": "gamescrcpy.settings", "version": 1,)"
                                                    R"( "appearance": {"reducedTransparency": "off"}})");
    CHECK(current.diagnostics.empty());
    CHECK(current.document.appearance.reducedTransparency == SystemToggle::Off);
    CHECK(save(current.document).find("\"reducedTransparency\": \"off\"") != std::string::npos);
}

TEST_CASE("the key hints show in full for a set time, three seconds unless changed")
{
    CHECK(Settings{}.keymap.hintIntroMs == 3000);
    const Entry* duration = findSetting("keymap.hintIntroMs");
    REQUIRE(duration != nullptr);
    CHECK(duration->page == Page::Input);
    CHECK(duration->min == 500);
    CHECK(duration->max == 10'000);
    Settings settings;
    CHECK(assign(settings, "keymap.hintIntroMs", Value(std::int64_t{4500})));
    CHECK(settings.keymap.hintIntroMs == 4500);
    CHECK_FALSE(assign(settings, "keymap.hintIntroMs", Value(std::int64_t{100})));
}

TEST_CASE("the retired mouse button settings are dropped without a complaint (ADR-0070, 2026-10-28 revision)")
{
    const Loaded<Settings> loaded = load<Settings>(
        R"({"format": "gamescrcpy.settings", "version": 1, "keymap": {"hintOpacity": 40, "rightButton": "back",)"
        R"( "middleButton": "home", "backButton": "none", "forwardButton": "appSwitch"}})");
    CHECK(loaded.usable);
    CHECK(loaded.diagnostics.empty());
    CHECK(loaded.document.keymap.hintOpacity == 40);
    const std::string saved = save(loaded.document);
    CHECK(saved.find("Button") == std::string::npos);
    CHECK(findSetting("keymap.rightButton") == nullptr);
}

TEST_CASE("a status capsule turned off by the old switch stays hidden, and the old switch is dropped (UI-007)")
{
    const Loaded<Settings> off = load<Settings>(
        R"({"format": "gamescrcpy.settings", "version": 1, "display": {"showStatus": false}})");
    CHECK(off.usable);
    CHECK(off.diagnostics.empty());
    CHECK(off.document.display.statusCapsule == StatusCapsule::Hidden);
    CHECK(save(off.document).find("showStatus") == std::string::npos);

    const Loaded<Settings> on = load<Settings>(
        R"({"format": "gamescrcpy.settings", "version": 1, "display": {"showStatus": true, "sharpness": 20}})");
    CHECK(on.diagnostics.empty());
    CHECK(on.document.display.statusCapsule == StatusCapsule::Auto);
    CHECK(on.document.display.sharpness == 20);

    const Loaded<Settings> both = load<Settings>(
        R"({"format": "gamescrcpy.settings", "version": 1,)"
        R"( "display": {"showStatus": false, "statusCapsule": "always"}})");
    CHECK(both.diagnostics.empty());
    CHECK(both.document.display.statusCapsule == StatusCapsule::Always);
}

TEST_CASE("a hint once shown is remembered through the file and is no setting (UI-007)")
{
    CHECK_FALSE(Settings{}.hints.mirrorToolbar);
    CHECK(save(Settings{}).find("hints") == std::string::npos);

    Settings settings;
    settings.hints.mirrorToolbar = true;
    const Loaded<Settings> loaded = load<Settings>(save(settings));
    CHECK(loaded.diagnostics.empty());
    CHECK(loaded.document.hints.mirrorToolbar);
    CHECK(loaded.document == settings);

    CHECK_FALSE(valueOf(settings, "hints.mirrorToolbar").has_value());
    Settings untouched;
    CHECK_FALSE(assign(untouched, "hints.mirrorToolbar", Value(true)));
    CHECK(untouched == Settings{});

    const Loaded<Settings> wrong = load<Settings>(
        R"({"format": "gamescrcpy.settings", "version": 1, "hints": {"mirrorToolbar": "yes"}})");
    CHECK(wrong.usable);
    CHECK_FALSE(wrong.document.hints.mirrorToolbar);
    CHECK(reports(wrong, Problem::WrongType, "$.hints.mirrorToolbar"));
}

TEST_CASE("ADR-0110: the guided tours already finished or skipped are kept in the settings file as a plain list")
{
    CHECK(Settings{}.hints.tours.empty());
    Settings settings;
    settings.hints.tours = {"devices", "keymap"};
    const Loaded<Settings> loaded = load<Settings>(save(settings));
    CHECK(loaded.diagnostics.empty());
    CHECK(loaded.document.hints.tours == std::vector<std::string>{"devices", "keymap"});
    CHECK(loaded.document == settings);
    CHECK_FALSE(valueOf(settings, "hints.tours").has_value());

    const Loaded<Settings> wrong = load<Settings>(
        R"({"format": "gamescrcpy.settings", "version": 1, "hints": {"tours": ["devices", 3]}})");
    CHECK(wrong.usable);
    CHECK(wrong.document.hints.tours == std::vector<std::string>{"devices"});
    CHECK(reports(wrong, Problem::WrongType, "$.hints.tours[1]"));

    Settings many;
    for (int index = 0; index <= static_cast<int>(Hints::kMaxTours); ++index) {
        many.hints.tours.push_back("tour" + std::to_string(index));
    }
    const Loaded<Settings> capped = load<Settings>(save(many));
    CHECK(capped.document.hints.tours.size() == Hints::kMaxTours);
    CHECK(reports(capped, Problem::OutOfRange, "$.hints.tours[" + std::to_string(Hints::kMaxTours) + "]"));
}

TEST_CASE("bytes that are no UTF-8 do not stop the settings from being written")
{
    Settings settings;
    rememberConnection(settings.wireless, "10.0.0.2:5555", 1000);
    settings.wireless.recent.at("10.0.0.2:5555").name = "bad \xff name";
    const std::string json = save(settings);
    CHECK(json.find("10.0.0.2:5555") != std::string::npos);
    CHECK(load<Settings>(json).usable);
}
