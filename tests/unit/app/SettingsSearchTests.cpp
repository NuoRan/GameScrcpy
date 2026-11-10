#include "SettingsSearch.h"

#include <doctest/doctest.h>

#include <QString>
#include <QStringList>

using gs::app::matchesQuery;

namespace {

const QStringList kSoundRate{QStringLiteral("Sound bit rate"), QStringLiteral("Higher sounds better."),
                             QStringLiteral("audio quality opus kbps")};

}

TEST_CASE("a search finds words anywhere in a setting's texts, whatever their case")
{
    CHECK(matchesQuery(kSoundRate, QStringLiteral("RATE")));
    CHECK(matchesQuery(kSoundRate, QStringLiteral("opus")));
    CHECK(matchesQuery(kSoundRate, QStringLiteral("bett")));
    CHECK_FALSE(matchesQuery(kSoundRate, QStringLiteral("fps")));
}

TEST_CASE("every word of a search must be found, each in any of the texts")
{
    CHECK(matchesQuery(kSoundRate, QStringLiteral("sound kbps")));
    CHECK(matchesQuery(kSoundRate, QStringLiteral("  kbps   sound ")));
    CHECK_FALSE(matchesQuery(kSoundRate, QStringLiteral("sound fps")));
}

TEST_CASE("Chinese searches match without spaces, and an ideographic space separates words")
{
    const QStringList fps{QStringLiteral(u"帧率上限"), QStringLiteral(u"手机画面静止时不出新帧。"),
                          QStringLiteral(u"fps 帧 刷新 流畅")};
    CHECK(matchesQuery(fps, QStringLiteral(u"帧率")));
    CHECK(matchesQuery(fps, QStringLiteral(u"帧率\u3000流畅")));
    CHECK(matchesQuery(fps, QStringLiteral("FPS")));
    CHECK_FALSE(matchesQuery(fps, QStringLiteral(u"码率")));
}

TEST_CASE("an empty search matches everything")
{
    CHECK(matchesQuery(kSoundRate, QString()));
    CHECK(matchesQuery(kSoundRate, QStringLiteral("   ")));
}
