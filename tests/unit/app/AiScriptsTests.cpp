#include "AiAssistant.h"
#include "AiScripts.h"
#include "AiService.h"
#include "AppSettings.h"
#include "ExtensionHost.h"
#include "LogicServices.h"
#include "McpTestSupport.h"
#include "Settings.h"

#include "unit/ext/TestFiles.h"

#include <gs/base/Clock.h>
#include <gs/media/FrameSnapshot.h>

#include <doctest/doctest.h>

#include <QByteArray>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSize>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using gs::app::AiProvider;
using gs::app::AiReply;
using gs::app::AiRequest;
using gs::app::AiScriptHooks;
using gs::app::AiScriptReplies;
using gs::app::AiScripts;
using gs::app::AiTurn;
using gs::app::LogicCaller;
using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace {

struct Service {
    std::vector<AiRequest> requests;
    std::vector<AiProvider::Handlers> waiting;
    int providers = 0;
    int cancels = 0;
    bool answerAtOnce = false;
};

class FakeProvider final : public AiProvider
{
public:
    explicit FakeProvider(std::shared_ptr<Service> service)
        : m_service(std::move(service))
    {
    }

    void send(const AiRequest& request, Handlers handlers) override
    {
        m_service->requests.push_back(request);
        if (m_service->answerAtOnce) {
            handlers.done(AiReply{.text = u"A red door."_s, .stop = u"stop"_s});
            return;
        }
        m_service->waiting.push_back(std::move(handlers));
    }

    void cancel() override { ++m_service->cancels; }

private:
    std::shared_ptr<Service> m_service;
};

std::shared_ptr<const gs::media::LumaSnapshot> halves(int width, int height)
{
    auto shot = std::make_shared<gs::media::LumaSnapshot>();
    shot->width = width;
    shot->height = height;
    shot->luma.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), std::uint8_t{0});
    std::fill_n(shot->luma.begin(), static_cast<std::size_t>(width) * static_cast<std::size_t>(height / 2),
                std::uint8_t{255});
    return shot;
}

QString asked(const QString& question, const QJsonValue& picture = false, const QJsonValue& region = QJsonValue(),
              int timeoutMs = 60000)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject{{u"question"_s, question},
                                                       {u"picture"_s, picture},
                                                       {u"region"_s, region},
                                                       {u"timeoutMs"_s, timeoutMs}})
                                 .toJson(QJsonDocument::Compact));
}

QImage pictureIn(const AiRequest& request)
{
    if (request.turns.empty() || request.turns.front().pictures.isEmpty()) {
        return {};
    }
    const QJsonObject item = request.turns.front().pictures.at(0).toObject();
    if (item.value(u"type"_s).toString() != u"image" || item.value(u"mimeType"_s).toString() != u"image/png") {
        return {};
    }
    return QImage::fromData(QByteArray::fromBase64(item.value(u"data"_s).toString().toLatin1()), "PNG");
}

const LogicCaller kMine{.logic = 1, .script = u"shooter"_s, .device = u"R58"_s};
const LogicCaller kDeck{.logic = 2, .extension = u"com.example.deck"_s, .script = u"helper"_s, .device = u"R58"_s};
const LogicCaller kTools{.logic = 3, .extension = u"com.example.deck"_s, .script = u"tools"_s};

struct Fixture {
    gs::test::McpApplication application;
    gs::ManualClock clock{gs::Microseconds{5'000'000}};
    std::shared_ptr<Service> service = std::make_shared<Service>();
    bool allowed = true;
    bool ready = true;
    QStringList permitted{u"com.example.deck"_s};
    std::shared_ptr<const gs::media::LumaSnapshot> shot = halves(400, 2400);
    QStringList framed;
    std::vector<std::pair<quint64, QString>> answers;
    std::vector<std::pair<quint64, QString>> refusals;
    AiScripts scripts{AiScriptHooks{.allowed = [this] { return allowed; },
                                    .ready = [this] { return ready; },
                                    .provider = [this]() -> std::unique_ptr<AiProvider> {
                                        ++service->providers;
                                        return std::make_unique<FakeProvider>(service);
                                    },
                                    .permitted = [this](const QString& extension) { return permitted.contains(extension); },
                                    .frames = [this](const QString& serial) -> gs::app::AiFrameCapture {
                                        framed.append(serial);
                                        if (shot == nullptr) {
                                            return {};
                                        }
                                        return [picture = shot] { return picture; };
                                    }},
                      clock};

    Fixture()
    {
        scripts.setReplies(AiScriptReplies{
            .answer = [this](std::uint64_t, quint64 request, const QString& json) { answers.emplace_back(request, json); },
            .refuse = [this](std::uint64_t, quint64 request,
                             const QString& message) { refusals.emplace_back(request, message); }});
    }
};

}

TEST_CASE("SCR-022: logic may not call AI while the setting is off or no AI service is set up (GS-API-095)")
{
    Fixture fixture;
    fixture.allowed = false;
    fixture.scripts.request(kMine, 1, asked(u"Is it day?"_s));
    REQUIRE(fixture.refusals.size() == 1);
    CHECK(fixture.refusals[0].first == 1);
    CHECK(fixture.refusals[0].second ==
          u"GS-API-095 logic may not call AI: turn on \"Let logic call AI\" in Settings > AI"_s);

    fixture.allowed = true;
    fixture.ready = false;
    fixture.scripts.request(kMine, 2, asked(u"Is it day?"_s, true));
    REQUIRE(fixture.refusals.size() == 2);
    CHECK(fixture.refusals[1].first == 2);
    CHECK(fixture.refusals[1].second.startsWith(u"GS-API-095 logic may not call AI: no AI service is set up"_s));
    CHECK(fixture.service->providers == 0);
    CHECK(fixture.framed.isEmpty());
    CHECK(fixture.scripts.pending() == 0);

    fixture.ready = true;
    fixture.scripts.request(kMine, 3, asked(u"   "_s));
    fixture.scripts.request(kMine, 4, u"[1, 2]"_s);
    fixture.scripts.request(kMine, 5, asked(u"Hi"_s, false, QJsonValue(), 999));
    fixture.scripts.request(kMine, 6,
                            asked(u"Hi"_s, true, QJsonObject{{u"x"_s, 0.6}, {u"y"_s, 0}, {u"width"_s, 0.5}, {u"height"_s, 1}}));
    REQUIRE(fixture.refusals.size() == 6);
    CHECK(fixture.refusals[2].second.startsWith(u"GS-API-011 "_s));
    CHECK(fixture.refusals[3].second.startsWith(u"GS-API-011 "_s));
    CHECK(fixture.refusals[4].second.startsWith(u"GS-API-003 "_s));
    CHECK(fixture.refusals[5].second.startsWith(u"GS-API-002 "_s));
    CHECK(fixture.service->providers == 0);
}

TEST_CASE("SCR-022: each logic asks at most 10 times a minute; the 11th question within the minute gets GS-API-096")
{
    Fixture fixture;
    fixture.service->answerAtOnce = true;
    for (quint64 id = 1; id <= 10; ++id) {
        fixture.scripts.request(kMine, id, asked(u"Again?"_s));
        fixture.clock.advance(1s);
    }
    CHECK(fixture.answers.size() == 10);
    fixture.scripts.request(kMine, 11, asked(u"Again?"_s));
    REQUIRE(fixture.refusals.size() == 1);
    CHECK(fixture.refusals[0].first == 11);
    CHECK(fixture.refusals[0].second == u"GS-API-096 this logic called AI 10 times in the last minute"_s);
    CHECK(fixture.service->providers == 10);

    fixture.scripts.request(kDeck, 12, asked(u"Mine?"_s));
    CHECK(fixture.answers.size() == 11);

    fixture.clock.advance(49s);
    fixture.scripts.request(kMine, 13, asked(u"Again?"_s));
    CHECK(fixture.refusals.size() == 2);
    fixture.clock.advance(1s);
    fixture.scripts.request(kMine, 14, asked(u"Again?"_s));
    REQUIRE(fixture.answers.size() == 12);
    CHECK(fixture.answers.back().first == 14);

    fixture.scripts.endLogic(kMine.logic);
    fixture.scripts.request(kMine, 15, asked(u"Again?"_s));
    CHECK(fixture.answers.size() == 13);
}

TEST_CASE("SCR-022: the logic of an extension asks only with the permission ai; the user's own logic needs none")
{
    Fixture fixture;
    fixture.service->answerAtOnce = true;
    fixture.permitted.clear();
    fixture.scripts.request(kDeck, 1, asked(u"Hi?"_s));
    REQUIRE(fixture.refusals.size() == 1);
    CHECK(fixture.refusals[0].first == 1);
    CHECK(fixture.refusals[0].second.startsWith(u"GS-API-072 this needs the permission \"ai\""_s));
    CHECK(fixture.service->providers == 0);

    fixture.scripts.request(kMine, 2, asked(u"Hi?"_s));
    REQUIRE(fixture.answers.size() == 1);
    CHECK(fixture.answers[0].first == 2);

    fixture.permitted = QStringList{u"com.example.deck"_s};
    fixture.scripts.request(kDeck, 3, asked(u"Hi?"_s));
    REQUIRE(fixture.answers.size() == 2);
    CHECK(fixture.answers[1].first == 3);
    CHECK(fixture.answers[1].second == u"\"A red door.\""_s);

    fixture.allowed = false;
    fixture.scripts.request(kDeck, 4, asked(u"Hi?"_s));
    REQUIRE(fixture.refusals.size() == 2);
    CHECK(fixture.refusals[1].second.startsWith(u"GS-API-095 "_s));
}

TEST_CASE("SCR-022: the service's text comes back as a JSON string and no text as null; a failure, a time-out and a "
          "withdrawn question end the call")
{
    Fixture fixture;
    fixture.scripts.request(kMine, 1, asked(u"What colour is the door?"_s));
    REQUIRE(fixture.service->waiting.size() == 1);
    const AiRequest sent = fixture.service->requests.at(0);
    CHECK_FALSE(sent.stream);
    CHECK(sent.maxTokens == AiScripts::kMostTokens);
    CHECK(sent.tools.isEmpty());
    CHECK(sent.system.contains(u"GameScrcpy"_s));
    REQUIRE(sent.turns.size() == 1);
    CHECK(sent.turns[0].role == AiTurn::Role::User);
    CHECK(sent.turns[0].text == u"What colour is the door?"_s);
    CHECK(sent.turns[0].pictures.isEmpty());
    CHECK(fixture.framed.isEmpty());

    fixture.service->waiting.at(0).done(AiReply{.text = u"Red, \"dark\" red.\n"_s, .stop = u"stop"_s});
    REQUIRE(fixture.answers.size() == 1);
    CHECK(fixture.answers[0].first == 1);
    CHECK(fixture.answers[0].second == uR"("Red, \"dark\" red.\n")"_s);
    CHECK(fixture.scripts.pending() == 0);

    fixture.scripts.request(kMine, 2, asked(u"Anything?"_s));
    fixture.service->waiting.at(1).done(AiReply{.stop = u"stop"_s});
    REQUIRE(fixture.answers.size() == 2);
    CHECK(fixture.answers[1].first == 2);
    CHECK(fixture.answers[1].second == u"null"_s);

    fixture.scripts.request(kMine, 3, asked(u"Again?"_s));
    fixture.service->waiting.at(2).done(AiReply{.status = 401, .problem = u"The service did not accept the API key."_s});
    REQUIRE(fixture.refusals.size() == 1);
    CHECK(fixture.refusals[0].first == 3);
    CHECK(fixture.refusals[0].second == u"GS-API-097 the AI service failed: The service did not accept the API key."_s);

    fixture.scripts.request(kMine, 4, asked(u"Slow?"_s, false, QJsonValue(), 1000));
    REQUIRE(fixture.scripts.pending() == 1);
    fixture.clock.advance(999ms);
    fixture.scripts.expire();
    CHECK(fixture.scripts.pending() == 1);
    fixture.clock.advance(1ms);
    fixture.scripts.expire();
    CHECK(fixture.scripts.pending() == 0);
    CHECK(fixture.service->cancels == 1);
    REQUIRE(fixture.refusals.size() == 2);
    CHECK(fixture.refusals[1].first == 4);
    CHECK(fixture.refusals[1].second == u"GS-API-097 the AI service failed: it gave no answer within 1000 ms"_s);
    fixture.service->waiting.at(3).done(AiReply{.text = u"Late"_s, .stop = u"stop"_s});
    CHECK(fixture.answers.size() == 2);

    fixture.scripts.request(kMine, 5, asked(u"Never mind"_s));
    fixture.scripts.drop(kMine, 5);
    CHECK(fixture.service->cancels == 2);
    CHECK(fixture.scripts.pending() == 0);
    fixture.service->waiting.at(4).done(AiReply{.text = u"Too late"_s, .stop = u"stop"_s});
    CHECK(fixture.answers.size() == 2);
    CHECK(fixture.refusals.size() == 2);

    fixture.scripts.request(kMine, 6, asked(u"Still there?"_s));
    fixture.scripts.request(kDeck, 7, asked(u"Mine?"_s));
    REQUIRE(fixture.scripts.pending() == 2);
    fixture.scripts.endLogic(kMine.logic);
    CHECK(fixture.scripts.pending() == 1);
    CHECK(fixture.service->cancels == 3);
}

TEST_CASE("SCR-022: a picture goes along only when asked: that part of the phone's picture, at most 1024 pixels on "
          "its long side")
{
    Fixture fixture;
    fixture.scripts.request(
        kMine, 1,
        asked(u"What is at the bottom?"_s, true, QJsonObject{{u"x"_s, 0}, {u"y"_s, 0.5}, {u"width"_s, 1}, {u"height"_s, 0.5}}));
    REQUIRE(gs::test::waitUntil([&fixture] { return fixture.service->requests.size() == 1; }));
    CHECK(fixture.framed == QStringList{u"R58"_s});
    const QImage bottom = pictureIn(fixture.service->requests.at(0));
    REQUIRE_FALSE(bottom.isNull());
    CHECK(bottom.height() == AiScripts::kMostPictureSide);
    CHECK(bottom.width() < bottom.height());
    CHECK(qGray(bottom.pixel(bottom.width() / 2, 0)) < 16);
    CHECK(qGray(bottom.pixel(bottom.width() / 2, bottom.height() - 1)) < 16);

    fixture.scripts.request(kMine, 2, asked(u"And all of it?"_s, true));
    REQUIRE(gs::test::waitUntil([&fixture] { return fixture.service->requests.size() == 2; }));
    const QImage whole = pictureIn(fixture.service->requests.at(1));
    REQUIRE_FALSE(whole.isNull());
    CHECK(std::max(whole.width(), whole.height()) == AiScripts::kMostPictureSide);
    CHECK(qGray(whole.pixel(whole.width() / 2, 4)) > 240);
    CHECK(qGray(whole.pixel(whole.width() / 2, whole.height() - 5)) < 16);

    fixture.shot = halves(300, 200);
    fixture.scripts.request(kMine, 3, asked(u"Small?"_s, true));
    REQUIRE(gs::test::waitUntil([&fixture] { return fixture.service->requests.size() == 3; }));
    const QImage small = pictureIn(fixture.service->requests.at(2));
    REQUIRE_FALSE(small.isNull());
    CHECK(small.size() == QSize(300, 200));

    fixture.scripts.request(kMine, 4, asked(u"No picture, please"_s));
    REQUIRE(fixture.service->requests.size() == 4);
    CHECK(fixture.service->requests.at(3).turns.at(0).pictures.isEmpty());
    CHECK(fixture.framed.size() == 3);
}

TEST_CASE("SCR-022: logic without a phone may not send a picture (GS-API-073), and before the phone shows a picture the "
          "answer is null without asking")
{
    Fixture fixture;
    fixture.scripts.request(kTools, 1, asked(u"What do you see?"_s, true));
    REQUIRE(fixture.refusals.size() == 1);
    CHECK(fixture.refusals[0].second.startsWith(u"GS-API-073 "_s));
    fixture.scripts.request(kTools, 2, asked(u"Just text"_s));
    CHECK(fixture.service->requests.size() == 1);

    fixture.shot = nullptr;
    fixture.scripts.request(kMine, 3, asked(u"What do you see?"_s, true));
    REQUIRE(fixture.answers.size() == 1);
    CHECK(fixture.answers[0].first == 3);
    CHECK(fixture.answers[0].second == u"null"_s);
    CHECK(fixture.service->requests.size() == 1);
    CHECK(fixture.scripts.pending() == 1);
}

TEST_CASE("SCR-022: a picture rides in the user's turn for both kinds of service")
{
    const QJsonObject picture{{u"type"_s, u"image"_s}, {u"data"_s, u"AAAA"_s}, {u"mimeType"_s, u"image/png"_s}};
    AiRequest request{.turns = {AiTurn{.role = AiTurn::Role::User, .text = u"What is this?"_s, .pictures = QJsonArray{picture}}},
                      .stream = false};
    CHECK(gs::app::aiPictureCount(request) == 1);

    const QJsonArray content = gs::app::aiRequestJson(gs::app::AiKind::OpenAi, request)
                                   .value(u"messages"_s)
                                   .toArray()
                                   .at(0)
                                   .toObject()
                                   .value(u"content"_s)
                                   .toArray();
    REQUIRE(content.size() == 2);
    CHECK(content.at(0).toObject().value(u"text"_s).toString() == u"What is this?"_s);
    CHECK(content.at(1).toObject().value(u"image_url"_s).toObject().value(u"url"_s).toString() ==
          u"data:image/png;base64,AAAA"_s);

    const QJsonArray blocks = gs::app::aiRequestJson(gs::app::AiKind::Anthropic, request)
                                  .value(u"messages"_s)
                                  .toArray()
                                  .at(0)
                                  .toObject()
                                  .value(u"content"_s)
                                  .toArray();
    REQUIRE(blocks.size() == 2);
    CHECK(blocks.at(0).toObject().value(u"type"_s).toString() == u"image"_s);
    CHECK(blocks.at(0).toObject().value(u"source"_s).toObject().value(u"data"_s).toString() == u"AAAA"_s);
    CHECK(blocks.at(1).toObject().value(u"text"_s).toString() == u"What is this?"_s);

    request.pictures = false;
    CHECK(gs::app::aiPictureCount(request) == 0);
    const QJsonArray without = gs::app::aiRequestJson(gs::app::AiKind::OpenAi, request)
                                   .value(u"messages"_s)
                                   .toArray()
                                   .at(0)
                                   .toObject()
                                   .value(u"content"_s)
                                   .toArray();
    REQUIRE(without.size() == 2);
    CHECK(without.at(1).toObject().value(u"type"_s).toString() == u"text"_s);

    const AiRequest plain{.turns = {AiTurn{.role = AiTurn::Role::User, .text = u"Hi"_s}}};
    CHECK(gs::app::aiRequestJson(gs::app::AiKind::OpenAi, plain)
              .value(u"messages"_s)
              .toArray()
              .at(0)
              .toObject()
              .value(u"content"_s)
              .toString() == u"Hi"_s);
}

TEST_CASE("SCR-022: Let logic call AI is off by default on the AI assistant page, and the program's hooks follow it, "
          "the AI connection and the permission ai of an extension")
{
    namespace settings = gs::app::settings;
    const settings::Entry* entry = settings::findSetting("assistant.scripts");
    REQUIRE(entry != nullptr);
    CHECK(entry->kind == settings::Kind::Boolean);
    CHECK(entry->page == settings::Page::Assistant);
    CHECK(entry->defaultValue == settings::Value(false));
    CHECK(entry->facets.live);
    CHECK(entry->facets.title == "Let logic call AI");

    gs::test::McpApplication application;
    const gs::ext::test::Scratch scratch;
    gs::ext::test::writeFile(scratch / "user/alice.asker/1.0.0/manifest.json",
                             R"({"schemaVersion": 1, "id": "alice.asker", "version": "1.0.0", "name": "Asker",
                                 "license": "MIT", "requires": {"script": "^1.0"},
                                 "contributes": {"scripts": [{"name": "helper", "entry": "helper.ts", "run": "app"}]},
                                 "permissions": [{"name": "ai", "reason": "Asks about the picture."}]})");
    gs::ext::test::writeFile(scratch / "user/alice.asker/1.0.0/helper.ts", "export default async function () {}\n");
    gs::app::AppSettings appSettings(scratch / "data");
    gs::app::ExtensionHost extensions(appSettings, {scratch / "user", {}});
    extensions.start(false);
    gs::app::AiAssistant assistant(appSettings, u"GameScrcpy/test-ai-scripts/"_s);
    const AiScriptHooks hooks = gs::app::aiScriptHooks(appSettings, assistant, extensions, {});

    CHECK_FALSE(hooks.allowed());
    REQUIRE(appSettings.setValue(u"assistant.scripts"_s, true));
    CHECK(hooks.allowed());
    CHECK_FALSE(hooks.ready());
    CHECK(hooks.provider() == nullptr);
    REQUIRE(appSettings.setValue(u"assistant.openai.model"_s, u"gpt-test"_s));
    CHECK(hooks.ready());
    CHECK(hooks.provider() != nullptr);

    CHECK_FALSE(hooks.permitted(u"alice.asker"_s));
    REQUIRE(extensions.setEnabled("alice.asker", true));
    CHECK(hooks.permitted(u"alice.asker"_s));
    CHECK_FALSE(hooks.permitted(u"bob.other"_s));
    CHECK(hooks.frames == nullptr);
}

TEST_CASE("SCR-022: the program offers ai.ask on the service channel while it runs, and refuses it with no hooks")
{
    gs::test::McpApplication application;
    const gs::ManualClock clock;
    std::vector<QString> refusals;
    {
        AiScripts scripts(AiScriptHooks{}, clock);
        scripts.setReplies(AiScriptReplies{.answer = {}, .refuse = [&refusals](std::uint64_t, quint64,
                                                                               const QString& message) {
                                               refusals.push_back(message);
                                           }});
        CHECK_FALSE(gs::app::LogicServices::instance().offers(AiScripts::kService));
        scripts.offerService();
        CHECK(gs::app::LogicServices::instance().offers(AiScripts::kService));
        scripts.request(kMine, 1, asked(u"Hi"_s));
    }
    CHECK_FALSE(gs::app::LogicServices::instance().offers(AiScripts::kService));
    REQUIRE(refusals.size() == 1);
    CHECK(refusals[0].startsWith(u"GS-API-095 "_s));
}
