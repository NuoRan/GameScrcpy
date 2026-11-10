#include "TrayNotices.h"

#include <doctest/doctest.h>

#include <QString>
#include <QStringList>

using gs::app::NoticeTarget;
using gs::app::TrayNotices;
using namespace Qt::StringLiterals;

TEST_CASE("a click on a notification opens what the balloon on screen is about, and a newer balloon takes over")
{
    TrayNotices notices;
    QStringList balloons;
    bool shows = true;
    const TrayNotices::Balloon balloon = [&balloons, &shows](const QString& title, const QString& text) {
        if (shows) {
            balloons.append(title + u" | "_s + text);
        }
        return shows;
    };
    CHECK(notices.target() == NoticeTarget{});

    const NoticeTarget phone{.kind = NoticeTarget::Kind::Phone, .serial = u"R58M"_s};
    CHECK(notices.show(u"Boss"_s, u"The boss is here"_s, phone, balloon));
    CHECK(notices.target() == phone);

    const NoticeTarget other{.kind = NoticeTarget::Kind::Phone, .serial = u"emulator-5556"_s};
    CHECK(notices.show(u"Chest"_s, u"A chest opened"_s, other, balloon));
    CHECK(notices.target() == other);

    const NoticeTarget task{.kind = NoticeTarget::Kind::Task, .task = u"check-in"_s};
    CHECK(notices.show(u"Task done"_s, u"Check-in finished on R58M"_s, task, balloon));
    CHECK(notices.target() == task);
    CHECK(notices.target().serial.isEmpty());

    shows = false;
    CHECK_FALSE(notices.show(u"Boss"_s, u"Again"_s, phone, balloon));
    CHECK(notices.target() == task);
    CHECK_FALSE(notices.show(u"Boss"_s, u"Again"_s, phone, {}));
    CHECK(notices.target() == task);

    shows = true;
    CHECK(notices.show(u"GameScrcpy"_s, u"Still running in the tray"_s, NoticeTarget{}, balloon));
    CHECK(notices.target() == NoticeTarget{});
    CHECK(balloons == QStringList{u"Boss | The boss is here"_s, u"Chest | A chest opened"_s,
                                  u"Task done | Check-in finished on R58M"_s,
                                  u"GameScrcpy | Still running in the tray"_s});
}
