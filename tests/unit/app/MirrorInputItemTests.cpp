#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "KeyForwarding.h"
#include "MirrorInputItem.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QQuickWindow>
#include <QStringList>
#include <QTest>
#include <QWheelEvent>

#include <utility>
#include <vector>

using gs::app::ForwardedKey;
using gs::app::MirrorInputItem;
using gs::input::CursorPhase;
using gs::input::Vec2;

namespace {

struct Touch {
    CursorPhase phase;
    Vec2 position;
};

struct Wheel {
    Vec2 position;
    int horizontal = 0;
    int vertical = 0;
};

struct KeyRecord {
    int keycode = 0;
    int metaState = 0;
    bool down = false;
    int repeat = 0;

    friend bool operator==(const KeyRecord&, const KeyRecord&) = default;
};

struct Surface {
    QQuickWindow window;
    MirrorInputItem* item = new MirrorInputItem(window.contentItem());
    std::vector<Touch> touches;
    std::vector<Wheel> wheels;
    std::vector<KeyRecord> keys;
    std::vector<QPoint> moves;
    std::vector<int> buttons;
    QStringList typed;

    Surface()
    {
        window.resize(400, 300);
        item->setSize(QSizeF(400, 300));
        item->setContentRect(QRectF(100, 0, 200, 300));
        item->setTyping(true);
        item->setFocus(true);
        QObject::connect(item, &MirrorInputItem::touched,
                         [this](CursorPhase phase, Vec2 position) { touches.push_back({phase, position}); });
        QObject::connect(item, &MirrorInputItem::scrolled, [this](Vec2 position, int horizontal, int vertical) {
            wheels.push_back({position, horizontal, vertical});
        });
        QObject::connect(item, &MirrorInputItem::androidKey, [this](int keycode, int metaState, bool down, int repeat) {
            keys.push_back({keycode, metaState, down, repeat});
        });
        QObject::connect(item, &MirrorInputItem::typed, [this](const QString& text) { typed.append(text); });
        QObject::connect(item, &MirrorInputItem::mouseMoved, [this](int dx, int dy) { moves.emplace_back(dx, dy); });
        QObject::connect(item, &MirrorInputItem::mouseButtonsChanged, [this](int held) { buttons.push_back(held); });
        window.show();
        window.requestActivate();
        REQUIRE(QTest::qWaitForWindowActive(&window));
    }

    void wheel(QPointF at, QPoint angle)
    {
        QWheelEvent event(at, window.mapToGlobal(at), QPoint(), angle, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                          false);
        QCoreApplication::sendEvent(&window, &event);
    }

    bool overrides(Qt::Key key, Qt::KeyboardModifiers modifiers, const QString& text)
    {
        QKeyEvent query(QEvent::ShortcutOverride, key, modifiers, text);
        query.ignore();
        QCoreApplication::sendEvent(&window, &query);
        return query.isAccepted();
    }
};

KeyRecord down(int keycode, int metaState = 0, int repeat = 0)
{
    return {keycode, metaState, true, repeat};
}

KeyRecord up(int keycode, int metaState = 0)
{
    return {keycode, metaState, false, 0};
}

}

TEST_CASE("positions map through the content rectangle, clamped only while a touch is held (MED-003)")
{
    const QRectF content(100, 0, 200, 300);
    CHECK(MirrorInputItem::normalized(content, {150, 75}, false) == Vec2{0.25, 0.25});
    CHECK(MirrorInputItem::normalized(content, {300, 300}, false) == Vec2{1.0, 1.0});
    CHECK_FALSE(MirrorInputItem::normalized(content, {50, 150}, false).has_value());
    CHECK(MirrorInputItem::normalized(content, {50, 400}, true) == Vec2{0.0, 1.0});
    CHECK_FALSE(MirrorInputItem::normalized(QRectF(), {50, 50}, true).has_value());
}

TEST_CASE("wheel steps of 120 become 256 scroll units")
{
    CHECK(MirrorInputItem::scrollUnits(120) == 256);
    CHECK(MirrorInputItem::scrollUnits(-120) == -256);
    CHECK(MirrorInputItem::scrollUnits(60) == 128);
    CHECK(MirrorInputItem::scrollUnits(8) == 17);
    CHECK(MirrorInputItem::scrollUnits(1'000'000) == 32767);
}

TEST_CASE("printable text is typed, other keys become Android keycodes with their modifiers (ADR-0069)")
{
    using gs::app::androidKeyOf;
    using gs::app::typesText;
    CHECK(typesText(QStringLiteral("a"), Qt::NoModifier));
    CHECK(typesText(QStringLiteral("A"), Qt::ShiftModifier));
    CHECK(typesText(QString(QChar(0x20AC)), Qt::ControlModifier | Qt::AltModifier));
    CHECK_FALSE(typesText(QStringLiteral("a"), Qt::ControlModifier));
    CHECK_FALSE(typesText(QStringLiteral("a"), Qt::AltModifier));
    CHECK_FALSE(typesText(QStringLiteral("\r"), Qt::NoModifier));
    CHECK_FALSE(typesText(QString(), Qt::NoModifier));

    CHECK(androidKeyOf(Qt::Key_Return, Qt::NoModifier) == ForwardedKey{66, 0});
    CHECK(androidKeyOf(Qt::Key_Backspace, Qt::NoModifier) == ForwardedKey{67, 0});
    CHECK(androidKeyOf(Qt::Key_A, Qt::ControlModifier) == ForwardedKey{29, 0x3000});
    CHECK(androidKeyOf(Qt::Key_5, Qt::ControlModifier | Qt::ShiftModifier) == ForwardedKey{12, 0x3041});
    CHECK(androidKeyOf(Qt::Key_Backtab, Qt::ShiftModifier) == ForwardedKey{61, 0x41});
    CHECK(androidKeyOf(Qt::Key_F12, Qt::NoModifier) == ForwardedKey{142, 0});
    CHECK(androidKeyOf(Qt::Key_Left, Qt::AltModifier) == ForwardedKey{21, 0x12});
    CHECK_FALSE(androidKeyOf(Qt::Key_A, Qt::NoModifier).has_value());
    CHECK_FALSE(androidKeyOf(Qt::Key_VolumeUp, Qt::NoModifier).has_value());
}

TEST_CASE("a left drag on the picture becomes a touch, a press on the letterbox does nothing (INP-002)")
{
    Surface s;
    QTest::mousePress(&s.window, Qt::LeftButton, {}, QPoint(150, 75));
    QTest::mouseMove(&s.window, QPoint(350, 150));
    QTest::mouseRelease(&s.window, Qt::LeftButton, {}, QPoint(350, 150));
    REQUIRE(s.touches.size() == 3);
    CHECK(s.touches[0].phase == CursorPhase::Down);
    CHECK(s.touches[0].position == Vec2{0.25, 0.25});
    CHECK(s.touches[1].phase == CursorPhase::Move);
    CHECK(s.touches[1].position == Vec2{1.0, 0.5});
    CHECK(s.touches[2].phase == CursorPhase::Up);
    CHECK(s.touches[2].position == Vec2{1.0, 0.5});

    QTest::mousePress(&s.window, Qt::LeftButton, {}, QPoint(50, 150));
    QTest::mouseRelease(&s.window, Qt::LeftButton, {}, QPoint(50, 150));
    CHECK(s.touches.size() == 3);
}

TEST_CASE("mouse buttons send no phone keys by default, and the configured keys anywhere in the window (ADR-0070)")
{
    Surface s;
    QTest::mouseClick(&s.window, Qt::RightButton, {}, QPoint(50, 150));
    QTest::mouseClick(&s.window, Qt::MiddleButton, {}, QPoint(200, 150));
    CHECK(s.keys.empty());
    CHECK(s.touches.empty());
    CHECK_FALSE(s.item->keycodeOf(Qt::RightButton).has_value());

    s.item->setButtonKeys({{Qt::RightButton, 4u}, {Qt::MiddleButton, 3u}, {Qt::ForwardButton, 187u}});
    CHECK(s.item->keycodeOf(Qt::RightButton) == 4u);
    CHECK_FALSE(s.item->keycodeOf(Qt::LeftButton).has_value());
    QTest::mouseClick(&s.window, Qt::RightButton, {}, QPoint(50, 150));
    QTest::mouseClick(&s.window, Qt::MiddleButton, {}, QPoint(200, 150));
    QTest::mouseClick(&s.window, Qt::ForwardButton, {}, QPoint(200, 150));
    const std::vector<KeyRecord> expected{down(4), up(4), down(3), up(3), down(187), up(187)};
    CHECK(s.keys == expected);
    CHECK(s.touches.empty());
}

TEST_CASE("the wheel scrolls at the pointer with up and right positive, and only over the picture")
{
    Surface s;
    s.wheel({200, 150}, {0, 120});
    s.wheel({200, 150}, {120, 0});
    s.wheel({50, 150}, {0, 120});
    REQUIRE(s.wheels.size() == 2);
    CHECK(s.wheels[0].position == Vec2{0.5, 0.5});
    CHECK(s.wheels[0].vertical == 256);
    CHECK(s.wheels[0].horizontal == 0);
    CHECK(s.wheels[1].horizontal == -256);
    CHECK(s.wheels[1].vertical == 0);
}

TEST_CASE("wheel directions a keymap binding claims in cursor state do not scroll the phone (ADR-0077)")
{
    Surface s;
    s.item->setClaimedInputs({gs::input::Chord{.source = *gs::input::parseSource("Mouse.WheelUp")},
                              gs::input::Chord{.source = *gs::input::parseSource("Mouse.WheelLeft")}});
    CHECK(s.item->claimed(*gs::input::parseSource("Mouse.WheelUp")));
    CHECK(s.item->claimed(*gs::input::parseSource("Mouse.WheelUp"), gs::input::modifier::kCtrl));
    CHECK_FALSE(s.item->claimed(*gs::input::parseSource("Mouse.WheelDown")));
    s.wheel({200, 150}, {0, 120});
    s.wheel({200, 150}, {0, -120});
    s.wheel({200, 150}, {120, 0});
    s.wheel({200, 150}, {-120, 0});
    REQUIRE(s.wheels.size() == 2);
    CHECK(s.wheels[0].vertical == -256);
    CHECK(s.wheels[0].horizontal == 0);
    CHECK(s.wheels[1].horizontal == 256);
    CHECK(s.wheels[1].vertical == 0);

    s.item->setClaimedInputs({});
    s.wheel({200, 150}, {0, 120});
    CHECK(s.wheels.size() == 3);

    s.item->setClaimedInputs({gs::input::Chord{.modifiers = gs::input::modifier::kCtrl,
                                               .source = *gs::input::parseSource("Mouse.WheelUp")}});
    CHECK_FALSE(s.item->claimed(*gs::input::parseSource("Mouse.WheelUp")));
    CHECK(s.item->claimed(*gs::input::parseSource("Mouse.WheelUp"), gs::input::modifier::kCtrl));
    s.wheel({200, 150}, {0, 120});
    CHECK(s.wheels.size() == 4);
}

TEST_CASE("while the camera is locked the surface cancels what it holds and ignores input (ADR-0070)")
{
    Surface s;
    s.item->setButtonKeys({{Qt::RightButton, 4u}});
    QTest::mousePress(&s.window, Qt::LeftButton, {}, QPoint(150, 75));
    QTest::mousePress(&s.window, Qt::RightButton, {}, QPoint(150, 75));
    s.item->setCapturing(true);
    REQUIRE(s.touches.size() == 2);
    CHECK(s.touches[1].phase == CursorPhase::Cancel);
    CHECK(s.keys == std::vector<KeyRecord>{down(4), up(4)});

    QTest::mouseRelease(&s.window, Qt::RightButton, {}, QPoint(150, 75));
    QTest::mouseRelease(&s.window, Qt::LeftButton, {}, QPoint(150, 75));
    QTest::mouseClick(&s.window, Qt::LeftButton, {}, QPoint(200, 150));
    s.wheel({200, 150}, {0, 120});
    CHECK(s.touches.size() == 2);
    CHECK(s.keys.size() == 2);
    CHECK(s.wheels.empty());

    s.item->setCapturing(false);
    QTest::mouseClick(&s.window, Qt::LeftButton, {}, QPoint(200, 150));
    REQUIRE(s.touches.size() == 4);
    CHECK(s.touches[2].phase == CursorPhase::Down);
    CHECK(s.touches[3].phase == CursorPhase::Up);
}

TEST_CASE("keys typed into the mirror window reach the phone as text or as held Android keys (ADR-0069)")
{
    Surface s;
    QTest::keyClick(&s.window, 'a');
    QTest::keyClick(&s.window, 'A', Qt::ShiftModifier);
    QTest::keyClick(&s.window, Qt::Key_Return);
    QTest::keyClick(&s.window, Qt::Key_C, Qt::ControlModifier);
    CHECK(s.typed == QStringList{QStringLiteral("a"), QStringLiteral("A")});
    CHECK(s.keys == std::vector<KeyRecord>{down(66), up(66), down(31, 0x3000), up(31, 0x3000)});

    s.keys.clear();
    QTest::keyPress(&s.window, Qt::Key_Left);
    QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier, QString(), true);
    QCoreApplication::sendEvent(&s.window, &repeat);
    s.item->setFocus(false);
    CHECK(s.keys == std::vector<KeyRecord>{down(21), down(21, 0, 1), up(21)});
    QTest::keyRelease(&s.window, Qt::Key_Left);
    CHECK(s.keys.size() == 3);

    s.item->setFocus(true);
    s.item->setTyping(false);
    QTest::keyClick(&s.window, Qt::Key_B);
    QTest::keyClick(&s.window, Qt::Key_Return);
    CHECK(s.typed.size() == 2);
    CHECK(s.keys.size() == 3);
}

TEST_CASE("while typing the surface takes the keys a text box would, never function keys (ADR-0062)")
{
    Surface s;
    CHECK(s.overrides(Qt::Key_A, Qt::NoModifier, QStringLiteral("a")));
    CHECK(s.overrides(Qt::Key_Backspace, Qt::NoModifier, QString()));
    CHECK(s.overrides(Qt::Key_V, Qt::ControlModifier, QString()));
    CHECK_FALSE(s.overrides(Qt::Key_F11, Qt::NoModifier, QString()));
    CHECK_FALSE(s.overrides(Qt::Key_K, Qt::ControlModifier, QString()));
    CHECK_FALSE(s.overrides(Qt::Key_Escape, Qt::NoModifier, QString()));

    s.item->setTyping(false);
    CHECK_FALSE(s.overrides(Qt::Key_A, Qt::NoModifier, QStringLiteral("a")));
}

TEST_CASE("input method commits are typed, and the input method is on only while typing (INP-009)")
{
    Surface s;
    CHECK(s.item->inputMethodQuery(Qt::ImEnabled).toBool());
    CHECK(s.item->flags().testFlag(QQuickItem::ItemAcceptsInputMethod));
    QInputMethodEvent compose(QStringLiteral("zhong"), {});
    QCoreApplication::sendEvent(s.item, &compose);
    QInputMethodEvent commit;
    commit.setCommitString(QString::fromUtf8("\xE4\xB8\xAD\xE6\x96\x87"));
    QCoreApplication::sendEvent(s.item, &commit);
    CHECK(s.typed == QStringList{QString::fromUtf8("\xE4\xB8\xAD\xE6\x96\x87")});

    s.item->setTyping(false);
    CHECK_FALSE(s.item->inputMethodQuery(Qt::ImEnabled).toBool());
    CHECK_FALSE(s.item->flags().testFlag(QQuickItem::ItemAcceptsInputMethod));
    QCoreApplication::sendEvent(s.item, &commit);
    CHECK(s.typed.size() == 1);
}

TEST_CASE("without a picture the mouse moves the phone's own pointer in its pixels and clicks there, never touching "
          "(HID-006)")
{
    Surface s;
    s.item->setRelativeMouse(true, QSizeF(1080, 2400));
    CHECK(s.item->relativeMouse());
    QTest::mouseMove(&s.window, QPoint(150, 75));
    QTest::mouseMove(&s.window, QPoint(160, 80));
    QTest::mouseMove(&s.window, QPoint(161, 80));
    REQUIRE_FALSE(s.moves.empty());
    int dx = 0;
    int dy = 0;
    for (const QPoint& move : s.moves) {
        dx += move.x();
        dy += move.y();
    }
    CHECK(dx == 59);
    CHECK(dy == 40);

    QTest::mousePress(&s.window, Qt::LeftButton, {}, QPoint(161, 80));
    QTest::mouseRelease(&s.window, Qt::LeftButton, {}, QPoint(161, 80));
    QTest::mouseClick(&s.window, Qt::RightButton, {}, QPoint(161, 80));
    CHECK(s.buttons == std::vector<int>{1, 0, 2, 0});
    CHECK(s.touches.empty());
    CHECK(s.keys.empty());
    s.wheel({200, 150}, {0, 120});
    REQUIRE(s.wheels.size() == 1);
    CHECK(s.wheels[0].vertical == 256);
}

TEST_CASE("the phone's pointer follows the mouse only while the window is in front, and stops while the camera is "
          "locked (HID-006)")
{
    Surface s;
    s.item->setRelativeMouse(true, QSizeF(200, 300));
    QTest::mouseMove(&s.window, QPoint(150, 75));
    QTest::mouseMove(&s.window, QPoint(170, 75));
    REQUIRE(s.moves.size() == 1);
    CHECK(s.moves[0] == QPoint(20, 0));

    QQuickWindow other;
    other.resize(100, 100);
    other.show();
    other.requestActivate();
    REQUIRE(QTest::qWaitForWindowActive(&other));
    QTest::mouseMove(&s.window, QPoint(190, 75));
    QTest::mouseMove(&s.window, QPoint(200, 90));
    CHECK(s.moves.size() == 1);

    s.window.requestActivate();
    REQUIRE(QTest::qWaitForWindowActive(&s.window));
    QTest::mouseMove(&s.window, QPoint(150, 150));
    QTest::mouseMove(&s.window, QPoint(155, 150));
    REQUIRE(s.moves.size() == 2);
    CHECK(s.moves[1] == QPoint(5, 0));

    QTest::mousePress(&s.window, Qt::LeftButton, {}, QPoint(155, 150));
    s.item->setCapturing(true);
    CHECK(s.buttons == std::vector<int>{1, 0});
    QTest::mouseMove(&s.window, QPoint(180, 160));
    CHECK(s.moves.size() == 2);
    s.item->setCapturing(false);

    s.item->setClaimedInputs({gs::input::Chord{.source = *gs::input::parseSource("Mouse.Right")}});
    QTest::mouseClick(&s.window, Qt::RightButton, {}, QPoint(180, 160));
    CHECK(s.buttons == std::vector<int>{1, 0});
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    doctest::Context context(argc, argv);
    return context.run();
}
