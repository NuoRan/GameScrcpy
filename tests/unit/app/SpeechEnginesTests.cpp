#include "SpeechEngines.h"

#include <gs/platform/Speech.h>

#include <doctest/doctest.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using gs::app::LogicCaller;
using gs::app::SpeechDefaults;
using gs::app::SpeechEngine;
using gs::app::SpeechEngineInfo;
using gs::app::SpeechEngines;
using gs::app::SpeechHeardInfo;
using gs::app::SpeechLineRequest;
using gs::app::SpeechListenRequest;
using gs::app::SpeechReplies;
using gs::app::SpeechVoiceInfo;

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

bool pumpUntil(const std::function<bool()>& done)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

void settleEvents()
{
    for (int round = 0; round < 5; ++round) {
        QCoreApplication::processEvents();
    }
}

struct FakeState {
    std::vector<SpeechLineRequest> said;
    std::map<std::uint64_t, SpeechEngine::Spoken> speaking;
    std::vector<std::uint64_t> stopped;
    std::vector<SpeechListenRequest> listens;
    std::map<std::uint64_t, std::pair<SpeechEngine::Heard, SpeechEngine::Ended>> listening;
    std::vector<std::uint64_t> unlistened;
    std::uint64_t next = 0;

    void finish(std::uint64_t token, bool finished, const QString& problem = {})
    {
        const auto found = speaking.find(token);
        REQUIRE(found != speaking.end());
        SpeechEngine::Spoken spoken = std::move(found->second);
        speaking.erase(found);
        spoken(finished, problem);
    }

    void hear(std::uint64_t token, SpeechHeardInfo heard)
    {
        REQUIRE(listening.contains(token));
        listening.at(token).first(std::move(heard));
    }

    void end(std::uint64_t token, const QString& problem)
    {
        REQUIRE(listening.contains(token));
        SpeechEngine::Ended ended = std::move(listening.at(token).second);
        listening.erase(token);
        ended(problem);
    }
};

class FakeEngine final : public SpeechEngine
{
public:
    FakeEngine(std::shared_ptr<FakeState> state, SpeechEngineInfo info)
        : m_state(std::move(state))
        , m_info(std::move(info))
    {
    }

    SpeechEngineInfo info() const override { return m_info; }

    void voices(Voices done) override
    {
        done({{.id = QStringLiteral("robot"), .name = QStringLiteral("Robot"), .language = QStringLiteral("en-US")}},
             {});
    }

    std::uint64_t say(const SpeechLineRequest& line, Spoken spoken) override
    {
        const std::uint64_t token = ++m_state->next;
        m_state->said.push_back(line);
        m_state->speaking.emplace(token, std::move(spoken));
        return token;
    }

    void stop(std::uint64_t token) override
    {
        m_state->stopped.push_back(token);
        if (m_state->speaking.contains(token)) {
            m_state->finish(token, false, QStringLiteral("cut off"));
        }
    }

    std::uint64_t listen(const SpeechListenRequest& wanted, Heard heard, Ended ended) override
    {
        const std::uint64_t token = ++m_state->next;
        m_state->listens.push_back(wanted);
        m_state->listening.emplace(token, std::make_pair(std::move(heard), std::move(ended)));
        return token;
    }

    void unlisten(std::uint64_t token) override
    {
        m_state->unlistened.push_back(token);
        m_state->listening.erase(token);
    }

private:
    std::shared_ptr<FakeState> m_state;
    SpeechEngineInfo m_info;
};

struct Told {
    std::uint64_t logic = 0;
    QString kind;
    QString id;
    QJsonObject value;
};

const LogicCaller kOwn{.logic = 1};
const LogicCaller kOther{.logic = 2};
const LogicCaller kTalker{.logic = 3, .extension = QStringLiteral("alice.talker")};

struct Fixture {
    Application app;
    SpeechEngines engines{nullptr};
    std::shared_ptr<FakeState> fake = std::make_shared<FakeState>();
    std::vector<std::pair<quint64, QString>> answers;
    std::vector<std::pair<quint64, QString>> refusals;
    std::vector<Told> told;
    SpeechDefaults defaults{.engine = QStringLiteral("fake"),
                            .voice = QStringLiteral("robot"),
                            .rate = 1.5,
                            .volume = 0.4,
                            .listenEngine = QStringLiteral("fake"),
                            .language = {},
                            .interfaceLanguage = QStringLiteral("zh-CN")};

    Fixture()
    {
        engines.setReplies(SpeechReplies{
            .answer = [this](std::uint64_t, quint64 request, const QString& json) { answers.emplace_back(request, json); },
            .refuse = [this](std::uint64_t, quint64 request,
                             const QString& message) { refusals.emplace_back(request, message); },
            .tell =
                [this](std::uint64_t logic, const QString& kind, const QString& id, const QString& json) {
                    told.push_back({logic, kind, id, QJsonDocument::fromJson(json.toUtf8()).object()});
                },
        });
        engines.setDefaults([this] { return defaults; });
        engines.addEngine(std::make_unique<FakeEngine>(
            fake, SpeechEngineInfo{.id = QStringLiteral("fake"),
                                   .title = QStringLiteral("Fake"),
                                   .say = true,
                                   .listen = true,
                                   .dictation = false,
                                   .languages = {QStringLiteral("en-US"), QStringLiteral("zh-CN")}}));
    }

    QString answerTo(quint64 request) const
    {
        const auto found = std::ranges::find(answers, request, &std::pair<quint64, QString>::first);
        return found == answers.end() ? QString() : found->second;
    }

    QString refusalOf(quint64 request) const
    {
        const auto found = std::ranges::find(refusals, request, &std::pair<quint64, QString>::first);
        return found == refusals.end() ? QString() : found->second;
    }

    void say(const LogicCaller& caller, quint64 id, const QString& json)
    {
        engines.request(caller, "speech.say", id, json);
        settleEvents();
    }

    QJsonObject listen(const LogicCaller& caller, quint64 id, const QString& json)
    {
        engines.request(caller, "speech.listen", id, json);
        settleEvents();
        return QJsonDocument::fromJson(answerTo(id).toUtf8()).object();
    }
};

}

TEST_CASE("ADR-0112: speech.say reads one text at a time per engine, in turn, with the settings' voice, rate and "
          "volume unless the logic picks others")
{
    Fixture fixture;
    fixture.say(kOwn, 10, QStringLiteral(R"({"text":"one"})"));
    REQUIRE(fixture.fake->said.size() == 1);
    CHECK(fixture.fake->said[0].text == u"one");
    CHECK(fixture.fake->said[0].voice == u"robot");
    CHECK(fixture.fake->said[0].rate == doctest::Approx(1.5));
    CHECK(fixture.fake->said[0].volume == doctest::Approx(0.4));

    fixture.say(kOther, 11, QStringLiteral(R"({"text":"two","voice":"bell","rate":0.75,"volume":1})"));
    CHECK(fixture.fake->said.size() == 1);
    fixture.fake->finish(1, true);
    REQUIRE(pumpUntil([&fixture] { return fixture.fake->said.size() == 2; }));
    CHECK(fixture.answerTo(10) == u"true");
    CHECK(fixture.fake->said[1].voice == u"bell");
    CHECK(fixture.fake->said[1].rate == doctest::Approx(0.75));
    CHECK(fixture.fake->said[1].volume == doctest::Approx(1.0));
    fixture.fake->finish(2, false, QStringLiteral("it broke"));
    REQUIRE(pumpUntil([&fixture] { return !fixture.refusalOf(11).isEmpty(); }));
    CHECK(fixture.refusalOf(11) == u"GS-API-112 it broke");

    fixture.say(kOwn, 12, QStringLiteral(R"({"text":"three","engine":"nobody"})"));
    CHECK(fixture.refusalOf(12).startsWith(u"GS-API-110 "));
    fixture.say(kOwn, 13, QStringLiteral(R"({"text":"   "})"));
    CHECK(fixture.refusalOf(13).startsWith(u"GS-API-111 "));
    fixture.say(kOwn, 14, QStringLiteral(R"({"text":"fast","rate":3})"));
    CHECK(fixture.refusalOf(14).startsWith(u"GS-API-111 "));
    fixture.say(kOwn, 15, QStringLiteral(R"({"text":"%1"})").arg(QString(1001, u'a')));
    CHECK(fixture.refusalOf(15).startsWith(u"GS-API-111 "));
    fixture.say(kOwn, 16, QStringLiteral(R"({"text":"loud","volume":-1})"));
    CHECK(fixture.refusalOf(16).startsWith(u"GS-API-111 "));
    CHECK(fixture.fake->said.size() == 2);

    fixture.defaults.engine = QStringLiteral("nobody");
    fixture.say(kOwn, 17, QStringLiteral(R"({"text":"four","engine":"fake"})"));
    REQUIRE(fixture.fake->said.size() == 3);
    CHECK(fixture.fake->said[2].voice.isEmpty());
}

TEST_CASE("ADR-0112: interrupt, speech.stop, a cancelled task and the end of a logic cut off what is read and drop "
          "what waits")
{
    Fixture fixture;
    fixture.say(kOwn, 20, QStringLiteral(R"({"text":"a"})"));
    fixture.say(kOwn, 21, QStringLiteral(R"({"text":"b"})"));
    fixture.say(kOther, 22, QStringLiteral(R"({"text":"c"})"));
    fixture.say(kOther, 23, QStringLiteral(R"({"text":"d","interrupt":true})"));
    CHECK(fixture.fake->stopped == std::vector<std::uint64_t>{1});
    CHECK(fixture.answerTo(21) == u"false");
    CHECK(fixture.answerTo(22) == u"false");
    REQUIRE(pumpUntil([&fixture] { return fixture.fake->said.size() == 2; }));
    CHECK(fixture.answerTo(20) == u"false");
    CHECK(fixture.refusalOf(20).isEmpty());
    CHECK(fixture.fake->said[1].text == u"d");

    CHECK(fixture.engines.call(kOther, "speech.stop", "{}").code.empty());
    REQUIRE(pumpUntil([&fixture] { return !fixture.answerTo(23).isEmpty(); }));
    CHECK(fixture.answerTo(23) == u"false");

    fixture.say(kOwn, 24, QStringLiteral(R"({"text":"e"})"));
    fixture.say(kOwn, 25, QStringLiteral(R"({"text":"f"})"));
    fixture.engines.dropRequest(kOwn, 25);
    CHECK(fixture.answerTo(25) == u"false");
    fixture.engines.dropRequest(kOwn, 24);
    REQUIRE(pumpUntil([&fixture] { return !fixture.answerTo(24).isEmpty(); }));
    CHECK(fixture.answerTo(24) == u"false");

    fixture.say(kOther, 26, QStringLiteral(R"({"text":"g"})"));
    fixture.engines.endLogic(2);
    REQUIRE(pumpUntil([&fixture] { return !fixture.answerTo(26).isEmpty(); }));
    CHECK(fixture.answerTo(26) == u"false");

    for (quint64 id = 100; id < 134; ++id) {
        fixture.say(kOwn, id, QStringLiteral(R"({"text":"many"})"));
    }
    CHECK(fixture.refusalOf(132).isEmpty());
    CHECK(fixture.refusalOf(133).startsWith(u"GS-API-111 "));

    CHECK(fixture.engines.preview());
    fixture.engines.endLogic(1);
    settleEvents();
    REQUIRE(pumpUntil([&fixture] { return fixture.fake->said.back().text != u"many"; }));
    CHECK_FALSE(fixture.fake->said.back().text.isEmpty());
    CHECK(fixture.fake->said.back().voice == u"robot");
}

TEST_CASE("ADR-0112: a text that interrupts is taken even when the logic has 32 texts waiting already")
{
    Fixture fixture;
    for (quint64 id = 300; id < 333; ++id) {
        fixture.say(kOwn, id, QStringLiteral(R"({"text":"queued"})"));
    }
    CHECK(fixture.refusalOf(332).isEmpty());
    fixture.say(kOwn, 333, QStringLiteral(R"({"text":"more"})"));
    CHECK(fixture.refusalOf(333).startsWith(u"GS-API-111 "));

    fixture.say(kOwn, 334, QStringLiteral(R"({"text":"now","interrupt":true})"));
    CHECK(fixture.refusalOf(334).isEmpty());
    REQUIRE(pumpUntil([&fixture] { return fixture.fake->said.size() == 2; }));
    CHECK(fixture.fake->said.front().text == u"queued");
    CHECK(fixture.fake->said.back().text == u"now");
    for (quint64 id = 300; id < 333; ++id) {
        CHECK(fixture.answerTo(id) == u"false");
    }
}

TEST_CASE("ADR-0112: speech.listen hands over what is heard surely enough to the logic until it closes")
{
    Fixture fixture;
    const QJsonObject made =
        fixture.listen(kOwn, 30, QStringLiteral(R"({"phrases":["open map"," close bag ","open map"],"confidence":0.5})"));
    CHECK(made.value(u"handle").toString() == u"s1");
    CHECK(made.value(u"engine").toString() == u"fake");
    CHECK(made.value(u"language").toString() == u"zh-CN");
    REQUIRE(fixture.fake->listens.size() == 1);
    CHECK(fixture.fake->listens[0].phrases == QStringList{QStringLiteral("open map"), QStringLiteral("close bag")});
    CHECK(fixture.fake->listens[0].confidence == doctest::Approx(0.5));

    fixture.fake->hear(1, {.phrase = QStringLiteral("open map"), .text = QStringLiteral("open map"), .confidence = 0.9});
    fixture.fake->hear(1, {.phrase = QStringLiteral("close bag"), .text = QStringLiteral("close"), .confidence = 0.3});
    REQUIRE(pumpUntil([&fixture] { return !fixture.told.empty(); }));
    settleEvents();
    REQUIRE(fixture.told.size() == 1);
    CHECK(fixture.told[0].logic == 1);
    CHECK(fixture.told[0].kind == u"speech");
    CHECK(fixture.told[0].id == u"s1");
    CHECK(fixture.told[0].value == QJsonObject{{QStringLiteral("type"), QStringLiteral("heard")},
                                               {QStringLiteral("phrase"), QStringLiteral("open map")},
                                               {QStringLiteral("text"), QStringLiteral("open map")},
                                               {QStringLiteral("confidence"), 0.9}});

    const QJsonObject english = fixture.listen(kOwn, 31, QStringLiteral(R"({"phrases":["hi"],"language":"en"})"));
    CHECK(english.value(u"language").toString() == u"en-US");
    fixture.listen(kOwn, 32, QStringLiteral(R"({"phrases":["salut"],"language":"fr-FR"})"));
    CHECK(fixture.refusalOf(32).startsWith(u"GS-API-110 "));
    fixture.listen(kOwn, 33, QStringLiteral(R"({"phrases":null})"));
    CHECK(fixture.refusalOf(33).startsWith(u"GS-API-110 "));
    fixture.listen(kOwn, 34, QStringLiteral(R"({"phrases":[]})"));
    CHECK(fixture.refusalOf(34).startsWith(u"GS-API-111 "));
    fixture.listen(kOwn, 35, QStringLiteral(R"({"phrases":["%1"]})").arg(QString(65, u'x')));
    CHECK(fixture.refusalOf(35).startsWith(u"GS-API-111 "));
    fixture.listen(kOwn, 36, QStringLiteral(R"({"phrases":["hi"],"confidence":2})"));
    CHECK(fixture.refusalOf(36).startsWith(u"GS-API-111 "));

    CHECK(fixture.engines.call(kOther, "speech.close", R"({"handle":"s1"})").code.empty());
    settleEvents();
    CHECK(fixture.fake->unlistened.empty());
    CHECK(fixture.engines.call(kOwn, "speech.close", R"({"handle":"s1"})").code.empty());
    REQUIRE(pumpUntil([&fixture] { return !fixture.fake->unlistened.empty(); }));
    CHECK(fixture.fake->unlistened == std::vector<std::uint64_t>{1});

    fixture.fake->end(2, QStringLiteral("the microphone went away"));
    REQUIRE(pumpUntil([&fixture] { return fixture.told.size() == 2; }));
    CHECK(fixture.told[1].id == u"s2");
    CHECK(fixture.told[1].value.value(u"type").toString() == u"closed");
    CHECK(fixture.told[1].value.value(u"reason").toString() == u"engine");
    CHECK(fixture.told[1].value.value(u"problem").toString() == u"the microphone went away");

    for (quint64 id = 40; id < 49; ++id) {
        fixture.listen(kOther, id, QStringLiteral(R"({"phrases":["go"]})"));
    }
    CHECK(fixture.refusalOf(47).isEmpty());
    CHECK(fixture.refusalOf(48).startsWith(u"GS-API-111 "));
    const std::size_t before = fixture.fake->unlistened.size();
    fixture.engines.endLogic(2);
    CHECK(fixture.fake->unlistened.size() == before + 8);
    CHECK(fixture.told.size() == 2);
}

TEST_CASE("ADR-0112: only an extension that may listen gets to, and listeners close when the permission or the "
          "engine goes away")
{
    Fixture fixture;
    fixture.listen(kTalker, 50, QStringLiteral(R"({"phrases":["go"]})"));
    CHECK(fixture.refusalOf(50).startsWith(u"GS-API-113 "));
    fixture.engines.setPermitted({QStringLiteral("alice.talker")});
    const QString handle = fixture.listen(kTalker, 51, QStringLiteral(R"({"phrases":["go"]})")).value(u"handle").toString();
    REQUIRE_FALSE(handle.isEmpty());
    fixture.engines.setPermitted({});
    REQUIRE(fixture.told.size() == 1);
    CHECK(fixture.told[0].id == handle);
    CHECK(fixture.told[0].value.value(u"reason").toString() == u"forbidden");
    CHECK(fixture.fake->listening.empty());

    fixture.say(kTalker, 52, QStringLiteral(R"({"text":"no permission needed"})"));
    CHECK(fixture.refusalOf(52).isEmpty());
    REQUIRE(fixture.fake->said.size() == 1);
    fixture.say(kOwn, 53, QStringLiteral(R"({"text":"waiting"})"));
    const QString own = fixture.listen(kOwn, 54, QStringLiteral(R"({"phrases":["go"]})")).value(u"handle").toString();

    const QJsonDocument listed = QJsonDocument::fromJson(
        QByteArray::fromStdString(fixture.engines.call(kOwn, "speech.engines", "{}").json));
    REQUIRE(listed.isArray());
    REQUIRE(listed.array().size() == 1);
    CHECK(listed.array()[0].toObject().value(u"id").toString() == u"fake");
    CHECK(listed.array()[0].toObject().value(u"languages").toArray().size() == 2);
    REQUIRE(fixture.engines.engineList().size() == 1);
    CHECK(fixture.engines.engineList()[0].toMap().value(QStringLiteral("title")).toString() == u"Fake");

    fixture.engines.request(kOwn, "speech.voices", 55, QStringLiteral("{}"));
    REQUIRE(pumpUntil([&fixture] { return !fixture.answerTo(55).isEmpty(); }));
    CHECK(fixture.answerTo(55) == uR"([{"id":"robot","language":"en-US","name":"Robot"}])");

    fixture.engines.removeEngine(QStringLiteral("fake"));
    CHECK(fixture.answerTo(52) == u"false");
    CHECK(fixture.answerTo(53) == u"false");
    REQUIRE(fixture.told.size() == 2);
    CHECK(fixture.told[1].id == own);
    CHECK(fixture.told[1].value.value(u"reason").toString() == u"engine");
    CHECK(fixture.engines.call(kOwn, "speech.engines", "{}").json == "[]");
    fixture.say(kOwn, 56, QStringLiteral(R"({"text":"gone"})"));
    CHECK(fixture.refusalOf(56).startsWith(u"GS-API-110 "));
}

TEST_CASE("ADR-0112: the Windows engine reads a text without a sound and hears a command in a recording")
{
    Application app;
    SpeechEngines engines(nullptr);
    std::vector<std::pair<quint64, QString>> answers;
    std::vector<Told> told;
    engines.setReplies(SpeechReplies{
        .answer = [&answers](std::uint64_t, quint64 request, const QString& json) { answers.emplace_back(request, json); },
        .refuse = [&answers](std::uint64_t, quint64 request,
                             const QString& message) { answers.emplace_back(request, message); },
        .tell = [&told](std::uint64_t logic, const QString& kind, const QString& id,
                        const QString& json) { told.push_back({logic, kind, id, QJsonDocument::fromJson(json.toUtf8()).object()}); },
    });
    auto made = std::make_unique<gs::app::SystemSpeechEngine>(gs::platform::SpeechOutput::Nowhere);
    gs::app::SystemSpeechEngine& system = *made;
    const SpeechEngineInfo info = system.info();
    engines.addEngine(std::move(made));
    CHECK(info.id == u"system");
    CHECK(info.title == u"Windows");
    if (!info.say) {
        MESSAGE("no voice on this computer");
        return;
    }
    engines.request(kOwn, "speech.say", 60, QStringLiteral(R"({"text":"测试朗读"})"));
    REQUIRE(pumpUntil([&answers] { return !answers.empty(); }));
    CHECK(answers[0] == std::pair<quint64, QString>{60, QStringLiteral("true")});
    engines.request(kOwn, "speech.say", 61, QStringLiteral(R"({"text":"x","voice":"Nobody at all"})"));
    REQUIRE(pumpUntil([&answers] { return answers.size() == 2; }));
    CHECK(answers[1].second.startsWith(u"GS-API-110 "));

    if (!info.languages.contains(QStringLiteral("zh-CN"))) {
        MESSAGE("no Chinese recognizer on this computer");
        return;
    }
    const std::vector<gs::platform::SpeechVoice> voices = system.speech().voices();
    const auto voice = std::ranges::find(voices, std::string("zh-CN"), &gs::platform::SpeechVoice::language);
    REQUIRE(voice != voices.end());
    const auto pcm = gs::platform::SystemSpeech::record({.text = "打开地图", .voice = voice->id});
    REQUIRE(pcm);
    system.speech().hearFrom(*pcm);
    engines.request(kOwn, "speech.listen", 62,
                    QStringLiteral(R"({"phrases":["关闭背包","打开地图"],"language":"zh-CN"})"));
    REQUIRE(pumpUntil([&told] { return told.size() >= 2; }));
    CHECK(told[0].value.value(u"type").toString() == u"heard");
    CHECK(told[0].value.value(u"phrase").toString() == u"打开地图");
    CHECK(told[1].value.value(u"type").toString() == u"closed");
    CHECK(told[1].value.value(u"reason").toString() == u"engine");
}
