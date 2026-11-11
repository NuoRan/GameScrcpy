#include "AiScripts.h"

#include "AiAssistant.h"
#include "AppSettings.h"
#include "ExtensionHost.h"
#include "VisionSearch.h"

#include <gs/script/Permissions.h>

#include <QBuffer>
#include <QCoreApplication>
#include <QIODevice>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace gs::app {

using namespace Qt::StringLiterals;

namespace {

constexpr double kEdge = 1e-9;

struct Question {
    QString text;
    bool picture = false;
    std::optional<script::Region> region;
    std::chrono::milliseconds wait = AiScripts::kDefaultWait;
};

struct Encoded {
    QByteArray png;
    QSize size;
};

QString jsonText(const QString& text)
{
    const QByteArray wrapped = QJsonDocument(QJsonArray{text}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(wrapped.sliced(1, wrapped.size() - 2));
}

std::string_view permissionName() noexcept
{
    return script::channelRule(AiScripts::kService).permission;
}

QString turnedOff()
{
    return u"GS-API-095 logic may not call AI: turn on \"Let logic call AI\" in Settings > AI"_s;
}

QString notSetUp()
{
    return u"GS-API-095 logic may not call AI: no AI service is set up; fill in Settings > AI and test the "
           u"connection"_s;
}

QString askerOf(const LogicCaller& caller)
{
    QString who = u"logic "_s + (caller.script.isEmpty() ? u"?"_s : caller.script);
    if (!caller.extension.isEmpty()) {
        who += u" of "_s + caller.extension;
    }
    if (!caller.device.isEmpty()) {
        who += u" on "_s + caller.device;
    }
    return who;
}

QString scriptPrompt()
{
    return QStringLiteral(
        "A script that runs in GameScrcpy asks you the question below. GameScrcpy mirrors an Android phone to a "
        "computer and controls it. When a picture comes with the question, it is the phone's screen or a part of it. "
        "Your answer goes back to the script as text: answer only what is asked, as briefly as the question allows, "
        "and without Markdown unless the question asks for it.");
}

bool ratioIn(const QJsonObject& area, const QString& name, double& out)
{
    const QJsonValue value = area.value(name);
    if (!value.isDouble()) {
        return false;
    }
    out = value.toDouble();
    return out >= 0.0 && out <= 1.0;
}

std::optional<Question> questionOf(const QString& json, QString& problem)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        problem = u"GS-API-011 the question must come as a JSON object"_s;
        return std::nullopt;
    }
    const QJsonObject asked = document.object();
    Question made;
    made.text = asked.value(u"question"_s).toString();
    if (made.text.trimmed().isEmpty() || made.text.size() > AiScripts::kMostQuestionChars) {
        problem = u"GS-API-011 the question must be text of 1 to %1 characters that are not all spaces"_s.arg(
            AiScripts::kMostQuestionChars);
        return std::nullopt;
    }
    const QJsonValue wait = asked.value(u"timeoutMs"_s);
    if (!wait.isUndefined() && !wait.isNull()) {
        const double ms = wait.toDouble(-1.0);
        if (!wait.isDouble() || ms < static_cast<double>(AiScripts::kShortestWait.count()) ||
            ms > static_cast<double>(AiScripts::kLongestWait.count())) {
            problem = u"GS-API-003 timeoutMs must be from %1 to %2 milliseconds"_s.arg(AiScripts::kShortestWait.count())
                          .arg(AiScripts::kLongestWait.count());
            return std::nullopt;
        }
        made.wait = std::chrono::milliseconds(std::llround(ms));
    }
    made.picture = asked.value(u"picture"_s).toBool(false);
    const QJsonValue region = asked.value(u"region"_s);
    if (made.picture && region.isObject()) {
        const QJsonObject area = region.toObject();
        script::Region read;
        if (!ratioIn(area, u"x"_s, read.x) || !ratioIn(area, u"y"_s, read.y) || !ratioIn(area, u"width"_s, read.width) ||
            !ratioIn(area, u"height"_s, read.height) || read.x + read.width > 1.0 + kEdge ||
            read.y + read.height > 1.0 + kEdge) {
            problem = u"GS-API-002 the region must be an object inside the picture"_s;
            return std::nullopt;
        }
        made.region = read;
    }
    return made;
}

QImage imageOf(const media::LumaSnapshot& shot)
{
    if (shot.colored()) {
        if (QImage picture = pictureOf(shot); !picture.isNull()) {
            return picture;
        }
    }
    const auto width = static_cast<std::size_t>(std::max(shot.width, 0));
    if (width == 0 || shot.height <= 0 || shot.luma.size() < width * static_cast<std::size_t>(shot.height)) {
        return {};
    }
    QImage grey(shot.width, shot.height, QImage::Format_Grayscale8);
    for (int y = 0; y < shot.height; ++y) {
        std::memcpy(grey.scanLine(y), shot.luma.data() + static_cast<std::size_t>(y) * width, width);
    }
    return grey;
}

Encoded encoded(const AiFrameCapture& capture, const std::optional<script::Region>& region)
{
    const std::shared_ptr<const media::LumaSnapshot> shot = capture();
    if (shot == nullptr) {
        return {};
    }
    QImage image = imageOf(*shot);
    if (image.isNull()) {
        return {};
    }
    if (region) {
        const vision::Rect area = pixelArea(*region, image.width(), image.height());
        if (area.empty()) {
            return {};
        }
        image = image.copy(area.x, area.y, area.width, area.height);
    }
    if (std::max(image.width(), image.height()) > AiScripts::kMostPictureSide) {
        image = image.scaled(QSize(AiScripts::kMostPictureSide, AiScripts::kMostPictureSide), Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    }
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
        return {};
    }
    buffer.close();
    return Encoded{.png = png, .size = image.size()};
}

}

AiScriptHooks aiScriptHooks(const AppSettings& settings, AiAssistant& assistant, const ExtensionHost& extensions,
                            std::function<AiFrameCapture(const QString& serial)> frames)
{
    return AiScriptHooks{
        .allowed = [&settings] { return settings.effective({}).assistant.scripts; },
        .ready = [&assistant] { return assistant.ready(); },
        .provider = [&assistant] { return assistant.provider(); },
        .permitted =
            [&extensions](const QString& extension) {
                const std::vector<std::string> granted = extensions.grantedPermissions(extension.toStdString());
                return std::ranges::any_of(granted, [](const std::string& name) { return name == permissionName(); });
            },
        .frames = std::move(frames),
        .unavailable = [&assistant] { return assistant.unavailable(); }};
}

AiScripts::AiScripts(AiScriptHooks hooks, const Clock& clock, QObject* parent)
    : QObject(parent)
    , m_hooks(std::move(hooks))
    , m_clock(clock)
    , m_replies{.answer =
                    [](std::uint64_t logic, quint64 request, const QString& json) {
                        static_cast<void>(LogicServices::instance().answer(logic, request, json));
                    },
                .refuse =
                    [](std::uint64_t logic, quint64 request, const QString& message) {
                        static_cast<void>(LogicServices::instance().refuse(logic, request, message));
                    }}
{
    m_deadline.setSingleShot(true); // source-rule-allow R9: a question to the AI fails when the time limit the logic gave it runs out
    connect(&m_deadline, &QTimer::timeout, this, &AiScripts::expire);
}

AiScripts::~AiScripts()
{
    if (m_offered) {
        LogicServices::instance().withdraw(kService);
    }
    m_deadline.stop();
    for (auto& entry : m_pending) {
        if (entry.second.provider != nullptr) {
            entry.second.provider->cancel();
        }
    }
}

void AiScripts::offerService()
{
    LogicServices& services = LogicServices::instance();
    services.offerRequest(
        std::string(kService),
        [this](const LogicCaller& caller, quint64 id, const QString& json) { request(caller, id, json); },
        [this](const LogicCaller& caller, quint64 id) { drop(caller, id); });
    connect(&services, &LogicServices::logicEnded, this, [this](quint64 logic) { endLogic(logic); });
    m_offered = true;
}

void AiScripts::setReplies(AiScriptReplies replies)
{
    m_replies = std::move(replies);
}

void AiScripts::request(const LogicCaller& caller, quint64 id, const QString& json)
{
    QString problem;
    const std::optional<Question> asked = questionOf(json, problem);
    if (asked) {
        problem = refusalFor(caller, asked->picture);
    }
    if (!asked || !problem.isEmpty()) {
        qInfo("ai ask from %s: refused, %s", qUtf8Printable(askerOf(caller)),
              qUtf8Printable(problem.section(u' ', 0, 0)));
        refuse(caller.logic, id, problem);
        return;
    }
    const Key key{caller.logic, id};
    m_pending.insert_or_assign(
        key, Pending{.caller = caller,
                     .question = asked->text,
                     .deadline = m_clock.now() + std::chrono::duration_cast<Microseconds>(asked->wait),
                     .wait = asked->wait});
    schedule();
    if (!asked->picture) {
        qInfo("ai ask from %s: %lld characters, no picture", qUtf8Printable(askerOf(caller)),
              static_cast<long long>(asked->text.size()));
        send(key, {});
        return;
    }
    AiFrameCapture capture = m_hooks.frames ? m_hooks.frames(caller.device) : AiFrameCapture();
    if (!capture) {
        unpictured(key);
        return;
    }
    look(key, std::move(capture), asked->region);
}

void AiScripts::drop(const LogicCaller& caller, quint64 id)
{
    const auto found = m_pending.find(Key{caller.logic, id});
    if (found == m_pending.end()) {
        return;
    }
    const std::unique_ptr<AiProvider> provider = std::move(found->second.provider);
    m_pending.erase(found);
    if (provider != nullptr) {
        provider->cancel();
    }
    schedule();
}

void AiScripts::endLogic(std::uint64_t logic)
{
    m_recent.erase(logic);
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (it->first.first != logic) {
            ++it;
            continue;
        }
        if (it->second.provider != nullptr) {
            it->second.provider->cancel();
        }
        it = m_pending.erase(it);
    }
    schedule();
}

void AiScripts::expire()
{
    const Microseconds now = m_clock.now();
    std::vector<Key> due;
    for (const auto& entry : m_pending) {
        if (entry.second.deadline <= now) {
            due.push_back(entry.first);
        }
    }
    for (const Key& key : due) {
        const auto found = m_pending.find(key);
        if (found == m_pending.end()) {
            continue;
        }
        const std::unique_ptr<AiProvider> provider = std::move(found->second.provider);
        const std::chrono::milliseconds wait = found->second.wait;
        qInfo("ai ask from %s: no answer within %lld ms", qUtf8Printable(askerOf(found->second.caller)),
              static_cast<long long>(wait.count()));
        m_pending.erase(found);
        if (provider != nullptr) {
            provider->cancel();
        }
        refuse(key.first, key.second,
               u"GS-API-097 the AI service failed: it gave no answer within %1 ms"_s.arg(wait.count()));
    }
    schedule();
}

QString AiScripts::refusalFor(const LogicCaller& caller, bool picture)
{
    if (!caller.extension.isEmpty() && !(m_hooks.permitted && m_hooks.permitted(caller.extension))) {
        return QString::fromStdString(script::permissionProblem(permissionName()));
    }
    if (!(m_hooks.allowed && m_hooks.allowed())) {
        return turnedOff();
    }
    if (m_hooks.unavailable) {
        if (QString problem = m_hooks.unavailable(); !problem.isEmpty()) {
            return problem;
        }
    }
    if (!(m_hooks.ready && m_hooks.ready())) {
        return notSetUp();
    }
    if (picture && caller.device.isEmpty()) {
        return QString::fromStdString(script::phoneProblem());
    }
    if (!admit(caller.logic)) {
        return u"GS-API-096 this logic called AI %1 times in the last minute"_s.arg(kMostCallsPerMinute);
    }
    return {};
}

bool AiScripts::admit(std::uint64_t logic)
{
    const Microseconds now = m_clock.now();
    std::deque<Microseconds>& recent = m_recent[logic];
    while (!recent.empty() && now - recent.front() >= kCallWindow) {
        recent.pop_front();
    }
    if (recent.size() >= kMostCallsPerMinute) {
        return false;
    }
    recent.push_back(now);
    return true;
}

void AiScripts::look(const Key& key, AiFrameCapture capture, std::optional<script::Region> region)
{
    const QPointer<AiScripts> self(this);
    QThreadPool::globalInstance()->start([self, key, capture = std::move(capture), region = std::move(region)] {
        const Encoded made = encoded(capture, region);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, key, made] {
                if (self != nullptr) {
                    self->pictured(key, made.png, made.size);
                }
            },
            Qt::QueuedConnection);
    });
}

void AiScripts::pictured(const Key& key, const QByteArray& png, QSize size)
{
    const auto found = m_pending.find(key);
    if (found == m_pending.end()) {
        return;
    }
    if (png.isEmpty()) {
        unpictured(key);
        return;
    }
    qInfo("ai ask from %s: %lld characters, a %d x %d picture", qUtf8Printable(askerOf(found->second.caller)),
          static_cast<long long>(found->second.question.size()), size.width(), size.height());
    send(key, png);
}

void AiScripts::unpictured(const Key& key)
{
    const auto found = m_pending.find(key);
    if (found == m_pending.end()) {
        return;
    }
    qInfo("ai ask from %s: %lld characters, no picture of the phone yet, so nothing was asked",
          qUtf8Printable(askerOf(found->second.caller)), static_cast<long long>(found->second.question.size()));
    m_pending.erase(found);
    schedule();
    answer(key.first, key.second, u"null"_s);
}

void AiScripts::send(const Key& key, const QByteArray& png)
{
    const auto found = m_pending.find(key);
    if (found == m_pending.end()) {
        return;
    }
    std::unique_ptr<AiProvider> provider = m_hooks.provider ? m_hooks.provider() : nullptr;
    if (provider == nullptr) {
        m_pending.erase(found);
        schedule();
        const QString gone = m_hooks.unavailable ? m_hooks.unavailable() : QString();
        refuse(key.first, key.second, gone.isEmpty() ? notSetUp() : gone);
        return;
    }
    AiTurn turn{.role = AiTurn::Role::User, .text = found->second.question};
    if (!png.isEmpty()) {
        turn.pictures.append(QJsonObject{{u"type"_s, u"image"_s},
                                         {u"data"_s, QString::fromLatin1(png.toBase64())},
                                         {u"mimeType"_s, u"image/png"_s}});
    }
    AiRequest asked{.system = scriptPrompt(), .stream = false, .maxTokens = kMostTokens};
    asked.turns.push_back(std::move(turn));
    AiProvider* sending = provider.get();
    found->second.provider = std::move(provider);
    const QPointer<AiScripts> self(this);
    sending->send(asked, AiProvider::Handlers{.text = {}, .done = [self, key](AiReply reply) {
                                                  if (self != nullptr) {
                                                      self->answered(key, std::move(reply));
                                                  }
                                              }});
}

void AiScripts::answered(const Key& key, AiReply reply)
{
    const auto found = m_pending.find(key);
    if (found == m_pending.end()) {
        return;
    }
    std::shared_ptr<AiProvider> finished(std::move(found->second.provider));
    m_pending.erase(found);
    schedule();
    QMetaObject::invokeMethod(this, [finished]() mutable { finished.reset(); }, Qt::QueuedConnection);
    if (!reply.problem.isEmpty()) {
        refuse(key.first, key.second, u"GS-API-097 the AI service failed: "_s + reply.problem);
        return;
    }
    answer(key.first, key.second, reply.text.isEmpty() ? u"null"_s : jsonText(reply.text));
}

void AiScripts::schedule()
{
    std::optional<Microseconds> next;
    for (const auto& entry : m_pending) {
        next = next ? std::min(*next, entry.second.deadline) : entry.second.deadline;
    }
    if (!next) {
        m_deadline.stop();
        return;
    }
    const Microseconds left = std::max(*next - m_clock.now(), Microseconds{0});
    m_deadline.start(std::chrono::ceil<std::chrono::milliseconds>(left));
}

void AiScripts::answer(std::uint64_t logic, quint64 request, const QString& json) const
{
    if (m_replies.answer) {
        m_replies.answer(logic, request, json);
    }
}

void AiScripts::refuse(std::uint64_t logic, quint64 request, const QString& message) const
{
    if (m_replies.refuse) {
        m_replies.refuse(logic, request, message);
    }
}

}
