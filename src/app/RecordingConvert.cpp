#include "RecordingConvert.h"

#include "InputHost.h"
#include "QtConversions.h"
#include "ScriptWorkbenchFiles.h"
#include "TaskLibrary.h"

#include <gs/input/Geometry.h>
#include <gs/input/Profile.h>
#include <gs/platform/AtomicFile.h>
#include <gs/record/RecordedVision.h>
#include <gs/record/Recording.h>
#include <gs/script/ApiFiles.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>

namespace gs::app {

namespace {

constexpr qint64 kRoundMs = 10;
constexpr qint64 kDefaultTapMs = 60;
constexpr qint64 kShortTapMs = 200;
constexpr qint64 kLongestHoldMs = 60'000;
constexpr Microseconds kSlack{30'000};
constexpr Microseconds kScrollGap{500'000};
constexpr double kSwipeShare = 0.02;
constexpr double kNearShare = 0.06;
constexpr std::size_t kMostProfileBytes = std::size_t{8} << 20;
constexpr std::string_view kCustomType = "component";
constexpr std::array<std::string_view, 7> kDrivenTypes{"wasd",      "rightWalk", "padStick", "camera",
                                                       "padCamera", "freeLook",  "skill"};

struct Placed {
    std::string id;
    std::string name;
    std::string type;
    input::Vec2 at;
};

struct Press {
    std::string id;
    Microseconds from{0};
    Microseconds to{0};
};

struct Gesture {
    Microseconds from{0};
    Microseconds to{0};
    double x = 0.0;
    double y = 0.0;
    double moved = 0.0;
    bool claimed = false;
    bool used = false;
};

struct Step {
    qint64 at = 0;
    qint64 busy = 0;
    QString call;
    bool waits = true;
};

std::filesystem::path toPath(const QString& text)
{
    return {text.toStdWString()};
}

qint64 roundedMs(Microseconds at)
{
    return std::llround(static_cast<double>(at.count()) / (1000.0 * kRoundMs)) * kRoundMs;
}

QString ratioText(double value)
{
    QString text = QString::number(std::clamp(value, 0.0, 1.0), 'f', 4);
    while (text.endsWith(u'0')) {
        text.chop(1);
    }
    if (text.endsWith(u'.')) {
        text.chop(1);
    }
    return text;
}

QString jsText(const QString& text)
{
    QString quoted = QStringLiteral("\"");
    for (const QChar c : text) {
        if (c == u'\\' || c == u'"') {
            quoted += u'\\';
            quoted += c;
        } else if (c == u'\n') {
            quoted += QStringLiteral("\\n");
        } else if (c.unicode() < 0x20) {
            quoted += QStringLiteral("\\u%1").arg(static_cast<int>(c.unicode()), 4, 16, QChar(u'0'));
        } else {
            quoted += c;
        }
    }
    quoted += u'"';
    return quoted;
}

Step logStep(qint64 at, const QString& text)
{
    return Step{.at = at, .busy = 0, .call = QStringLiteral("log.info(%1)").arg(jsText(text)), .waits = false};
}

Step tapStep(const Gesture& gesture)
{
    const qint64 at = roundedMs(gesture.from);
    const qint64 held = std::clamp(roundedMs(gesture.to) - at, kRoundMs, kLongestHoldMs);
    if (held > kShortTapMs) {
        return Step{.at = at,
                    .busy = held,
                    .call = QStringLiteral("touch.tap(%1, %2, %3)")
                                .arg(ratioText(gesture.x), ratioText(gesture.y), QString::number(held))};
    }
    return Step{.at = at,
                .busy = kDefaultTapMs,
                .call = QStringLiteral("touch.tap(%1, %2)").arg(ratioText(gesture.x), ratioText(gesture.y))};
}

Step swipeStep(const Gesture& gesture)
{
    return logStep(roundedMs(gesture.from),
                   QCoreApplication::translate("RecordingConvert", "A swipe was here; swipes are not turned into steps"));
}

QString codeOf(std::vector<Step> steps)
{
    std::ranges::stable_sort(steps, {}, &Step::at);
    QString body;
    const auto add = [&body](const QString& statement) {
        body += QStringLiteral("        ") + statement + QStringLiteral(";\n");
    };
    qint64 now = 0;
    qint64 pending = 0;
    for (std::size_t index = 0; index < steps.size(); ++index) {
        const Step& step = steps[index];
        if (step.at > now) {
            add(QStringLiteral("await sleep(%1)").arg(step.at - now));
            now = step.at;
        }
        if (!step.waits) {
            add(step.call);
            continue;
        }
        const qint64 next = index + 1 < steps.size() ? steps[index + 1].at : std::numeric_limits<qint64>::max();
        const qint64 ends = step.at + step.busy;
        if (ends <= next) {
            add(QStringLiteral("await ") + step.call);
            now = ends;
        } else {
            add(step.call);
            pending = std::max(pending, ends);
        }
    }
    if (pending > now) {
        add(QStringLiteral("await sleep(%1)").arg(pending - now));
    }
    return QStringLiteral("export default class RecordedActions extends Task {\n"
                          "    async run() {\n")
           + body + QStringLiteral("    }\n}\n");
}

}

QString recordingTitle(const QString& recording)
{
    QString name = QFileInfo(QDir::cleanPath(QDir::fromNativeSeparators(recording))).fileName();
    const QString suffix = toQString(record::kRecordingExtension);
    if (name.endsWith(suffix, Qt::CaseInsensitive)) {
        name.chop(suffix.size());
    }
    return name;
}

std::optional<QString> taskCodeFor(const script::Session& session, const input::Profile* profile)
{
    const double width = static_cast<double>(std::max(session.device.width, 1));
    const double height = static_cast<double>(std::max(session.device.height, 1));
    const double swipePixels = kSwipeShare * std::min(width, height);
    const double nearPixels = kNearShare * std::min(width, height);
    const auto distance = [width, height](double x, double y, input::Vec2 to) {
        return std::hypot((x - to.x) * width, (y - to.y) * height);
    };

    std::vector<Placed> placed;
    if (profile != nullptr) {
        const input::CoordinateMapper mapper(input::ScreenGeometry{.width = width, .height = height},
                                             profile->screen.scaleMode, profile->screen.safeArea);
        for (const input::Control& control : profile->controls) {
            placed.push_back(Placed{.id = control.id,
                                    .name = control.name,
                                    .type = std::string(input::kControlTypeNames[control.kind.index()]),
                                    .at = mapper.position(control.anchor, control.offset)});
        }
    }

    Microseconds end{session.durationMs * 1000};
    for (const script::SessionEvent& event : session.events) {
        end = std::max(end, event.at);
    }
    for (const script::SessionOperation& operation : session.operations) {
        end = std::max(end, operation.at);
    }

    std::vector<Press> presses;
    std::map<std::string, Microseconds, std::less<>> down;
    for (const script::SessionEvent& event : session.events) {
        if (event.kind == script::SessionEventKind::Press) {
            down.try_emplace(event.id, event.at);
        } else if (event.kind == script::SessionEventKind::Release) {
            if (const auto found = down.find(event.id); found != down.end()) {
                presses.push_back(Press{.id = event.id, .from = found->second, .to = event.at});
                down.erase(found);
            }
        }
    }
    for (const auto& [id, from] : down) {
        presses.push_back(Press{.id = id, .from = from, .to = end});
    }
    std::ranges::stable_sort(presses, {}, &Press::from);
    std::vector<const Placed*> pressed;
    for (const Press& press : presses) {
        const auto found = std::ranges::find(placed, press.id, &Placed::id);
        pressed.push_back(found != placed.end() ? &*found : nullptr);
    }

    std::vector<Gesture> gestures;
    std::map<int, std::size_t> active;
    for (const script::SessionOperation& operation : session.operations) {
        const auto* touch = std::get_if<script::SessionTouch>(&operation.action);
        if (touch == nullptr) {
            continue;
        }
        for (const script::SessionTouchPoint& point : touch->points) {
            if (point.pointer >= InputHost::kEnginePointers) {
                continue;
            }
            const auto open = active.find(point.pointer);
            if (point.action == script::SessionTouchAction::Down) {
                if (open != active.end()) {
                    gestures[open->second].to = operation.at;
                }
                active.insert_or_assign(point.pointer, gestures.size());
                gestures.push_back(Gesture{.from = operation.at, .to = operation.at, .x = point.x, .y = point.y});
                continue;
            }
            if (open == active.end()) {
                continue;
            }
            Gesture& gesture = gestures[open->second];
            gesture.moved = std::max(gesture.moved, distance(point.x, point.y, {gesture.x, gesture.y}));
            if (point.action == script::SessionTouchAction::Up || point.action == script::SessionTouchAction::Cancel) {
                gesture.to = operation.at;
                active.erase(open);
            }
        }
    }
    for (const auto& open : active) {
        gestures[open.second].to = end;
    }

    std::vector<std::optional<std::size_t>> firstTouch(presses.size());
    for (std::size_t index = 0; index < gestures.size(); ++index) {
        Gesture& gesture = gestures[index];
        for (std::size_t which = 0; which < presses.size(); ++which) {
            const Press& press = presses[which];
            if (gesture.from + kSlack >= press.from && gesture.from <= press.from + kSlack) {
                gesture.claimed = true;
                if (!firstTouch[which]) {
                    firstTouch[which] = index;
                }
                continue;
            }
            const Placed* known = pressed[which];
            if (known != nullptr && gesture.from + kSlack >= press.from && gesture.from <= press.to + kSlack
                && distance(gesture.x, gesture.y, known->at) <= nearPixels) {
                gesture.claimed = true;
            }
        }
        if (!gesture.claimed) {
            gesture.claimed = std::ranges::any_of(placed, [&](const Placed& each) {
                const bool driven = std::ranges::find(kDrivenTypes, std::string_view(each.type)) != kDrivenTypes.end();
                return (driven || gesture.moved > swipePixels) && distance(gesture.x, gesture.y, each.at) <= nearPixels;
            });
        }
    }

    std::vector<Step> steps;
    for (std::size_t which = 0; which < presses.size(); ++which) {
        const Press& press = presses[which];
        const Placed* known = pressed[which];
        const qint64 from = roundedMs(press.from);
        if (known != nullptr && known->type != kCustomType) {
            const QString access = controlAccess(toQString(press.id));
            qint64 start = from;
            qint64 left = std::max(roundedMs(press.to) - from, kRoundMs);
            while (left > 0) {
                const qint64 held = std::min(left, kLongestHoldMs);
                steps.push_back(Step{.at = start,
                                     .busy = held,
                                     .call = QStringLiteral("%1.hold(%2)").arg(access, QString::number(held))});
                start += held;
                left -= held;
            }
        } else if (known != nullptr) {
            steps.push_back(logStep(
                from, QCoreApplication::translate("RecordingConvert",
                                                  "%1 was pressed here; components made with logic are not turned into "
                                                  "steps")
                          .arg(toQString(known->name.empty() ? known->id : known->name))));
        } else if (firstTouch[which] && !gestures[*firstTouch[which]].used) {
            Gesture& gesture = gestures[*firstTouch[which]];
            gesture.used = true;
            steps.push_back(gesture.moved > swipePixels ? swipeStep(gesture) : tapStep(gesture));
        } else {
            steps.push_back(logStep(
                from, QCoreApplication::translate("RecordingConvert",
                                                  "The component %1 was pressed here, but the profile no longer has it")
                          .arg(toQString(press.id))));
        }
    }
    for (const Gesture& gesture : gestures) {
        if (!gesture.claimed) {
            steps.push_back(gesture.moved > swipePixels ? swipeStep(gesture) : tapStep(gesture));
        }
    }

    std::optional<Microseconds> lastScroll;
    for (const script::SessionOperation& operation : session.operations) {
        const qint64 at = roundedMs(operation.at);
        if (const auto* typed = std::get_if<script::SessionText>(&operation.action)) {
            const int count = static_cast<int>(std::min<std::size_t>(typed->length, std::numeric_limits<int>::max()));
            steps.push_back(logStep(at, QCoreApplication::translate(
                                            "RecordingConvert",
                                            "Typed %n character(s) here; typing is not turned into steps", nullptr,
                                            count)));
        } else if (const auto* key = std::get_if<script::SessionKey>(&operation.action)) {
            if (key->down && key->repeat == 0) {
                steps.push_back(logStep(at, QCoreApplication::translate(
                                                "RecordingConvert",
                                                "The Android key %1 was sent here; keys are not turned into steps")
                                                .arg(key->keycode)));
            }
        } else if (std::holds_alternative<script::SessionScroll>(operation.action)) {
            if (!lastScroll || operation.at - *lastScroll > kScrollGap) {
                steps.push_back(logStep(
                    at, QCoreApplication::translate("RecordingConvert",
                                                    "A scroll was here; scrolling is not turned into steps")));
            }
            lastScroll = operation.at;
        }
    }
    if (steps.empty()) {
        return std::nullopt;
    }
    return codeOf(std::move(steps));
}

Expected<MadeTask, QString> turnRecordingIntoTask(TaskLibrary& tasks, const QString& recording,
                                                  const input::Profile* profile)
{
    const auto session = script::loadSession(script::sessionFileOf(toPath(recording)));
    if (!session) {
        return makeUnexpected(QCoreApplication::translate("RecordingConvert", "The recording cannot be read: %1")
                                  .arg(toQString(session.error())));
    }
    const std::optional<QString> code = taskCodeFor(*session, profile);
    if (!code) {
        return makeUnexpected(QCoreApplication::translate(
            "RecordingConvert", "The recording has no key presses or taps to turn into a blueprint"));
    }
    QString declarations;
    if (profile != nullptr) {
        const script::ReplayProfile typed = replayProfileOf(*profile);
        declarations = toQString(script::profileTypes(typed.controls, typed.states));
    }
    const QString id = tasks.addWithCode(
        QCoreApplication::translate("RecordingConvert", "%1 — actions").arg(recordingTitle(recording)), *code,
        declarations);
    if (id.isEmpty()) {
        return makeUnexpected(QCoreApplication::translate("RecordingConvert", "The task could not be saved"));
    }
    const TaskEntry* entry = tasks.find(id);
    return MadeTask{.id = id, .name = entry != nullptr ? entry->name : id};
}

script::ReplayProfile replayProfileOf(const input::Profile& profile)
{
    script::ReplayProfile replayed;
    for (const input::Control& control : profile.controls) {
        replayed.controls.push_back({control.id, control.name, std::string(input::kControlTypeNames[control.kind.index()]),
                                     control.enabled, input::controlSettings(control)});
    }
    for (const input::Variable& variable : profile.vars) {
        replayed.states.push_back({variable.id, variable.name,
                                   std::visit([](const auto& each) { return script::VarValue(each); }, variable.initial)});
    }
    return replayed;
}

script::ReplayKit replayKit()
{
    return script::ReplayKit{
        .profile = [](const std::filesystem::path& file) -> Expected<script::ReplayProfile, std::string> {
            std::string text;
            if (platform::readFile(file, kMostProfileBytes, text)) {
                return makeUnexpected(std::string("$: the profile cannot be read"));
            }
            const input::LoadResult loaded = input::loadProfile(text);
            std::string problems;
            for (const input::Diagnostic& diagnostic : loaded.diagnostics) {
                if (diagnostic.severity == input::Severity::Error) {
                    problems += (problems.empty() ? "" : "; ")
                                + (diagnostic.detail.empty() ? diagnostic.path : diagnostic.path + ": " + diagnostic.detail);
                }
            }
            if (!problems.empty()) {
                return makeUnexpected(std::move(problems));
            }
            return replayProfileOf(loaded.profile);
        },
        .vision = [](const std::filesystem::path& folder, const std::filesystem::path& video,
                     std::int64_t videoStartPtsUs) -> std::unique_ptr<script::ReplayVision> {
            auto vision = std::make_unique<record::RecordedVision>(folder, video, videoStartPtsUs);
            if (!vision->opened()) {
                return nullptr;
            }
            return vision;
        }};
}

Expected<QString, QString> saveRecordingTest(const QString& profileFolder, const QString& recording)
{
    const QString entry = logicEntryIn(profileFolder);
    if (entry.isEmpty()) {
        return makeUnexpected(QCoreApplication::translate("RecordingConvert", "This profile has no logic to test"));
    }
    const QString session = QDir::cleanPath(QFileInfo(recording).absoluteFilePath());
    const std::filesystem::path folder = toPath(profileFolder);
    const script::ReplayRun run = script::replaySession(
        script::ReplayRequest{.folder = folder, .entry = entry.toStdString(), .session = toPath(session)}, replayKit());
    if (!run.problem.empty()) {
        return makeUnexpected(
            QCoreApplication::translate("RecordingConvert", "Not saved: the logic could not run on the recording: %1")
                .arg(toQString(run.problem)));
    }
    if (!run.ok) {
        return makeUnexpected(QCoreApplication::translate(
            "RecordingConvert", "Not saved: the logic failed on the recording; run it on the recording to see why"));
    }
    const QString name = recordingTitle(recording);
    const std::filesystem::path tests = folder / toPath(toQString(script::kTestsFolder));
    const std::filesystem::path file = tests / toPath(name + toQString(script::kTestSuffix));
    std::error_code error;
    std::filesystem::create_directories(tests, error);
    if (error || platform::writeFileAtomically(file, script::testText(session.toStdString(),
                                                                      script::outputLines(run.lines)))) {
        return makeUnexpected(QCoreApplication::translate("RecordingConvert", "Not saved: %1 cannot be written")
                                  .arg(QDir::toNativeSeparators(QString::fromStdWString(file.wstring()))));
    }
    return name;
}

std::vector<script::TestOutcome> runLogicTests(const QString& folder)
{
    const std::filesystem::path root = toPath(folder);
    const std::string entry = logicEntryIn(folder).toStdString();
    const script::ReplayKit kit = replayKit();
    std::vector<script::TestOutcome> outcomes;
    for (const std::filesystem::path& file : script::testFiles(root)) {
        outcomes.push_back(script::runTest(file, root, entry, false, kit));
    }
    return outcomes;
}

QVariantMap shownTestResult(const script::TestOutcome& outcome)
{
    QString text;
    if (outcome.verdict == script::TestVerdict::Error) {
        text = outcome.missingRecording
                   ? QCoreApplication::translate("RecordingConvert", "The recording is no longer there")
                   : QCoreApplication::translate("RecordingConvert", "Could not run: %1").arg(toQString(outcome.detail));
    } else if (outcome.verdict == script::TestVerdict::Fail && outcome.logicFailed) {
        text = QCoreApplication::translate("RecordingConvert", "The logic failed while replaying the recording");
    } else if (outcome.verdict == script::TestVerdict::Fail) {
        const QString nothing = QCoreApplication::translate("RecordingConvert", "(nothing more)");
        text = QCoreApplication::translate("RecordingConvert", "Line %1: expected “%2”, got “%3”")
                   .arg(QString::number(outcome.line), outcome.expected ? toQString(*outcome.expected) : nothing,
                        outcome.actual ? toQString(*outcome.actual) : nothing);
    }
    return {{QStringLiteral("name"), toQString(outcome.name)},
            {QStringLiteral("result"), toQString(script::verdictName(outcome.verdict))},
            {QStringLiteral("text"), text}};
}

LogicTests::LogicTests(QObject* parent)
    : QObject(parent)
{
}

LogicTests::~LogicTests() = default;

void LogicTests::run(const QString& folder)
{
    if (m_running) {
        return;
    }
    m_running = true;
    m_note = QCoreApplication::translate("RecordingConvert", "Running the tests…");
    emit changed();
    const QPointer<LogicTests> self(this);
    QThreadPool::globalInstance()->start([self, folder] {
        const std::vector<script::TestOutcome> outcomes = runLogicTests(folder);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, outcomes] {
                if (self != nullptr) {
                    self->show(outcomes);
                }
            },
            Qt::QueuedConnection);
    });
}

void LogicTests::show(const std::vector<script::TestOutcome>& outcomes)
{
    m_running = false;
    m_results.clear();
    int failed = 0;
    for (const script::TestOutcome& outcome : outcomes) {
        m_results.append(shownTestResult(outcome));
        if (outcome.verdict != script::TestVerdict::Pass && outcome.verdict != script::TestVerdict::Updated) {
            ++failed;
        }
    }
    const int total = static_cast<int>(outcomes.size());
    if (total == 0) {
        m_note = QCoreApplication::translate("RecordingConvert", "There are no tests in the tests folder");
    } else if (failed == 0) {
        m_note = QCoreApplication::translate("RecordingConvert", "All %n test(s) passed", nullptr, total);
    } else {
        m_note = QCoreApplication::translate("RecordingConvert", "%1 of %n test(s) did not pass", nullptr, total)
                     .arg(failed);
    }
    emit changed();
}

}
