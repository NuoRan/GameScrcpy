#include "Recordings.h"

#include "AppSettings.h"
#include "ProfileLibrary.h"
#include "RecordingConvert.h"
#include "Settings.h"
#include "TaskLibrary.h"

#include <gs/media/FrameSnapshot.h>
#include <gs/record/Mp4Export.h>
#include <gs/record/Operations.h>
#include <gs/record/Recorder.h>
#include <gs/record/Recording.h>

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QSaveFile>
#include <QSize>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUrl>
#include <QtLogging>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

namespace gs::app {

namespace {

constexpr int kClockMs = 1000;
constexpr quint64 kKilobyte = 1024;
constexpr quint64 kMegabyte = kKilobyte * 1024;
constexpr quint64 kGigabyte = kMegabyte * 1024;

class Tee final : public LaneObserver
{
public:
    Tee(std::shared_ptr<LaneObserver> target, std::shared_ptr<RecordingTap> tap)
        : m_target(std::move(target))
        , m_tap(std::move(tap))
    {
    }

    void synced(const LaneValues& values) override
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_states.assign(values.states.begin(), values.states.end());
        }
        m_target->synced(values);
    }

    void controlPressed(std::string_view id, bool down) override
    {
        m_tap->pressed(id, down);
        m_target->controlPressed(id, down);
    }

    void stateChanged(std::string_view id, bool value) override
    {
        if (changes(id, value)) {
            m_tap->stateChanged(id, value);
        }
        m_target->stateChanged(id, value);
    }

    void controlEnabledChanged(std::string_view id, bool enabled) override
    {
        m_target->controlEnabledChanged(id, enabled);
    }

    void statusChanged(std::string_view id, const input::ControlStatus& status) override
    {
        m_target->statusChanged(id, status);
    }

    void keyEvent(std::string_view name, bool down) override { m_target->keyEvent(name, down); }

    void lockChanged(bool locked) override { m_target->lockChanged(locked); }

private:
    bool changes(std::string_view id, bool value)
    {
        const std::scoped_lock lock(m_mutex);
        const auto found = std::ranges::find(m_states, id, &std::pair<std::string, bool>::first);
        if (found == m_states.end()) {
            m_states.emplace_back(std::string(id), value);
            return true;
        }
        return std::exchange(found->second, value) != value;
    }

    std::shared_ptr<LaneObserver> m_target;
    std::shared_ptr<RecordingTap> m_tap;
    std::mutex m_mutex;
    std::vector<std::pair<std::string, bool>> m_states;
};

std::filesystem::path pathOf(const QString& text)
{
    return {text.toStdWString()};
}

constexpr auto kInfoFile = "recording.json";
constexpr auto kThumbnailFolder = "thumbnails";
constexpr int kMostListed = 100;

void writeRecordingInfo(const QString& folder, const RecordingStart& start)
{
    const QJsonObject info{{QStringLiteral("serial"), start.serial},
                           {QStringLiteral("phone"), start.phone},
                           {QStringLiteral("profileId"), start.profileId},
                           {QStringLiteral("profileName"), start.profileName}};
    QSaveFile file(QDir(folder).filePath(QString::fromLatin1(kInfoFile)));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(info).toJson()) < 0 || !file.commit()) {
        qWarning("could not write %s in %s", kInfoFile, qUtf8Printable(QDir::toNativeSeparators(folder)));
    }
}

QJsonObject readRecordingInfo(const QString& folder)
{
    QFile file(QDir(folder).filePath(QString::fromLatin1(kInfoFile)));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

void saveThumbnails(const QString& folder, const QVariantList& thumbnails)
{
    const QDir dir(folder);
    if (thumbnails.isEmpty() || !dir.mkpath(QString::fromLatin1(kThumbnailFolder))) {
        return;
    }
    for (qsizetype index = 0; index < thumbnails.size(); ++index) {
        const QImage image = thumbnails[index].value<QImage>();
        if (!image.isNull()) {
            image.save(dir.filePath(QStringLiteral("%1/%2.png").arg(QString::fromLatin1(kThumbnailFolder)).arg(index)));
        }
    }
}

QVariantList loadThumbnails(const QString& folder)
{
    QVariantList images;
    const QDir dir(QDir(folder).filePath(QString::fromLatin1(kThumbnailFolder)));
    for (int index = 0; index < RecordingThumbnails::kShown; ++index) {
        const QImage image(dir.filePath(QStringLiteral("%1.png").arg(index)));
        if (image.isNull()) {
            break;
        }
        images.append(image);
    }
    return images;
}

QDateTime timeInName(const QFileInfo& info)
{
    return QDateTime::fromString(info.completeBaseName().right(19), QStringLiteral("yyyy-MM-dd HH.mm.ss"));
}

qint64 folderTime(const QFileInfo& info)
{
    const QDateTime named = timeInName(info);
    return named.isValid() ? named.toMSecsSinceEpoch() : info.lastModified().toMSecsSinceEpoch();
}

QString phoneOfFolder(const QFileInfo& info)
{
    const QString name = info.completeBaseName();
    return name.size() > 20 && timeInName(info).isValid() ? name.chopped(20) : QString();
}

quint64 folderBytes(const QString& folder)
{
    quint64 bytes = 0;
    QDirIterator files(folder, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        bytes += static_cast<quint64>(files.nextFileInfo().size());
    }
    return bytes;
}

}

void RecordingTap::setRecorder(std::shared_ptr<record::Recorder> recorder)
{
    const std::scoped_lock lock(m_mutex);
    m_recorder = std::move(recorder);
}

std::shared_ptr<record::Recorder> RecordingTap::recorder() const
{
    const std::scoped_lock lock(m_mutex);
    return m_recorder;
}

void RecordingTap::touched(const protocol::TouchFrame& frame)
{
    if (const std::shared_ptr<record::Recorder> recording = recorder()) {
        recording->operation(record::touchOf(frame));
    }
}

void RecordingTap::keyed(const protocol::Key& key)
{
    if (const std::shared_ptr<record::Recorder> recording = recorder()) {
        recording->operation(record::keyOf(key));
    }
}

void RecordingTap::scrolled(const protocol::Scroll& scroll)
{
    if (const std::shared_ptr<record::Recorder> recording = recorder()) {
        recording->operation(record::scrollOf(scroll));
    }
}

void RecordingTap::typed(std::size_t characters)
{
    if (const std::shared_ptr<record::Recorder> recording = recorder()) {
        recording->operation(script::SessionText{.length = characters});
    }
}

void RecordingTap::pressed(std::string_view id, bool down)
{
    if (const std::shared_ptr<record::Recorder> recording = recorder()) {
        recording->event(down ? script::SessionEventKind::Press : script::SessionEventKind::Release, std::string(id));
    }
}

void RecordingTap::stateChanged(std::string_view id, bool value)
{
    if (const std::shared_ptr<record::Recorder> recording = recorder()) {
        recording->event(script::SessionEventKind::State, std::string(id), value);
    }
}

void RecordingTap::answered(const QString& json)
{
    if (const std::shared_ptr<record::Recorder> recording = recorder()) {
        recording->event(script::SessionEventKind::Answer, json.toStdString());
    }
}

RecordingLane::RecordingLane(std::shared_ptr<LaneAccess> lane, std::shared_ptr<RecordingTap> tap)
    : m_lane(std::move(lane))
    , m_tap(std::move(tap))
{
}

void RecordingLane::attach(std::shared_ptr<const input::Profile> profile, std::shared_ptr<LaneObserver> observer)
{
    if (observer != m_observer || m_tee == nullptr) {
        m_observer = observer;
        m_tee = observer != nullptr ? std::make_shared<Tee>(std::move(observer), m_tap) : nullptr;
    }
    m_lane->attach(std::move(profile), m_tee);
}

void RecordingLane::detach(std::shared_ptr<LaneObserver> observer)
{
    if (observer == nullptr || observer != m_observer) {
        m_lane->detach(std::move(observer));
        return;
    }
    m_lane->detach(m_tee);
    m_observer.reset();
    m_tee.reset();
}

void RecordingLane::post(std::function<void(InputHost& host, Microseconds now)> task)
{
    m_lane->post(std::move(task));
}

void RecordingLane::claimKeys(std::vector<input::InputSource> keys)
{
    m_lane->claimKeys(std::move(keys));
}

void RecordingLane::handOver(std::vector<std::string> ids)
{
    m_lane->handOver(std::move(ids));
}

void RecordingThumbnails::offer(const QImage& image)
{
    if (image.isNull()) {
        return;
    }
    if (m_tick++ % m_every != 0) {
        return;
    }
    m_kept.append(image);
    if (m_kept.size() < 2 * kShown) {
        return;
    }
    QList<QImage> halved;
    for (qsizetype index = 0; index < m_kept.size(); index += 2) {
        halved.append(m_kept[index]);
    }
    m_kept = std::move(halved);
    m_every *= 2;
    m_tick = 1;
}

QVariantList RecordingThumbnails::shown() const
{
    QVariantList shown;
    const qsizetype count = std::min<qsizetype>(m_kept.size(), kShown);
    for (qsizetype index = 0; index < count; ++index) {
        const qsizetype at = count > 1 ? index * (m_kept.size() - 1) / (count - 1) : 0;
        shown.append(QVariant::fromValue(m_kept[at]));
    }
    return shown;
}

QImage snapshotThumbnail(const media::LumaSnapshot& shot, int longSide)
{
    if (!shot.colored() || shot.width <= 0 || shot.height <= 0 || longSide <= 0) {
        return {};
    }
    const double factor = static_cast<double>(longSide) / std::max(shot.width, shot.height);
    const int width = std::max(1, static_cast<int>(std::lround(shot.width * factor)));
    const int height = std::max(1, static_cast<int>(std::lround(shot.height * factor)));
    QImage image(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        const int row = std::min(shot.height - 1, static_cast<int>((y + 0.5) * shot.height / height));
        for (int x = 0; x < width; ++x) {
            const int column = std::min(shot.width - 1, static_cast<int>((x + 0.5) * shot.width / width));
            const std::vector<std::uint8_t> rgb = media::rgbOf(shot, column, row, 1, 1);
            line[x] = rgb.size() >= 3 ? qRgb(rgb[0], rgb[1], rgb[2]) : qRgb(0, 0, 0);
        }
    }
    return image;
}

QString defaultRecordingsLocation()
{
    if (const QString data = qEnvironmentVariable("GS_DATA_DIR"); !data.isEmpty()) {
        return QDir(data).filePath(QStringLiteral("recordings"));
    }
    QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (videos.isEmpty()) {
        videos = QDir::homePath();
    }
    return QDir(videos).filePath(QStringLiteral("GameScrcpy"));
}

QString recordingsLocation(const AppSettings& settings)
{
    const QString chosen = QString::fromStdString(settings.effective({}).capture.folder).trimmed();
    return QDir::cleanPath(chosen.isEmpty() ? defaultRecordingsLocation() : chosen);
}

QString recordingFolderName(const QString& phone, const QDateTime& when)
{
    const QString safe = QString::fromStdString(record::safeFileName(phone.trimmed().toStdString()));
    return safe + u' ' + when.toString(QStringLiteral("yyyy-MM-dd HH.mm.ss"));
}

QString sizeText(quint64 bytes)
{
    if (bytes >= kGigabyte) {
        return QStringLiteral("%1 GB").arg(static_cast<double>(bytes) / kGigabyte, 0, 'f', 1);
    }
    if (bytes >= 10 * kMegabyte) {
        return QStringLiteral("%1 MB").arg(static_cast<double>(bytes) / kMegabyte, 0, 'f', 0);
    }
    if (bytes >= kMegabyte) {
        return QStringLiteral("%1 MB").arg(static_cast<double>(bytes) / kMegabyte, 0, 'f', 1);
    }
    return QStringLiteral("%1 KB").arg(std::max<quint64>(1, (bytes + kKilobyte - 1) / kKilobyte));
}

QString clockText(qint64 milliseconds)
{
    const qint64 seconds = std::max<qint64>(milliseconds, 0) / 1000;
    const qint64 hours = seconds / 3600;
    const QString minutesAndSeconds =
        QStringLiteral("%1:%2").arg((seconds / 60) % 60, 2, 10, QChar(u'0')).arg(seconds % 60, 2, 10, QChar(u'0'));
    return hours > 0 ? QStringLiteral("%1:%2").arg(hours).arg(minutesAndSeconds) : minutesAndSeconds;
}

namespace {

QVariantMap describeFolder(const QFileInfo& info, const QJsonObject& recorded, const QString& serial, qint64& at)
{
    const QString folder = info.absoluteFilePath();
    QVariantMap entry;
    entry.insert(QStringLiteral("serial"), serial);
    entry.insert(QStringLiteral("folder"), QDir::toNativeSeparators(folder));
    entry.insert(QStringLiteral("name"), info.completeBaseName());
    entry.insert(QStringLiteral("phone"), recorded.contains(QStringLiteral("phone"))
                                              ? recorded.value(QStringLiteral("phone")).toString()
                                              : phoneOfFolder(info));
    entry.insert(QStringLiteral("profileId"), recorded.value(QStringLiteral("profileId")).toString());
    entry.insert(QStringLiteral("profileName"), recorded.value(QStringLiteral("profileName")).toString());
    entry.insert(QStringLiteral("size"), sizeText(folderBytes(folder)));
    entry.insert(QStringLiteral("problem"), QString());
    at = folderTime(info);
    const auto opened = record::openRecording(pathOf(folder));
    if (opened) {
        const script::Session& session = opened->session;
        if (session.startedAtMs > 0) {
            at = session.startedAtMs;
        }
        entry.insert(QStringLiteral("duration"), clockText(session.durationMs));
        entry.insert(QStringLiteral("width"), session.device.width);
        entry.insert(QStringLiteral("height"), session.device.height);
        entry.insert(QStringLiteral("operations"), static_cast<int>(session.operations.size()));
        entry.insert(QStringLiteral("video"), !opened->video.empty());
        entry.insert(QStringLiteral("sound"), !opened->audio.empty());
        entry.insert(QStringLiteral("broken"), false);
    } else {
        entry.insert(QStringLiteral("broken"), true);
        entry.insert(QStringLiteral("reason"), QString::fromStdString(opened.error()));
    }
    const QString first = QDir(folder).filePath(QStringLiteral("%1/0.png").arg(QString::fromLatin1(kThumbnailFolder)));
    const QSize size = QImageReader(first).size();
    const bool pictured = size.width() > 0 && size.height() > 0;
    entry.insert(QStringLiteral("thumbnail"), pictured ? QUrl::fromLocalFile(first).toString() : QString());
    entry.insert(QStringLiteral("thumbnailAspect"),
                 pictured ? static_cast<double>(size.height()) / size.width() : 0.5625);
    entry.insert(QStringLiteral("when"),
                 QDateTime::fromMSecsSinceEpoch(at).toString(QStringLiteral("yyyy-MM-dd HH:mm")));
    return entry;
}

}

QVariantList recordingEntries(const QString& location, const QString& serial, const QString& phone, int most)
{
    struct Found {
        qint64 at = 0;
        QVariantMap entry;
    };
    const QString trimmed = phone.trimmed();
    const QString prefix =
        trimmed.isEmpty() ? QString() : QString::fromStdString(record::safeFileName(trimmed.toStdString())) + u' ';
    std::vector<Found> found;
    const QFileInfoList folders = QDir(location).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QFileInfo& info : folders) {
        const QString folder = info.absoluteFilePath();
        if (!record::isRecordingFolder(pathOf(folder))) {
            continue;
        }
        const QJsonObject recorded = readRecordingInfo(folder);
        if (recorded.contains(QStringLiteral("serial")) ? recorded.value(QStringLiteral("serial")).toString() != serial
                                                         : prefix.isEmpty() || !info.fileName().startsWith(prefix)) {
            continue;
        }
        qint64 at = 0;
        QVariantMap entry = describeFolder(info, recorded, serial, at);
        found.push_back({at, std::move(entry)});
    }
    std::ranges::sort(found, [](const Found& left, const Found& right) { return left.at > right.at; });
    QVariantList list;
    for (const Found& item : found) {
        if (list.size() >= most) {
            break;
        }
        list.append(item.entry);
    }
    return list;
}

struct Recordings::Run {
    RecordingStart start;
    std::shared_ptr<record::Recorder> recorder;
    QString folder;
    qint64 startedMs = 0;
    RecordingThumbnails thumbnails;
};

Recordings::Recordings(AppSettings& settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
    Q_ASSERT(s_instance == nullptr);
    s_instance = this;
    m_clock.setInterval(kClockMs);
    connect(&m_clock, &QTimer::timeout, this, &Recordings::tick);
    connect(&m_settings, &AppSettings::valuesChanged, this, &Recordings::locationChanged);
}

Recordings::~Recordings()
{
    stopAll();
    s_instance = nullptr;
}

Recordings* Recordings::create(QQmlEngine*, QJSEngine*)
{
    Q_ASSERT(s_instance != nullptr);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

QString Recordings::location() const
{
    return QDir::toNativeSeparators(recordingsLocation(m_settings));
}

QUrl Recordings::locationUrl() const
{
    return QUrl::fromLocalFile(recordingsLocation(m_settings));
}

Expected<std::shared_ptr<record::Recorder>, QString> Recordings::start(RecordingStart begin)
{
    if (running(begin.serial)) {
        return makeUnexpected(tr("This phone is being recorded already"));
    }
    const QString parent = recordingsLocation(m_settings);
    if (!QDir().mkpath(parent)) {
        return makeUnexpected(tr("The recording could not start: the folder %1 cannot be made")
                                  .arg(QDir::toNativeSeparators(parent)));
    }
    const QDateTime now = QDateTime::currentDateTime();
    const QString name = recordingFolderName(begin.phone, now);
    const QString extension = QString::fromLatin1(record::kRecordingExtension.data(),
                                                  static_cast<qsizetype>(record::kRecordingExtension.size()));
    QString folder = QDir(parent).filePath(name + extension);
    for (int copy = 2; QFileInfo::exists(folder); ++copy) {
        folder = QDir(parent).filePath(QStringLiteral("%1 (%2)%3").arg(name).arg(copy).arg(extension));
    }
    auto recorder = record::Recorder::start(record::RecorderOptions{.folder = pathOf(folder),
                                                                    .device = begin.device,
                                                                    .startedAtMs = now.toMSecsSinceEpoch(),
                                                                    .frameRate = begin.frameRate});
    if (!recorder) {
        return makeUnexpected(tr("The recording could not start: %1").arg(QString::fromStdString(recorder.error())));
    }
    writeRecordingInfo(folder, begin);
    auto run = std::make_unique<Run>();
    run->recorder = *recorder;
    run->folder = folder;
    run->startedMs = now.toMSecsSinceEpoch();
    if (begin.thumbnail) {
        run->thumbnails.offer(begin.thumbnail());
    }
    run->start = std::move(begin);
    qInfo("recording %s into %s", qUtf8Printable(run->start.phone), qUtf8Printable(QDir::toNativeSeparators(folder)));
    const QString serial = run->start.serial;
    m_runs.insert_or_assign(serial, std::move(run));
    if (!m_clock.isActive()) {
        m_clock.start();
    }
    return *recorder;
}

bool Recordings::running(const QString& serial) const
{
    return m_runs.contains(serial);
}

QVariantMap Recordings::state(const QString& serial) const
{
    const auto found = m_runs.find(serial);
    if (found == m_runs.end()) {
        return {};
    }
    return {{QStringLiteral("on"), true},
            {QStringLiteral("startedMs"), found->second->startedMs},
            {QStringLiteral("sound"), found->second->start.sound}};
}

QVariantMap Recordings::stop(const QString& serial)
{
    const auto found = m_runs.find(serial);
    if (found == m_runs.end()) {
        return {};
    }
    const std::unique_ptr<Run> run = std::move(found->second);
    m_runs.erase(found);
    if (m_runs.empty()) {
        m_clock.stop();
    }
    const record::RecorderSummary summary = run->recorder->stop();
    qInfo("recording of %s stopped after %lld ms: %llu bytes, %zu operations, %zu events, %zu video sessions%s%s",
          qUtf8Printable(run->start.phone), static_cast<long long>(summary.durationMs),
          static_cast<unsigned long long>(summary.bytes), summary.operations, summary.events, summary.videoSessions,
          summary.problem.empty() ? "" : ", ", summary.problem.c_str());
    const QVariantList thumbnails = run->thumbnails.shown();
    saveThumbnails(run->folder, thumbnails);
    const QImage first = thumbnails.isEmpty() ? QImage() : thumbnails.front().value<QImage>();
    return {{QStringLiteral("serial"), serial},
            {QStringLiteral("folder"), QDir::toNativeSeparators(run->folder)},
            {QStringLiteral("name"), QFileInfo(run->folder).fileName()},
            {QStringLiteral("phone"), run->start.phone},
            {QStringLiteral("profileId"), run->start.profileId},
            {QStringLiteral("profileName"), run->start.profileName},
            {QStringLiteral("duration"), clockText(summary.durationMs)},
            {QStringLiteral("width"), summary.width},
            {QStringLiteral("height"), summary.height},
            {QStringLiteral("size"), sizeText(summary.bytes)},
            {QStringLiteral("operations"), static_cast<int>(summary.operations)},
            {QStringLiteral("sessions"), static_cast<int>(summary.videoSessions)},
            {QStringLiteral("video"), summary.video},
            {QStringLiteral("sound"), summary.audio},
            {QStringLiteral("problem"), QString::fromStdString(summary.problem)},
            {QStringLiteral("thumbnails"), thumbnails},
            {QStringLiteral("thumbnailAspect"),
             first.isNull() || first.width() <= 0 ? 0.5625 : static_cast<double>(first.height()) / first.width()}};
}

void Recordings::stopAll()
{
    while (!m_runs.empty()) {
        static_cast<void>(stop(m_runs.begin()->first));
    }
}

void Recordings::tick()
{
    std::vector<std::pair<QString, QString>> broken;
    for (const auto& [serial, run] : m_runs) {
        if (run->start.thumbnail) {
            run->thumbnails.offer(run->start.thumbnail());
        }
        if (const std::string problem = run->recorder->problem(); !problem.empty()) {
            broken.emplace_back(serial, QString::fromStdString(problem));
        }
    }
    for (const auto& [serial, problem] : broken) {
        emit failed(serial, problem);
    }
}

void Recordings::exportVideo(const QString& folder, const QString& serial)
{
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(folder));
    if (clean.isEmpty() || m_exporting.contains(clean)) {
        return;
    }
    m_exporting.insert(clean);
    qInfo("exporting %s as MP4", qUtf8Printable(QDir::toNativeSeparators(clean)));
    const QPointer<Recordings> self(this);
    QThreadPool::globalInstance()->start([self, clean, serial] {
        auto made = record::exportMp4(pathOf(clean));
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, clean, serial, made = std::move(made)] {
                if (self == nullptr) {
                    return;
                }
                self->m_exporting.remove(clean);
                if (!made) {
                    const QString reason = QString::fromStdString(made.error());
                    qWarning("exporting %s: %s", qUtf8Printable(QDir::toNativeSeparators(clean)), qUtf8Printable(reason));
                    emit self->exported(serial, false, tr("The video was not exported: %1").arg(reason), {});
                    return;
                }
                const QString first = QString::fromStdWString(made->front().file.wstring());
                qInfo("exported %s into %zu MP4 file(s)", qUtf8Printable(QDir::toNativeSeparators(clean)),
                      made->size());
                const QString text =
                    made->size() == 1
                        ? tr("Saved the video %1").arg(QFileInfo(first).fileName())
                        : tr("Saved %n video(s): the picture changed size while recording, so each part is a video of "
                             "its own",
                             "", static_cast<int>(made->size()));
                emit self->exported(serial, true, text, QDir::toNativeSeparators(first));
            },
            Qt::QueuedConnection);
    });
}

void Recordings::showInFolder(const QString& path) const
{
    const QString native = QDir::toNativeSeparators(QDir::cleanPath(QDir::fromNativeSeparators(path)));
    if (native.isEmpty()) {
        return;
    }
    if (QFileInfo::exists(native)) {
        QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), native});
        return;
    }
    QProcess::startDetached(QStringLiteral("explorer.exe"), {QDir::toNativeSeparators(location())});
}

QString Recordings::folderOf(const QUrl& url) const
{
    return url.isLocalFile() ? QDir::toNativeSeparators(url.toLocalFile()) : url.toString();
}

QVariantMap Recordings::turnIntoTask(const QString& folder, const QString& profileId)
{
    TaskLibrary* tasks = TaskLibrary::instance();
    if (tasks == nullptr) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("text"), tr("The task could not be saved")}};
    }
    const ProfileLibrary* profiles = ProfileLibrary::instance();
    const std::shared_ptr<const input::Profile> profile =
        profiles != nullptr && !profileId.isEmpty() ? profiles->profile(profileId) : nullptr;
    const QString recording = QDir::cleanPath(QDir::fromNativeSeparators(folder));
    const Expected<MadeTask, QString> made = turnRecordingIntoTask(*tasks, recording, profile.get());
    if (!made) {
        qWarning("turning %s into a task: %s", qUtf8Printable(QDir::toNativeSeparators(recording)),
                 qUtf8Printable(made.error()));
        return {{QStringLiteral("ok"), false}, {QStringLiteral("text"), made.error()}};
    }
    qInfo("turned %s into the task %s", qUtf8Printable(QDir::toNativeSeparators(recording)), qUtf8Printable(made->id));
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("task"), made->id},
            {QStringLiteral("name"), made->name},
            {QStringLiteral("text"), tr("Turned into the task %1").arg(made->name)}};
}

int Recordings::saveAsTest(const QString& folder, const QString& profileId)
{
    const int ticket = ++m_testTickets;
    const QPointer<Recordings> self(this);
    const ProfileLibrary* profiles = ProfileLibrary::instance();
    const ProfileEntry* entry = profiles != nullptr ? profiles->find(profileId) : nullptr;
    if (entry == nullptr || entry->folder.empty() || !entry->logic) {
        const QString problem = tr("This profile has no logic to test");
        QMetaObject::invokeMethod(
            this,
            [self, ticket, problem] {
                if (self != nullptr) {
                    emit self->testSaved(ticket, false, problem);
                }
            },
            Qt::QueuedConnection);
        return ticket;
    }
    const QString profileFolder = QString::fromStdWString(entry->folder.wstring());
    const QString name = entry->name;
    const QString recording = QDir::cleanPath(QDir::fromNativeSeparators(folder));
    qInfo("saving a test of %s on %s", qUtf8Printable(name), qUtf8Printable(QDir::toNativeSeparators(recording)));
    QThreadPool::globalInstance()->start([self, ticket, profileFolder, recording, name] {
        const Expected<QString, QString> saved = saveRecordingTest(profileFolder, recording);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, ticket, saved, name] {
                if (self == nullptr) {
                    return;
                }
                if (!saved) {
                    qWarning("the test was not saved: %s", qUtf8Printable(saved.error()));
                    emit self->testSaved(ticket, false, saved.error());
                    return;
                }
                emit self->testSaved(ticket, true, tr("Saved the test %1 in %2").arg(*saved, name));
            },
            Qt::QueuedConnection);
    });
    return ticket;
}

int Recordings::listRecordings(const QString& serial, const QString& phone)
{
    const int ticket = ++m_listTickets;
    const QString location = recordingsLocation(m_settings);
    const QPointer<Recordings> self(this);
    QThreadPool::globalInstance()->start([self, ticket, location, serial, phone] {
        QVariantList entries = recordingEntries(location, serial, phone, kMostListed);
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, ticket, entries = std::move(entries)] {
                if (self != nullptr) {
                    emit self->recordingsListed(ticket, entries);
                }
            },
            Qt::QueuedConnection);
    });
    return ticket;
}

QVariantMap Recordings::describe(const QString& folder, const QString& serial) const
{
    const QFileInfo info(QDir::cleanPath(QDir::fromNativeSeparators(folder)));
    if (folder.isEmpty() || !record::isRecordingFolder(pathOf(info.absoluteFilePath()))) {
        return {};
    }
    qint64 at = 0;
    QVariantMap entry = describeFolder(info, readRecordingInfo(info.absoluteFilePath()), serial, at);
    entry.insert(QStringLiteral("thumbnails"), loadThumbnails(info.absoluteFilePath()));
    return entry;
}

QString Recordings::moveToTrash(const QString& folder)
{
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(folder));
    const QString parent = QDir::cleanPath(QFileInfo(clean).absolutePath());
    if (clean.isEmpty() || !record::isRecordingFolder(pathOf(clean)) ||
        parent.compare(QDir::cleanPath(recordingsLocation(m_settings)), Qt::CaseInsensitive) != 0) {
        return tr("This is not a recording in the recordings folder");
    }
    for (const auto& running : m_runs) {
        if (QDir::cleanPath(running.second->folder).compare(clean, Qt::CaseInsensitive) == 0) {
            return tr("This recording is still being made");
        }
    }
    if (m_exporting.contains(clean)) {
        return tr("The video of this recording is being exported");
    }
    if (!QFile::moveToTrash(clean)) {
        qWarning("could not move %s to the Recycle Bin", qUtf8Printable(QDir::toNativeSeparators(clean)));
        return tr("The recording could not be moved to the Recycle Bin");
    }
    qInfo("moved the recording %s to the Recycle Bin", qUtf8Printable(QDir::toNativeSeparators(clean)));
    return {};
}

}
