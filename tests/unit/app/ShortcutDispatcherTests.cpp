#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "Commands.h"
#include "DeviceCommands.h"
#include "ShortcutDispatcher.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStringList>
#include <QTest>
#include <QVariant>

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

using gs::app::Commands;
using gs::app::DeviceFacts;
using gs::app::ShortcutDispatcher;

namespace {

class FakeHost final : public gs::app::DeviceCommandHost
{
public:
    std::vector<DeviceFacts> list;
    QStringList calls;

    [[nodiscard]] std::vector<DeviceFacts> devices() const override { return list; }
    [[nodiscard]] std::optional<DeviceFacts> device(const QString& serial) const override
    {
        const auto found = std::ranges::find(list, serial, &DeviceFacts::serial);
        return found == list.end() ? std::nullopt : std::optional(*found);
    }
    [[nodiscard]] bool canRetryAdb() const override { return false; }

    void retryAdb() override {}
    void connectDevice(const QString&) override {}
    void disconnectDevice(const QString&) override {}
    void showWindow(const QString&) override {}
    void setTransport(const QString&, int) override {}
    void setMuted(const QString&, bool) override {}
    void setVolume(const QString&, double) override {}
    void setScreenOff(const QString&, bool) override {}
    void setOrientation(const QString&, int) override {}
    void pressKey(const QString& serial, int keycode) override
    {
        calls.append(QStringLiteral("key %1 %2").arg(serial).arg(keycode));
    }
    void setDiagnostics(const QString&, bool) override {}
    void setFullScreen(const QString& serial, bool on) override
    {
        calls.append(QStringLiteral("fullscreen %1 %2").arg(serial).arg(on ? 1 : 0));
    }
    void setMini(const QString&, bool) override {}
    void setEditing(const QString&, bool) override {}
    void editorAction(const QString& serial, const QString& action) override
    {
        calls.append(QStringLiteral("editor %1 %2").arg(serial, action));
    }
    void setMeasuringLatency(const QString&, bool) override {}
    void toggleToolbar(const QString& serial) override
    {
        calls.append(QStringLiteral("toolbar %1").arg(serial));
    }
    void setKeymap(const QString& serial, bool on) override
    {
        calls.append(QStringLiteral("keymap %1 %2").arg(serial).arg(on ? 1 : 0));
    }
    void setCameraLocked(const QString& serial, bool locked) override
    {
        calls.append(QStringLiteral("camera %1 %2").arg(serial).arg(locked ? 1 : 0));
    }
    void setKeyHints(const QString&, bool) override {}
    void stepProfile(const QString&, int) override {}
    void endWireless(const QString&) override {}
};

constexpr char kWindow[] = R"(
import QtQuick
import QtQuick.Templates as T

Window {
    id: root

    width: 400
    height: 300
    visible: true

    property string log

    Item {
        objectName: "probe"
        width: 100
        height: 20
        focus: true
        Keys.onPressed: event => root.log += "press " + event.key + (event.isAutoRepeat ? " repeat" : "") + ";"
        Keys.onReleased: event => root.log += "release " + event.key + ";"
    }

    TextInput {
        objectName: "field"
        y: 30
        width: 100
        height: 20
    }

    T.ComboBox {
        objectName: "list"
        y: 60
        width: 100
        height: 20
        model: ["one", "two"]
        popup: T.Popup {
            width: 100
            height: 60
        }
    }

    T.Popup {
        objectName: "dialog"
        modal: true
        width: 50
        height: 50
    }

    T.Popup {
        objectName: "toast"
        width: 50
        height: 20
    }
}
)";

struct Fixture {
    FakeHost host;
    Commands commands{host};
    std::optional<QString> mirror;
    ShortcutDispatcher dispatcher{commands, [this](const QQuickWindow&) { return mirror; }};
    QQmlEngine engine;
    std::unique_ptr<QQuickWindow> window;
    QStringList requests;

    Fixture()
    {
        QObject::connect(&commands, &Commands::uiRequested, &commands,
                         [this](const QString& id, const QString&) { requests.append(id); });
        QQmlComponent component(&engine);
        component.setData(kWindow, QUrl());
        window.reset(qobject_cast<QQuickWindow*>(component.create()));
        REQUIRE_MESSAGE(window != nullptr, component.errorString().toStdString());
        window->requestActivate();
        REQUIRE(QTest::qWaitForWindowActive(window.get()));
        qApp->installEventFilter(&dispatcher);
    }

    ~Fixture() { qApp->removeEventFilter(&dispatcher); }

    QQuickItem* item(const char* name) const { return window->findChild<QQuickItem*>(name); }
    QObject* popup(const char* name) const { return window->findChild<QObject*>(name); }

    void key(QEvent::Type type, Qt::Key key, Qt::KeyboardModifiers modifiers, const QString& text = {},
             bool repeat = false)
    {
        QKeyEvent event(type, key, modifiers, text, repeat);
        QCoreApplication::sendEvent(window.get(), &event);
    }

    void press(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier, const QString& text = {})
    {
        this->key(QEvent::KeyPress, key, modifiers, text);
    }

    QStringList log() const { return window->property("log").toString().split(u';', Qt::SkipEmptyParts); }
};

}

TEST_CASE("a shortcut runs once and takes its press, repeats and release away from the focused item")
{
    Fixture fixture;
    fixture.key(QEvent::KeyPress, Qt::Key_F, Qt::ControlModifier);
    fixture.key(QEvent::KeyPress, Qt::Key_F, Qt::ControlModifier, {}, true);
    fixture.key(QEvent::KeyRelease, Qt::Key_F, Qt::ControlModifier);
    CHECK(fixture.requests == QStringList{QStringLiteral("settings.search")});
    CHECK(fixture.log().isEmpty());

    fixture.key(QEvent::KeyPress, Qt::Key_F, Qt::NoModifier, QStringLiteral("f"));
    fixture.key(QEvent::KeyRelease, Qt::Key_F, Qt::NoModifier, QStringLiteral("f"));
    CHECK(fixture.log() == QStringList{QStringLiteral("press 70"), QStringLiteral("release 70")});
}

TEST_CASE("a key whose command cannot run reaches the focused item with its repeats and release")
{
    Fixture fixture;
    fixture.commands.applyUserShortcuts({{"device.back", "Ctrl+B"}});
    fixture.key(QEvent::KeyPress, Qt::Key_B, Qt::ControlModifier);
    fixture.key(QEvent::KeyPress, Qt::Key_B, Qt::ControlModifier, {}, true);
    fixture.key(QEvent::KeyRelease, Qt::Key_B, Qt::ControlModifier);
    CHECK(fixture.host.calls.isEmpty());
    CHECK(fixture.log() == QStringList{QStringLiteral("press 66"), QStringLiteral("press 66 repeat"),
                                       QStringLiteral("release 66")});
}

TEST_CASE("a text box keeps the keys it types, characters typed with AltGr included")
{
    Fixture fixture;
    fixture.commands.applyUserShortcuts({{"hub.devices", "Ctrl+Alt+Q"}, {"hub.profiles", "D"}});
    QQuickItem* field = fixture.item("field");
    REQUIRE(field != nullptr);
    field->forceActiveFocus();
    REQUIRE(field->hasActiveFocus());

    fixture.press(Qt::Key_Q, Qt::ControlModifier | Qt::AltModifier, QStringLiteral("@"));
    fixture.press(Qt::Key_D, Qt::NoModifier, QStringLiteral("d"));
    CHECK(fixture.requests.isEmpty());
    CHECK(field->property("text").toString() == QStringLiteral("@d"));

    fixture.press(Qt::Key_Q, Qt::ControlModifier | Qt::AltModifier);
    CHECK(fixture.requests == QStringList{QStringLiteral("hub.devices")});
}

TEST_CASE("an open list, a modal popup or focus inside a popup holds shortcuts back; a passing toast does not")
{
    Fixture fixture;
    QQuickItem* list = fixture.item("list");
    REQUIRE(list != nullptr);
    list->forceActiveFocus();
    auto* dropdown = list->property("popup").value<QObject*>();
    REQUIRE(dropdown != nullptr);
    QMetaObject::invokeMethod(dropdown, "open");
    REQUIRE(dropdown->property("visible").toBool());
    fixture.press(Qt::Key_F, Qt::ControlModifier);
    CHECK(fixture.requests.isEmpty());
    QMetaObject::invokeMethod(dropdown, "close");
    REQUIRE_FALSE(dropdown->property("visible").toBool());
    fixture.press(Qt::Key_F, Qt::ControlModifier);
    CHECK(fixture.requests.size() == 1);

    fixture.item("probe")->forceActiveFocus();
    QObject* dialog = fixture.popup("dialog");
    REQUIRE(dialog != nullptr);
    QMetaObject::invokeMethod(dialog, "open");
    fixture.press(Qt::Key_F, Qt::ControlModifier);
    CHECK(fixture.requests.size() == 1);
    QMetaObject::invokeMethod(dialog, "close");

    QObject* toast = fixture.popup("toast");
    REQUIRE(toast != nullptr);
    QMetaObject::invokeMethod(toast, "open");
    REQUIRE(toast->property("visible").toBool());
    fixture.press(Qt::Key_F, Qt::ControlModifier);
    CHECK(fixture.requests.size() == 2);
}

TEST_CASE("a mirror window matches its own scope and acts on its device")
{
    Fixture fixture;
    DeviceFacts phone;
    phone.serial = QStringLiteral("A");
    phone.connected = true;
    fixture.host.list.push_back(phone);
    fixture.commands.applyUserShortcuts({{"device.back", "Ctrl+B"}});
    fixture.mirror = QStringLiteral("A");

    fixture.press(Qt::Key_B, Qt::ControlModifier);
    CHECK(fixture.host.calls == QStringList{QStringLiteral("key A 4")});
    fixture.press(Qt::Key_F, Qt::ControlModifier);
    CHECK(fixture.requests.isEmpty());
    CHECK(fixture.log() == QStringList{QStringLiteral("press 70")});
}

TEST_CASE("F11 in a mirror window toggles full screen for that window's device and is a plain key elsewhere")
{
    Fixture fixture;
    DeviceFacts phone;
    phone.connected = true;
    phone.serial = QStringLiteral("A");
    fixture.host.list.push_back(phone);
    phone.serial = QStringLiteral("B");
    fixture.host.list.push_back(phone);
    fixture.mirror = QStringLiteral("B");

    fixture.press(Qt::Key_F11);
    fixture.key(QEvent::KeyRelease, Qt::Key_F11, Qt::NoModifier);
    fixture.host.list.back().fullScreen = true;
    fixture.press(Qt::Key_F11);
    fixture.key(QEvent::KeyRelease, Qt::Key_F11, Qt::NoModifier);
    CHECK(fixture.host.calls == QStringList{QStringLiteral("fullscreen B 1"), QStringLiteral("fullscreen B 0")});
    CHECK(fixture.log().isEmpty());

    fixture.mirror.reset();
    fixture.press(Qt::Key_F11);
    CHECK(fixture.host.calls.size() == 2);
    CHECK(fixture.log() == QStringList{QStringLiteral("press %1").arg(static_cast<int>(Qt::Key_F11))});
}

TEST_CASE("a mirror window passes keys the profile binds on to the window and runs the other shortcuts (ADR-0070)")
{
    Fixture fixture;
    DeviceFacts phone;
    phone.connected = true;
    phone.serial = QStringLiteral("B");
    fixture.host.list.push_back(phone);
    fixture.mirror = QStringLiteral("B");
    fixture.dispatcher.setBlocked([](const QString& serial, const QKeyEvent& key) {
        return serial == QStringLiteral("B") && key.key() == Qt::Key_F11;
    });

    fixture.press(Qt::Key_F11);
    fixture.key(QEvent::KeyRelease, Qt::Key_F11, Qt::NoModifier);
    CHECK(fixture.host.calls.isEmpty());
    CHECK(fixture.log() == QStringList{QStringLiteral("press %1").arg(static_cast<int>(Qt::Key_F11)),
                                       QStringLiteral("release %1").arg(static_cast<int>(Qt::Key_F11))});

    fixture.dispatcher.setBlocked([](const QString&, const QKeyEvent&) { return false; });
    fixture.press(Qt::Key_F11);
    CHECK(fixture.host.calls == QStringList{QStringLiteral("fullscreen B 1")});
}

TEST_CASE("while trying the keys, editor shortcuts reach the window; saving and the Edit/Try switch still work (EDT-008)")
{
    Fixture fixture;
    DeviceFacts phone;
    phone.connected = true;
    phone.serial = QStringLiteral("B");
    phone.editing = true;
    fixture.host.list.push_back(phone);
    fixture.mirror = QStringLiteral("B");
    fixture.commands.applyUserShortcuts({{"editor.playtest", "F6"}});

    fixture.press(Qt::Key_Z, Qt::ControlModifier);
    CHECK(fixture.host.calls == QStringList{QStringLiteral("editor B undo")});
    CHECK(fixture.log().isEmpty());

    fixture.host.list.back().trying = true;
    fixture.host.calls.clear();
    fixture.press(Qt::Key_Z, Qt::ControlModifier);
    fixture.press(Qt::Key_Y, Qt::ControlModifier);
    fixture.press(Qt::Key_Delete);
    fixture.press(Qt::Key_D, Qt::ControlModifier);
    fixture.press(Qt::Key_A, Qt::ControlModifier);
    CHECK(fixture.host.calls.isEmpty());
    CHECK(fixture.log().size() == 5);

    fixture.press(Qt::Key_S, Qt::ControlModifier);
    fixture.press(Qt::Key_F6);
    CHECK(fixture.host.calls == QStringList{QStringLiteral("editor B save"), QStringLiteral("editor B edit")});
    CHECK(fixture.log().size() == 5);
}

TEST_CASE("F1 asks for the help article of the window it is pressed in and stays with the game when the profile binds it (ADR-0090)")
{
    Fixture fixture;
    QStringList topics;
    QObject::connect(&fixture.commands, &Commands::uiRequested, &fixture.commands,
                     [&topics](const QString& id, const QString& target) {
                         if (id == QStringLiteral("app.help")) {
                             topics.append(target);
                         }
                     });
    fixture.dispatcher.setHelpTopic([&fixture](const QQuickWindow&) {
        return fixture.mirror ? QStringLiteral("mirror-window") : QStringLiteral("scripts");
    });
    DeviceFacts phone;
    phone.connected = true;
    phone.serial = QStringLiteral("B");
    fixture.host.list.push_back(phone);

    fixture.press(Qt::Key_F1);
    fixture.key(QEvent::KeyRelease, Qt::Key_F1, Qt::NoModifier);
    fixture.mirror = QStringLiteral("B");
    fixture.press(Qt::Key_F1);
    fixture.key(QEvent::KeyRelease, Qt::Key_F1, Qt::NoModifier);
    CHECK(topics == QStringList{QStringLiteral("scripts"), QStringLiteral("mirror-window")});
    CHECK(fixture.log().isEmpty());

    fixture.dispatcher.setBlocked([](const QString& serial, const QKeyEvent& key) {
        return serial == QStringLiteral("B") && key.key() == Qt::Key_F1;
    });
    fixture.press(Qt::Key_F1);
    CHECK(topics.size() == 2);
    CHECK(fixture.log() == QStringList{QStringLiteral("press %1").arg(static_cast<int>(Qt::Key_F1))});
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    doctest::Context context(argc, argv);
    return context.run();
}
