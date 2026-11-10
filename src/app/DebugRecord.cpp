#include "DebugRecord.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>
#include <vector>

namespace gs::app {

namespace {

constexpr double kStepTailMs = 120.0;
constexpr double kEventTailMs = 300.0;
constexpr double kJoinGapMs = 1000.0;
constexpr double kNearMs = 1000.0;
constexpr double kTimelineCells = 300.0;
constexpr std::size_t kMostKept = 50000;
constexpr int kFrameQuality = 70;

int lineOf(const QString& position)
{
    return position.section(u':', 0, 0).toInt();
}

QString clockText(double ms)
{
    const qint64 tenths = std::max<qint64>(0, static_cast<qint64>(std::llround(ms / 100.0)));
    const qint64 seconds = tenths / 10;
    return QStringLiteral("%1:%2.%3")
        .arg(seconds / 60, 2, 10, QChar(u'0'))
        .arg(seconds % 60, 2, 10, QChar(u'0'))
        .arg(tenths % 10);
}

QVariant parsedJson(const QString& json)
{
    const QJsonArray wrapped = QJsonDocument::fromJson(QStringLiteral("[%1]").arg(json).toUtf8()).array();
    return wrapped.isEmpty() ? QVariant() : wrapped.first().toVariant();
}

QVariantList boxesOf(const QVariant& found)
{
    QVariantList boxes;
    const auto add = [&boxes](const QVariantMap& place) {
        if (!place.contains(QStringLiteral("width")) || !place.contains(QStringLiteral("height"))) {
            return;
        }
        const double width = place.value(QStringLiteral("width")).toDouble();
        const double height = place.value(QStringLiteral("height")).toDouble();
        boxes.push_back(QVariantMap{{QStringLiteral("x"), place.value(QStringLiteral("x")).toDouble() - width / 2.0},
                                    {QStringLiteral("y"), place.value(QStringLiteral("y")).toDouble() - height / 2.0},
                                    {QStringLiteral("w"), width},
                                    {QStringLiteral("h"), height},
                                    {QStringLiteral("text"),
                                     QString::number(place.value(QStringLiteral("score")).toDouble(), 'f', 2)},
                                    {QStringLiteral("tone"), QStringLiteral("good")}});
    };
    if (found.typeId() == QMetaType::QVariantList) {
        for (const QVariant& item : found.toList()) {
            add(item.toMap());
        }
    } else {
        add(found.toMap());
    }
    return boxes;
}

QString safeName(QString name)
{
    for (QChar& c : name) {
        if (QStringLiteral("\\/:*?\"<>|").contains(c) || c.unicode() < 0x20) {
            c = u'_';
        }
    }
    name = name.trimmed();
    return name.isEmpty() ? QStringLiteral("logic") : name;
}

bool codeFile(const QString& name)
{
    const QString lower = name.toLower();
    return lower.endsWith(QStringLiteral(".ts")) || lower.endsWith(QStringLiteral(".mts")) ||
           lower.endsWith(QStringLiteral(".js")) || lower.endsWith(QStringLiteral(".mjs")) ||
           lower.endsWith(QStringLiteral(".json"));
}

struct Block {
    double start = 0.0;
    double lastAt = 0.0;
    double end = 0.0;
    QString text;
    QString tone;
    int count = 1;
    bool mixed = false;
};

void placeBlock(std::vector<Block>& blocks, double at, double tail, double grain, const QString& text, const QString& tone)
{
    if (!blocks.empty()) {
        Block& previous = blocks.back();
        const bool join = !previous.mixed && previous.text == text && at - previous.lastAt < kJoinGapMs;
        if (join || at - previous.start < grain) {
            previous.mixed = previous.mixed || previous.text != text;
            previous.lastAt = std::max(previous.lastAt, at);
            previous.end = std::max(previous.end, at + tail);
            ++previous.count;
            return;
        }
        previous.end = std::min(previous.end, at);
    }
    blocks.push_back(Block{.start = at, .lastAt = at, .end = at + tail, .text = text, .tone = tone});
}

}

DebugRecord::DebugRecord(QObject* parent)
    : QObject(parent), m_clock([] { return static_cast<double>(QDateTime::currentMSecsSinceEpoch()); })
{
}

DebugRecord::~DebugRecord()
{
    if (m_recording && !m_folder.isEmpty()) {
        QDir(m_folder).removeRecursively();
    }
}

QByteArray DebugRecord::encodeFrame(const QImage& image)
{
    QByteArray bytes;
    if (image.isNull()) {
        return bytes;
    }
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "JPG", kFrameQuality)) {
        return {};
    }
    return bytes;
}

void DebugRecord::setClock(std::function<double()> clock)
{
    m_clock = std::move(clock);
}

double DebugRecord::now() const
{
    return m_clock();
}

void DebugRecord::start(const QString& liveFolder)
{
    if (liveFolder.isEmpty()) {
        return;
    }
    clear();
    m_opened = false;
    m_name.clear();
    m_code.clear();
    m_folder = liveFolder;
    QDir(m_folder).removeRecursively();
    QDir().mkpath(m_folder);
    m_recording = true;
    m_live = true;
    notify();
}

void DebugRecord::stop()
{
    if (!m_recording) {
        return;
    }
    m_recording = false;
    clear();
    if (!m_folder.isEmpty()) {
        QDir(m_folder).removeRecursively();
    }
    m_live = true;
    notify();
}

void DebugRecord::close()
{
    if (!m_opened) {
        return;
    }
    m_opened = false;
    m_name.clear();
    m_code.clear();
    clear();
    m_live = true;
    emit wentLive();
    notify();
}

QString DebugRecord::recordedFile(const QString& path) const
{
    if (!m_opened || m_code.isEmpty()) {
        return {};
    }
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (clean.isEmpty() || clean == u".." || clean.startsWith(QStringLiteral("../")) || QDir::isAbsolutePath(clean)) {
        return {};
    }
    const QString file = QDir(m_code).filePath(clean);
    return QFileInfo(file).isFile() ? file : QString();
}

void DebugRecord::clear()
{
    m_steps.clear();
    m_events.clear();
    m_frames.clear();
    m_framesMade = 0;
    m_second = -1;
    m_secondKept = 0;
    m_playhead = 0.0;
}

void DebugRecord::notify()
{
    m_notified = now();
    emit changed();
}

void DebugRecord::keep(Step step)
{
    const auto second = static_cast<qint64>(std::floor(step.at / 1000.0));
    if (second != m_second) {
        m_second = second;
        m_secondKept = 0;
    }
    if (m_secondKept >= kStepsPerSecond && !m_steps.empty()) {
        m_steps.back() = std::move(step);
        return;
    }
    ++m_secondKept;
    m_steps.push_back(std::move(step));
}

void DebugRecord::addHits(const QVariantList& hits)
{
    if (!m_recording) {
        return;
    }
    for (const QVariant& value : hits) {
        const QVariantMap hit = value.toMap();
        const double at = hit.value(QStringLiteral("at")).toDouble();
        if (hit.contains(QStringLiteral("kind"))) {
            auto where = m_events.end();
            while (where != m_events.begin() && std::prev(where)->at > at) {
                --where;
            }
            m_events.insert(where, Event{.at = at, .data = hit});
            continue;
        }
        const QString position = hit.value(QStringLiteral("position")).toString();
        keep(Step{.at = at,
                  .task = hit.value(QStringLiteral("task")).toLongLong(),
                  .line = lineOf(position),
                  .label = hit.value(QStringLiteral("label")).toString(),
                  .file = hit.value(QStringLiteral("file")).toString(),
                  .position = position,
                  .values = hit.value(QStringLiteral("values")).toMap()});
    }
    trim();
}

void DebugRecord::addPause(const QVariantMap& pause)
{
    if (!m_recording) {
        return;
    }
    QVariantMap data = pause;
    data.insert(QStringLiteral("kind"), QStringLiteral("pause"));
    m_events.push_back(Event{.at = now(), .data = data});
    trim();
    notify();
}

void DebugRecord::addResume(quint64 pause)
{
    if (!m_recording) {
        return;
    }
    m_events.push_back(Event{.at = now(),
                             .data = QVariantMap{{QStringLiteral("kind"), QStringLiteral("resume")},
                                                 {QStringLiteral("pause"), pause}}});
    trim();
    notify();
}

void DebugRecord::addFrame(const QByteArray& jpeg)
{
    if (!m_recording) {
        return;
    }
    if (!jpeg.isEmpty()) {
        const QString file = QDir(m_folder).filePath(QStringLiteral("frame-%1.jpg").arg(++m_framesMade));
        QFile out(file);
        if (out.open(QIODevice::WriteOnly) && out.write(jpeg) == jpeg.size()) {
            m_frames.push_back(Frame{.at = now(), .file = file});
        }
    }
    refresh();
}

void DebugRecord::refresh()
{
    if (!m_recording) {
        return;
    }
    trim();
    if (now() - m_notified >= kRefreshMs) {
        notify();
    }
}

void DebugRecord::trim()
{
    if (!m_recording) {
        return;
    }
    const double cutoff = now() - kWindowMs;
    while (!m_steps.empty() && (m_steps.front().at < cutoff || m_steps.size() > kMostKept)) {
        m_steps.pop_front();
    }
    while (!m_events.empty() && (m_events.front().at < cutoff || m_events.size() > kMostKept)) {
        m_events.pop_front();
    }
    while (!m_frames.empty() && m_frames.front().at < cutoff) {
        QFile::remove(m_frames.front().file);
        m_frames.pop_front();
    }
}

double DebugRecord::first() const
{
    double earliest = m_recording ? now() : 0.0;
    bool any = false;
    const auto take = [&](double at) {
        if (!any || at < earliest) {
            earliest = at;
            any = true;
        }
    };
    if (!m_steps.empty()) {
        take(m_steps.front().at);
    }
    if (!m_events.empty()) {
        take(m_events.front().at);
    }
    if (!m_frames.empty()) {
        take(m_frames.front().at);
    }
    return earliest;
}

double DebugRecord::last() const
{
    if (m_recording) {
        return now();
    }
    double latest = 0.0;
    if (!m_steps.empty()) {
        latest = std::max(latest, m_steps.back().at);
    }
    if (!m_events.empty()) {
        latest = std::max(latest, m_events.back().at);
    }
    if (!m_frames.empty()) {
        latest = std::max(latest, m_frames.back().at);
    }
    return latest;
}

const DebugRecord::Step* DebugRecord::stepAt(double at) const
{
    for (auto it = m_steps.rbegin(); it != m_steps.rend(); ++it) {
        if (it->at <= at) {
            return &*it;
        }
    }
    return m_steps.empty() ? nullptr : &m_steps.front();
}

const DebugRecord::Event* DebugRecord::pauseAt(double at) const
{
    QSet<quint64> resumed;
    for (auto it = m_events.rbegin(); it != m_events.rend(); ++it) {
        if (it->at > at) {
            continue;
        }
        const QString kind = it->data.value(QStringLiteral("kind")).toString();
        const quint64 id = it->data.value(QStringLiteral("pause")).toULongLong();
        if (kind == QStringLiteral("resume")) {
            resumed.insert(id);
        } else if (kind == QStringLiteral("pause") && !resumed.contains(id)) {
            return &*it;
        }
    }
    return nullptr;
}

QString DebugRecord::laneTitle(const QString& label) const
{
    return label.isEmpty() ? tr("Loading") : label;
}

QString DebugRecord::stepText(const Step& step) const
{
    return laneTitle(step.label) + QStringLiteral(" · ") + tr("%1 line %2").arg(step.file).arg(step.line);
}

QString DebugRecord::eventText(const QVariantMap& data) const
{
    const QString kind = data.value(QStringLiteral("kind")).toString();
    if (kind == QStringLiteral("vision")) {
        return visionText(data.value(QStringLiteral("json")).toString());
    }
    const QString x = QString::number(data.value(QStringLiteral("x")).toDouble(), 'f', 2);
    const QString y = QString::number(data.value(QStringLiteral("y")).toDouble(), 'f', 2);
    if (kind == QStringLiteral("swipe")) {
        return tr("Swipe from %1, %2").arg(x, y);
    }
    if (kind == QStringLiteral("hold")) {
        return tr("Hold %1, %2").arg(x, y);
    }
    return tr("Tap %1, %2").arg(x, y);
}

QString DebugRecord::visionText(const QString& json) const
{
    const QVariant found = parsedJson(json);
    if (found.typeId() == QMetaType::QVariantList) {
        const qsizetype count = found.toList().size();
        return count == 0 ? tr("Not found") : tr("Found %n place(s)", "", static_cast<int>(count));
    }
    if (found.typeId() == QMetaType::QVariantMap && found.toMap().contains(QStringLiteral("score"))) {
        return tr("Found %1").arg(QString::number(found.toMap().value(QStringLiteral("score")).toDouble(), 'f', 2));
    }
    if (!found.isValid() || found.isNull() || (found.typeId() == QMetaType::Bool && !found.toBool())) {
        return tr("Not found");
    }
    if (found.typeId() == QMetaType::QString) {
        return found.toString().left(60);
    }
    return json.left(60);
}

QVariantMap DebugRecord::timeline() const
{
    const double start = first();
    const double end = last();
    const double grain = std::max(kStepTailMs, (end - start) / kTimelineCells);
    QVariantList frames;
    for (const Frame& frame : m_frames) {
        frames.push_back(QVariantMap{{QStringLiteral("at"), frame.at},
                                     {QStringLiteral("source"), QUrl::fromLocalFile(frame.file).toString()}});
    }
    std::vector<Block> code;
    const Step* previous = nullptr;
    QString text;
    for (const Step& step : m_steps) {
        if (previous == nullptr || previous->line != step.line || previous->file != step.file ||
            previous->label != step.label) {
            text = stepText(step);
            previous = &step;
        }
        placeBlock(code, step.at, kStepTailMs, grain, text, QStringLiteral("action"));
    }
    std::vector<Block> actions;
    for (const Event& event : m_events) {
        const QString kind = event.data.value(QStringLiteral("kind")).toString();
        if (kind == QStringLiteral("pause") || kind == QStringLiteral("resume")) {
            continue;
        }
        placeBlock(actions, event.at, kEventTailMs, grain, eventText(event.data),
                   kind == QStringLiteral("vision") ? QStringLiteral("vision") : QStringLiteral("custom"));
    }
    QVariantList steps;
    const auto shown = [this, &steps](const std::vector<Block>& blocks, const QString& lane, bool actions) {
        for (const Block& block : blocks) {
            const QString mixed = actions ? tr("%n action(s)", "", block.count) : tr("%n step(s)", "", block.count);
            steps.push_back(QVariantMap{{QStringLiteral("lane"), lane},
                                        {QStringLiteral("start"), block.start},
                                        {QStringLiteral("end"), block.end},
                                        {QStringLiteral("text"), block.mixed ? mixed : block.text},
                                        {QStringLiteral("tone"), block.tone}});
        }
    };
    shown(code, QStringLiteral("code"), false);
    shown(actions, QStringLiteral("act"), true);
    QVariantList lanes{QVariantMap{{QStringLiteral("key"), QStringLiteral("code")}, {QStringLiteral("title"), tr("Steps")}}};
    if (!actions.empty()) {
        lanes.push_back(QVariantMap{{QStringLiteral("key"), QStringLiteral("act")}, {QStringLiteral("title"), tr("Actions")}});
    }
    return {{QStringLiteral("span"), QVariantMap{{QStringLiteral("start"), start}, {QStringLiteral("end"), end}}},
            {QStringLiteral("playhead"), m_live ? end : m_playhead},
            {QStringLiteral("live"), m_live},
            {QStringLiteral("frames"), frames},
            {QStringLiteral("steps"), steps},
            {QStringLiteral("lanes"), lanes}};
}

QVariantMap DebugRecord::moment() const
{
    if (!active()) {
        return {};
    }
    const double at = m_live ? last() : m_playhead;
    QVariantMap moment{{QStringLiteral("title"), tr("Moment · %1").arg(clockText(at - first()))}};
    QString frameUrl;
    for (auto it = m_frames.rbegin(); it != m_frames.rend(); ++it) {
        if (it->at <= at) {
            frameUrl = QUrl::fromLocalFile(it->file).toString();
            break;
        }
    }
    if (frameUrl.isEmpty() && !m_frames.empty()) {
        frameUrl = QUrl::fromLocalFile(m_frames.front().file).toString();
    }
    QVariantList boxes;
    QString result;
    for (auto it = m_events.rbegin(); it != m_events.rend(); ++it) {
        if (it->at > at) {
            continue;
        }
        if (at - it->at > kNearMs) {
            break;
        }
        if (it->data.value(QStringLiteral("kind")).toString() == QStringLiteral("vision")) {
            const QString json = it->data.value(QStringLiteral("json")).toString();
            result = visionText(json);
            boxes = boxesOf(parsedJson(json));
            break;
        }
    }
    QVariantList variables;
    QVariantList stack;
    const Event* pause = pauseAt(at);
    const Step* step = stepAt(at);
    if (pause != nullptr) {
        const QString file = pause->data.value(QStringLiteral("file")).toString();
        const int line = lineOf(pause->data.value(QStringLiteral("position")).toString());
        const Step* paused = stepAt(pause->at);
        const QString label = paused != nullptr && paused->file == file && paused->line == line ? paused->label : QString();
        moment.insert(QStringLiteral("subtitle"), laneTitle(label) + QStringLiteral(" · ") + tr("%1 line %2").arg(file).arg(line));
        const QVariantMap values = pause->data.value(QStringLiteral("values")).toMap();
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            variables.push_back(QVariantMap{{QStringLiteral("name"), it.key()}, {QStringLiteral("value"), it.value().toString()}});
        }
        const QVariantList frames = pause->data.value(QStringLiteral("stack")).toList();
        for (qsizetype i = 0; i < frames.size(); ++i) {
            const QString frame = frames[i].toString();
            const QString place = frame.section(u'#', 0, 0) + u':' + QString::number(lineOf(frame.section(u'#', 1)));
            stack.push_back(QVariantMap{{QStringLiteral("title"), i == 0 ? laneTitle(label) : place},
                                        {QStringLiteral("place"), place},
                                        {QStringLiteral("current"), false}});
        }
        stack.push_back(QVariantMap{{QStringLiteral("title"), tr("Paused here")},
                                    {QStringLiteral("place"), file + u':' + QString::number(line)},
                                    {QStringLiteral("current"), true}});
    } else if (step != nullptr) {
        moment.insert(QStringLiteral("subtitle"), stepText(*step));
        for (auto it = step->values.cbegin(); it != step->values.cend(); ++it) {
            variables.push_back(QVariantMap{{QStringLiteral("name"), it.key()}, {QStringLiteral("value"), it.value().toString()}});
        }
        stack.push_back(QVariantMap{{QStringLiteral("title"), laneTitle(step->label)},
                                    {QStringLiteral("place"), step->file + u':' + QString::number(step->line)},
                                    {QStringLiteral("current"), true}});
    }
    moment.insert(QStringLiteral("frame"), frameUrl);
    moment.insert(QStringLiteral("boxes"), boxes);
    moment.insert(QStringLiteral("result"), result);
    moment.insert(QStringLiteral("variables"), variables);
    moment.insert(QStringLiteral("stack"), stack);
    return moment;
}

void DebugRecord::seek(double at)
{
    if (!active()) {
        return;
    }
    m_live = false;
    m_playhead = std::clamp(at, first(), std::max(first(), last()));
    if (const Step* step = stepAt(m_playhead)) {
        emit replayed(step->file, step->position, step->values);
    }
    notify();
}

void DebugRecord::goLive()
{
    if (m_live || !m_recording) {
        return;
    }
    m_live = true;
    emit wentLive();
    notify();
}

QString DebugRecord::save(const QString& root, const QString& codeFolder, const QString& name) const
{
    if (!active()) {
        return {};
    }
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString folder = QDir(root).filePath(QStringLiteral("debug-records/%1-%2").arg(safeName(name), stamp));
    if (!QDir().mkpath(folder + QStringLiteral("/frames")) || !QDir().mkpath(folder + QStringLiteral("/code"))) {
        return {};
    }
    QJsonArray steps;
    for (const Step& step : m_steps) {
        steps.push_back(QJsonObject{{QStringLiteral("at"), step.at},
                                    {QStringLiteral("task"), step.task},
                                    {QStringLiteral("label"), step.label},
                                    {QStringLiteral("file"), step.file},
                                    {QStringLiteral("position"), step.position},
                                    {QStringLiteral("values"), QJsonObject::fromVariantMap(step.values)}});
    }
    QJsonArray events;
    for (const Event& event : m_events) {
        QJsonObject data = QJsonObject::fromVariantMap(event.data);
        data.insert(QStringLiteral("at"), event.at);
        events.push_back(data);
    }
    QJsonArray frames;
    for (const Frame& frame : m_frames) {
        const QString file = QStringLiteral("frames/") + QFileInfo(frame.file).fileName();
        QFile::copy(frame.file, QDir(folder).filePath(file));
        frames.push_back(QJsonObject{{QStringLiteral("at"), frame.at}, {QStringLiteral("file"), file}});
    }
    if (!codeFolder.isEmpty()) {
        const QDir code(codeFolder);
        QDirIterator files(codeFolder, QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const QString path = files.next();
            const QString relative = code.relativeFilePath(path);
            if (!codeFile(relative) || relative.startsWith(QStringLiteral("node_modules/")) ||
                relative.startsWith(QStringLiteral("types/")) || relative.startsWith(u'.')) {
                continue;
            }
            const QString target = QDir(folder + QStringLiteral("/code")).filePath(relative);
            QDir().mkpath(QFileInfo(target).absolutePath());
            QFile::copy(path, target);
        }
    }
    QSaveFile record(QDir(folder).filePath(QStringLiteral("record.json")));
    if (!record.open(QIODevice::WriteOnly)) {
        return {};
    }
    record.write(QJsonDocument(QJsonObject{{QStringLiteral("format"), QStringLiteral("gamescrcpy.debug-record")},
                                           {QStringLiteral("version"), 1},
                                           {QStringLiteral("name"), name},
                                           {QStringLiteral("steps"), steps},
                                           {QStringLiteral("events"), events},
                                           {QStringLiteral("frames"), frames}})
                     .toJson(QJsonDocument::Compact));
    return record.commit() ? folder : QString();
}

bool DebugRecord::open(const QString& folder)
{
    QFile file(QDir(folder).filePath(QStringLiteral("record.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("gamescrcpy.debug-record")) {
        return false;
    }
    stop();
    clear();
    for (const QJsonValue& value : root.value(QStringLiteral("steps")).toArray()) {
        const QJsonObject step = value.toObject();
        const QString position = step.value(QStringLiteral("position")).toString();
        m_steps.push_back(Step{.at = step.value(QStringLiteral("at")).toDouble(),
                               .task = step.value(QStringLiteral("task")).toInteger(),
                               .line = lineOf(position),
                               .label = step.value(QStringLiteral("label")).toString(),
                               .file = step.value(QStringLiteral("file")).toString(),
                               .position = position,
                               .values = step.value(QStringLiteral("values")).toObject().toVariantMap()});
    }
    for (const QJsonValue& value : root.value(QStringLiteral("events")).toArray()) {
        const QVariantMap data = value.toObject().toVariantMap();
        m_events.push_back(Event{.at = data.value(QStringLiteral("at")).toDouble(), .data = data});
    }
    for (const QJsonValue& value : root.value(QStringLiteral("frames")).toArray()) {
        const QJsonObject frame = value.toObject();
        m_frames.push_back(Frame{.at = frame.value(QStringLiteral("at")).toDouble(),
                                 .file = QDir(folder).filePath(frame.value(QStringLiteral("file")).toString())});
    }
    m_name = root.value(QStringLiteral("name")).toString();
    m_code = QDir(folder).filePath(QStringLiteral("code"));
    if (!QFileInfo(m_code).isDir()) {
        m_code.clear();
    }
    m_opened = true;
    m_live = false;
    m_playhead = first();
    notify();
    if (const Step* step = stepAt(m_playhead)) {
        emit replayed(step->file, step->position, step->values);
    }
    return true;
}

}
