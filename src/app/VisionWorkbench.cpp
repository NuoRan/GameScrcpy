#include "VisionWorkbench.h"

#include "LogicReferences.h"
#include "VisionSearch.h"

#include <gs/platform/AtomicFile.h>
#include <gs/render/VideoItem.h>

#include <QBuffer>
#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QMetaObject>
#include <QQuickItem>
#include <QRect>
#include <QThreadPool>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <mutex>
#include <string_view>
#include <system_error>
#include <utility>

namespace gs::app {

using namespace Qt::StringLiterals;

namespace {

constexpr double kRoundTo = 10000.0;
constexpr double kLeastRatio = 0.002;
constexpr qint64 kMostPictureBytes = qint64{32} << 20;
constexpr int kMostTestHits = 16;
constexpr int kMostPathPoints = static_cast<int>(script::kMostPathPoints);

struct TestOutcome {
    QVariantList results;
    QVariantMap summary;
};

double rounded(double value)
{
    return std::round(value * kRoundTo) / kRoundTo;
}

double clampRatio(double value)
{
    return std::clamp(std::isfinite(value) ? value : 0.0, 0.0, 1.0);
}

QString text(const std::string& value)
{
    return QString::fromStdString(value);
}

std::string utf8(const QString& value)
{
    return value.toStdString();
}

std::filesystem::path pathOf(const std::string& file)
{
    return QString::fromStdString(file).toStdWString();
}

std::string_view bytesOf(const QByteArray& bytes)
{
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

QByteArray digestOf(std::string_view bytes)
{
    return QCryptographicHash::hash(QByteArray::fromRawData(bytes.data(), static_cast<qsizetype>(bytes.size())),
                                    QCryptographicHash::Sha256);
}

QString listOf(const QStringList& files)
{
    return files.join(VisionWorkbench::tr(", ", "list separator"));
}

std::mutex& storeMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::map<QString, QImage>& store()
{
    static std::map<QString, QImage> images;
    return images;
}

template <typename Item>
Item* itemNamed(std::vector<Item>& items, const std::string& name)
{
    const auto found = std::ranges::find_if(items, [&name](const Item& item) { return item.name == name; });
    return found != items.end() ? &*found : nullptr;
}

template <typename Item>
bool eraseNamed(std::vector<Item>& items, const std::string& name)
{
    return std::erase_if(items, [&name](const Item& item) { return item.name == name; }) > 0;
}

template <typename Item>
const Item* findNamed(const std::vector<Item>& items, const std::string& name)
{
    const auto found = std::ranges::find_if(items, [&name](const Item& item) { return item.name == name; });
    return found != items.end() ? &*found : nullptr;
}

template <typename Item>
std::vector<Item> mergeItems(const std::vector<Item>& base, const std::vector<Item>& ours,
                             const std::vector<Item>& theirs)
{
    std::vector<Item> merged;
    for (const Item& mine : ours) {
        const Item* before = findNamed(base, mine.name);
        if (before == nullptr || !(*before == mine)) {
            merged.push_back(mine);
        } else if (const Item* now = findNamed(theirs, mine.name)) {
            merged.push_back(*now);
        }
    }
    for (const Item& other : theirs) {
        if (findNamed(base, other.name) == nullptr && findNamed(ours, other.name) == nullptr) {
            merged.push_back(other);
        }
    }
    return merged;
}

QVariantMap areaMap(double x, double y, double width, double height)
{
    return {{u"x"_s, x}, {u"y"_s, y}, {u"width"_s, width}, {u"height"_s, height}};
}

QRect pixelRect(double x, double y, double width, double height, QSize size)
{
    const int left = std::clamp(static_cast<int>(std::floor(x * size.width() + 1e-9)), 0, size.width());
    const int top = std::clamp(static_cast<int>(std::floor(y * size.height() + 1e-9)), 0, size.height());
    const int right = std::clamp(static_cast<int>(std::ceil((x + width) * size.width() - 1e-9)), left, size.width());
    const int bottom = std::clamp(static_cast<int>(std::ceil((y + height) * size.height() - 1e-9)), top, size.height());
    return {left, top, right - left, bottom - top};
}

QVariantMap hitMap(const vision::Rect& rect, double score, int width, int height)
{
    QVariantMap hit = areaMap(static_cast<double>(rect.x) / width, static_cast<double>(rect.y) / height,
                              static_cast<double>(rect.width) / width, static_cast<double>(rect.height) / height);
    hit.insert(u"score"_s, std::clamp(score, 0.0, 1.0));
    hit.insert(u"centerX"_s, static_cast<int>(std::lround(rect.x + rect.width / 2.0)));
    hit.insert(u"centerY"_s, static_cast<int>(std::lround(rect.y + rect.height / 2.0)));
    return hit;
}

double millisecondsSince(std::chrono::steady_clock::time_point started)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
}

TestOutcome testTemplate(const media::LumaSnapshot& shot, const QByteArray& bytes, const script::TemplateResource& entry,
                         const script::Region& region, const QString& name)
{
    TestOutcome outcome;
    outcome.summary = {{u"kind"_s, u"template"_s}, {u"name"_s, name}};
    const QImage picture = QImage::fromData(bytes);
    std::string problem;
    const std::shared_ptr<const vision::Template> pattern =
        picture.isNull() ? nullptr
                         : templateOf(scaledPicture(picture, entry.source, shot.width, shot.height),
                                      entry.match == script::MatchMode::Color, utf8(name), problem);
    if (pattern == nullptr) {
        outcome.summary.insert(u"problem"_s,
                               VisionWorkbench::tr("This template cannot be matched: its picture is empty, "
                                                   "transparent or a single color"));
        return outcome;
    }
    const vision::Rect area = pixelArea(region, shot.width, shot.height);
    const auto started = std::chrono::steady_clock::now();
    const std::vector<vision::Match> found = findTemplate(shot, *pattern, area, entry.threshold, kMostTestHits, true);
    outcome.summary.insert(u"ms"_s, millisecondsSince(started));
    for (const vision::Match& match : found) {
        outcome.results.append(hitMap(match.rect, match.score, shot.width, shot.height));
    }
    if (found.empty()) {
        const std::vector<vision::Match> nearest = findTemplate(shot, *pattern, area, -1.0, 1, false);
        if (!nearest.empty()) {
            outcome.summary.insert(u"nearest"_s, std::clamp(nearest.front().score, 0.0, 1.0));
        }
    }
    outcome.summary.insert(u"count"_s, static_cast<int>(found.size()));
    return outcome;
}

TestOutcome testColor(const media::LumaSnapshot& shot, const script::ColorResource& entry, const script::Region& region,
                      const QString& name)
{
    TestOutcome outcome;
    outcome.summary = {{u"kind"_s, u"color"_s}, {u"name"_s, name}};
    const vision::Rect area = pixelArea(region, shot.width, shot.height);
    const auto started = std::chrono::steady_clock::now();
    const std::optional<vision::ColorPatch> patch =
        findColorIn(shot, {entry.color.red, entry.color.green, entry.color.blue}, entry.tolerance, area);
    outcome.summary.insert(u"ms"_s, millisecondsSince(started));
    if (patch) {
        QVariantMap hit = hitMap(patch->rect, patch->score, shot.width, shot.height);
        hit.insert(u"pixels"_s, static_cast<qlonglong>(patch->pixels));
        outcome.results.append(hit);
    }
    outcome.summary.insert(u"count"_s, patch ? 1 : 0);
    return outcome;
}

}

script::Resources mergeResources(const script::Resources& base, const script::Resources& ours,
                                 const script::Resources& theirs)
{
    script::Resources merged;
    merged.templates = mergeItems(base.templates, ours.templates, theirs.templates);
    merged.regions = mergeItems(base.regions, ours.regions, theirs.regions);
    merged.points = mergeItems(base.points, ours.points, theirs.points);
    merged.paths = mergeItems(base.paths, ours.paths, theirs.paths);
    merged.colors = mergeItems(base.colors, ours.colors, theirs.colors);
    const auto known = [&merged](const std::string& region) {
        return region.empty() || merged.findRegion(region) != nullptr;
    };
    for (script::TemplateResource& entry : merged.templates) {
        if (!known(entry.region)) {
            entry.region.clear();
        }
    }
    for (script::ColorResource& entry : merged.colors) {
        if (!known(entry.region)) {
            entry.region.clear();
        }
    }
    return merged;
}

VisionImageProvider::VisionImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage VisionImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    QImage image;
    {
        const std::scoped_lock lock(storeMutex());
        if (const auto found = store().find(id); found != store().end()) {
            image = found->second;
        }
    }
    if (size != nullptr) {
        *size = image.size();
    }
    if (!image.isNull() && requestedSize.width() > 0 && requestedSize.height() > 0) {
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

void VisionImageProvider::publish(const QString& key, const QImage& image)
{
    const std::scoped_lock lock(storeMutex());
    store()[key] = image;
}

void VisionImageProvider::clear()
{
    const std::scoped_lock lock(storeMutex());
    store().clear();
}

VisionWorkbench::VisionWorkbench(VisionSetup setup, QObject* parent)
    : QObject(parent), m_setup(std::move(setup)), m_res(m_setup.folder / "res")
{
    load();
    publishPictures();
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &VisionWorkbench::reloadFromDisk);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &VisionWorkbench::reloadFromDisk);
    watchFiles();
    const std::vector<VisionPhone> phones = m_setup.phones ? m_setup.phones() : std::vector<VisionPhone>{};
    QString first;
    if (std::ranges::any_of(phones, [this](const VisionPhone& phone) { return phone.serial == m_setup.phone; })) {
        first = m_setup.phone;
    } else if (!phones.empty()) {
        first = phones.front().serial;
    }
    setPhone(first);
}

VisionWorkbench::~VisionWorkbench()
{
    shutdown();
}

void VisionWorkbench::shutdown()
{
    abandonRequest();
    if (m_feeding && m_feed.feed) {
        m_feed.feed(false);
    }
    m_feeding = false;
    if (m_feed.frames != nullptr) {
        static_cast<void>(m_feed.frames->take());
    }
    if (m_video != nullptr) {
        m_video->setSource(nullptr);
    }
    m_feed = {};
    VisionImageProvider::clear();
}

void VisionWorkbench::load()
{
    const script::LoadedResources loaded = script::loadResources(m_res / "resources.json");
    if (!loaded) {
        m_blocked = true;
        m_saved = false;
        m_problem = tr("res/resources.json has a problem, so changes are not saved until it is fixed: %1")
                        .arg(text(loaded.error().empty() ? std::string() : loaded.error().front()));
        return;
    }
    m_state.resources = *loaded;
    m_base = *loaded;
    for (const script::TemplateResource& entry : m_state.resources.templates) {
        QFile file(QString::fromStdWString((m_res / pathOf(entry.file)).wstring()));
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = file.read(kMostPictureBytes);
            m_state.images[entry.file] = bytes;
            m_onDisk[entry.file] = bytes;
        }
    }
}

QSize VisionWorkbench::pictureSize() const
{
    return m_frozen ? m_frozenImage.size() : m_liveSize;
}

QString VisionWorkbench::frozenKey() const
{
    return m_frozenImage.isNull() ? QString() : u"f%1"_s.arg(m_frozenRevision);
}

QVariantList VisionWorkbench::phones() const
{
    QVariantList list;
    if (m_setup.phones) {
        for (const VisionPhone& phone : m_setup.phones()) {
            list.append(QVariantMap{{u"serial"_s, phone.serial}, {u"name"_s, phone.name}});
        }
    }
    return list;
}

QVariantList VisionWorkbench::templates() const
{
    QVariantList list;
    for (const script::TemplateResource& entry : m_state.resources.templates) {
        QVariantMap item{{u"kind"_s, u"template"_s},
                         {u"name"_s, text(entry.name)},
                         {u"file"_s, text(entry.file)},
                         {u"threshold"_s, entry.threshold},
                         {u"match"_s, entry.match == script::MatchMode::Color ? u"color"_s : u"grey"_s},
                         {u"region"_s, text(entry.region)},
                         {u"sourceWidth"_s, entry.source.width},
                         {u"sourceHeight"_s, entry.source.height}};
        const auto image = m_state.images.find(entry.file);
        const QSize size = image != m_state.images.end() ? pictureSizeOf(image->second) : QSize();
        item.insert(u"picture"_s, image != m_state.images.end() ? pictureKey(image->second) : QString());
        item.insert(u"width"_s, size.width());
        item.insert(u"height"_s, size.height());
        if (const auto cut = m_state.cuts.find(entry.name); cut != m_state.cuts.end()) {
            item.insert(u"cut"_s, areaMap(cut->second.x(), cut->second.y(), cut->second.width(), cut->second.height()));
        }
        list.append(item);
    }
    return list;
}

QVariantList VisionWorkbench::regions() const
{
    QVariantList list;
    for (const script::RegionResource& entry : m_state.resources.regions) {
        QVariantMap item = areaMap(entry.area.x, entry.area.y, entry.area.width, entry.area.height);
        item.insert(u"kind"_s, u"region"_s);
        item.insert(u"name"_s, text(entry.name));
        list.append(item);
    }
    return list;
}

QVariantList VisionWorkbench::points() const
{
    QVariantList list;
    for (const script::PointResource& entry : m_state.resources.points) {
        list.append(QVariantMap{
            {u"kind"_s, u"point"_s}, {u"name"_s, text(entry.name)}, {u"x"_s, entry.at.x}, {u"y"_s, entry.at.y}});
    }
    return list;
}

QVariantList VisionWorkbench::paths() const
{
    QVariantList list;
    for (const script::PathResource& entry : m_state.resources.paths) {
        QVariantList points;
        for (const script::Point& point : entry.points) {
            points.append(QVariantMap{{u"x"_s, point.x}, {u"y"_s, point.y}});
        }
        list.append(QVariantMap{{u"kind"_s, u"path"_s},
                                {u"name"_s, text(entry.name)},
                                {u"points"_s, points},
                                {u"durationMs"_s, entry.durationMs}});
    }
    return list;
}

QVariantList VisionWorkbench::colors() const
{
    QVariantList list;
    for (const script::ColorResource& entry : m_state.resources.colors) {
        list.append(QVariantMap{{u"kind"_s, u"color"_s},
                                {u"name"_s, text(entry.name)},
                                {u"color"_s, text(script::colorText(entry.color))},
                                {u"tolerance"_s, entry.tolerance},
                                {u"region"_s, text(entry.region)}});
    }
    return list;
}

QVariantMap VisionWorkbench::selected() const
{
    if (m_kind.isEmpty()) {
        return {};
    }
    const QVariantList list = m_kind == u"template" ? templates()
                            : m_kind == u"region"   ? regions()
                            : m_kind == u"point"    ? points()
                            : m_kind == u"path"     ? paths()
                                                    : colors();
    for (const QVariant& item : list) {
        const QVariantMap entry = item.toMap();
        if (entry.value(u"name"_s).toString() == m_name) {
            return entry;
        }
    }
    return {};
}

QByteArray VisionWorkbench::imageOf(const QString& templateName) const
{
    const script::TemplateResource* entry = m_state.resources.findTemplate(utf8(templateName));
    if (entry == nullptr) {
        return {};
    }
    const auto image = m_state.images.find(entry->file);
    return image != m_state.images.end() ? image->second : QByteArray();
}

void VisionWorkbench::attachVideo(QQuickItem* host)
{
    if (host == nullptr || m_video != nullptr) {
        return;
    }
    auto* video = new render::VideoItem(host);
    video->setSize(host->size());
    connect(host, &QQuickItem::widthChanged, video, [host, video] { video->setSize(host->size()); });
    connect(host, &QQuickItem::heightChanged, video, [host, video] { video->setSize(host->size()); });
    connect(video, &render::VideoItem::contentRectChanged, this, &VisionWorkbench::followVideo, Qt::QueuedConnection);
    video->setSource(m_feed.frames);
    m_video = video;
    feedLive();
}

void VisionWorkbench::followVideo()
{
    if (m_video == nullptr) {
        return;
    }
    const QSize size = m_video->frameSize();
    if (size == m_liveSize) {
        return;
    }
    m_liveSize = size;
    emit pictureChanged();
}

void VisionWorkbench::setPhone(const QString& serial)
{
    if (serial == m_phone && (serial.isEmpty() || m_feed.frames != nullptr)) {
        return;
    }
    if (m_feeding && m_feed.feed) {
        m_feed.feed(false);
    }
    m_feeding = false;
    if (m_feed.frames != nullptr) {
        static_cast<void>(m_feed.frames->take());
    }
    m_phone = serial;
    m_feed = serial.isEmpty() || !m_setup.feedOf ? VisionFeed{} : m_setup.feedOf(serial);
    m_liveSize = {};
    if (m_video != nullptr) {
        m_video->setSource(m_feed.frames);
    }
    feedLive();
    clearResults();
    emit phoneChanged();
    emit pictureChanged();
}

void VisionWorkbench::refreshPhones()
{
    const std::vector<VisionPhone> phones = m_setup.phones ? m_setup.phones() : std::vector<VisionPhone>{};
    if (!std::ranges::any_of(phones, [this](const VisionPhone& phone) { return phone.serial == m_phone; })) {
        setPhone(phones.empty() ? QString() : phones.front().serial);
    }
    emit phonesChanged();
}

void VisionWorkbench::feedLive()
{
    const bool wanted = !m_frozen && m_video != nullptr && m_feed.feed != nullptr;
    if (wanted == m_feeding) {
        return;
    }
    m_feeding = wanted;
    if (m_feed.feed) {
        m_feed.feed(wanted);
    }
    if (!wanted && m_feed.frames != nullptr) {
        static_cast<void>(m_feed.frames->take());
    }
}

template <typename Work, typename Done>
void VisionWorkbench::later(Work work, Done done)
{
    setBusy(true);
    const QPointer<VisionWorkbench> self(this);
    QThreadPool::globalInstance()->start([work, done, self] {
        const auto made = work();
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [done, self, made] {
                if (self != nullptr) {
                    self->setBusy(false);
                    done(*self, made);
                }
            },
            Qt::QueuedConnection);
    });
}

void VisionWorkbench::setFrozen(bool frozen)
{
    if (!frozen) {
        if (m_frozen) {
            m_frozen = false;
            feedLive();
            clearResults();
            emit frozenChanged();
            emit pictureChanged();
        }
        return;
    }
    if (m_frozen || busy()) {
        return;
    }
    const std::shared_ptr<media::FrameSnapshots> snapshots = m_feed.snapshots;
    if (snapshots == nullptr) {
        emit notice(tr("No phone is mirroring, so there is no picture to freeze"), u"warn"_s);
        emit frozenChanged();
        return;
    }
    struct Captured {
        std::shared_ptr<const media::LumaSnapshot> shot;
        QImage picture;
    };
    later(
        [snapshots] {
            Captured made;
            made.shot = snapshots->capture(true);
            if (made.shot != nullptr) {
                made.picture = pictureOf(*made.shot);
            }
            return made;
        },
        [](VisionWorkbench& bench, const Captured& made) {
            if (made.shot == nullptr || made.picture.isNull()) {
                emit bench.notice(VisionWorkbench::tr("There is no picture to freeze yet"), u"warn"_s);
                emit bench.frozenChanged();
                return;
            }
            bench.freezeShot(made.shot, made.picture);
        });
}

void VisionWorkbench::freezeShot(std::shared_ptr<const media::LumaSnapshot> shot, QImage picture)
{
    m_frozenShot = std::move(shot);
    m_frozenImage = std::move(picture);
    ++m_frozenRevision;
    VisionImageProvider::publish(frozenKey(), m_frozenImage);
    m_frozen = true;
    feedLive();
    clearResults();
    emit frozenChanged();
    emit pictureChanged();
}

QString VisionWorkbench::addTemplate(double x, double y, double width, double height)
{
    if (!m_frozen || m_frozenImage.isNull()) {
        emit notice(tr("Freeze the picture to cut a template from it"), u"warn"_s);
        return {};
    }
    const QSize size = m_frozenImage.size();
    const QRect pixels = pixelRect(clampRatio(x), clampRatio(y), clampRatio(width), clampRatio(height), size);
    if (pixels.width() < kLeastTemplateSide || pixels.height() < kLeastTemplateSide) {
        emit notice(tr("Draw a larger box"), u"warn"_s);
        return {};
    }
    const QImage cut = m_frozenImage.copy(pixels);
    std::string problem;
    if (templateOf(cut, false, {}, problem) == nullptr) {
        emit notice(tr("This box holds a single color; draw it around something with detail"), u"warn"_s);
        return {};
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !cut.save(&buffer, "PNG")) {
        emit notice(tr("The template could not be made"), u"bad"_s);
        return {};
    }
    const QString name = freeName(u"template"_s);
    const std::string file = fileFor(name, {});
    State next = m_state;
    next.resources.templates.push_back({.name = utf8(name),
                                        .file = file,
                                        .threshold = kNewTemplateThreshold,
                                        .match = script::MatchMode::Color,
                                        .region = {},
                                        .source = {size.width(), size.height()}});
    next.images[file] = bytes;
    next.cuts[utf8(name)] = QRectF(static_cast<double>(pixels.x()) / size.width(),
                                   static_cast<double>(pixels.y()) / size.height(),
                                   static_cast<double>(pixels.width()) / size.width(),
                                   static_cast<double>(pixels.height()) / size.height());
    m_kind = u"template"_s;
    m_name = name;
    commit(std::move(next));
    const QString kept = requestFilled(u"template"_s, name);
    picked(u"image"_s, kept);
    return kept;
}

QString VisionWorkbench::addRegion(double x, double y, double width, double height)
{
    const double left = rounded(clampRatio(x));
    const double top = rounded(clampRatio(y));
    const double right = rounded(clampRatio(x + width));
    const double bottom = rounded(clampRatio(y + height));
    if (right - left < kLeastRatio || bottom - top < kLeastRatio) {
        emit notice(tr("Draw a larger box"), u"warn"_s);
        return {};
    }
    const QString name = freeName(u"region"_s);
    State next = m_state;
    next.resources.regions.push_back({utf8(name), {left, top, rounded(right - left), rounded(bottom - top)}});
    m_kind = u"region"_s;
    m_name = name;
    commit(std::move(next));
    const QString kept = requestFilled(u"region"_s, name);
    picked(u"region"_s, kept);
    return kept;
}

QString VisionWorkbench::addPoint(double x, double y)
{
    const QString name = freeName(u"point"_s);
    State next = m_state;
    next.resources.points.push_back({utf8(name), {rounded(clampRatio(x)), rounded(clampRatio(y))}});
    m_kind = u"point"_s;
    m_name = name;
    commit(std::move(next));
    const QString kept = requestFilled(u"point"_s, name);
    picked(u"point"_s, kept);
    return kept;
}

void VisionWorkbench::startPick(const QString& kind, const QString& prompt, std::function<void(const QString&)> done)
{
    m_pick = QVariantMap{{u"kind"_s, kind}, {u"prompt"_s, prompt}};
    m_picked = std::move(done);
    emit pickChanged();
}

void VisionWorkbench::cancelPick()
{
    if (m_pick.isEmpty()) {
        return;
    }
    m_pick.clear();
    m_picked = nullptr;
    emit pickChanged();
}

void VisionWorkbench::picked(const QString& kind, const QString& name)
{
    if (m_pick.value(u"kind"_s).toString() != kind || !m_picked) {
        return;
    }
    const std::function<void(const QString&)> done = std::exchange(m_picked, nullptr);
    m_pick.clear();
    emit pickChanged();
    emit notice(tr("Filled in %1").arg(name), u"info"_s);
    done(name);
}

void VisionWorkbench::beginRequest(std::vector<VisionRequestItem> items, VisionRequestDone done)
{
    for (VisionRequestItem& item : items) {
        item.state = u"pending"_s;
        item.resource.clear();
    }
    m_request = std::move(items);
    m_requestDone = std::move(done);
    m_requestCurrent = -1;
    advanceRequest();
}

void VisionWorkbench::abandonRequest()
{
    if (m_request.empty()) {
        return;
    }
    const std::vector<VisionRequestResult> results = requestResults();
    const VisionRequestDone done = std::exchange(m_requestDone, nullptr);
    m_request.clear();
    m_requestCurrent = -1;
    emit requestChanged();
    emit requestCurrentChanged();
    if (done) {
        done(results);
    }
}

void VisionWorkbench::giveRequest()
{
    if (m_request.empty() || !requestAnswered()) {
        return;
    }
    abandonRequest();
}

void VisionWorkbench::chooseRequest(int index)
{
    if (index < 0 || index >= static_cast<int>(m_request.size()) || index == m_requestCurrent) {
        return;
    }
    if (m_request[static_cast<std::size_t>(index)].state != u"pending"_s) {
        return;
    }
    m_requestCurrent = index;
    emit requestChanged();
    emit requestCurrentChanged();
}

void VisionWorkbench::skipRequest(int index)
{
    if (index < 0 || index >= static_cast<int>(m_request.size())) {
        return;
    }
    VisionRequestItem& item = m_request[static_cast<std::size_t>(index)];
    if (item.state != u"pending"_s) {
        return;
    }
    item.state = u"skipped"_s;
    item.resource.clear();
    if (index == m_requestCurrent) {
        advanceRequest();
    } else {
        emit requestChanged();
    }
}

void VisionWorkbench::useExistingRequest(int index, const QString& name)
{
    if (index < 0 || index >= static_cast<int>(m_request.size())) {
        return;
    }
    VisionRequestItem& item = m_request[static_cast<std::size_t>(index)];
    if (item.state != u"pending"_s || !exists(item.kind, name)) {
        return;
    }
    item.state = u"existing"_s;
    item.resource = name;
    if (index == m_requestCurrent) {
        advanceRequest();
    } else {
        emit requestChanged();
    }
}

QVariantList VisionWorkbench::requestItems() const
{
    QVariantList list;
    for (std::size_t index = 0; index < m_request.size(); ++index) {
        const VisionRequestItem& item = m_request[index];
        list.append(QVariantMap{{u"name"_s, item.name},
                                {u"kind"_s, item.kind},
                                {u"note"_s, item.note},
                                {u"state"_s, item.state},
                                {u"resource"_s, item.resource},
                                {u"current"_s, static_cast<int>(index) == m_requestCurrent},
                                {u"existing"_s, existingOf(item.kind)}});
    }
    return list;
}

bool VisionWorkbench::requestAnswered() const
{
    return !m_request.empty()
        && std::ranges::none_of(m_request, [](const VisionRequestItem& item) { return item.state == u"pending"; });
}

int VisionWorkbench::requestDoneCount() const
{
    return static_cast<int>(std::ranges::count_if(
        m_request, [](const VisionRequestItem& item) { return item.state != u"pending"; }));
}

QStringList VisionWorkbench::existingOf(const QString& kind) const
{
    QStringList names;
    if (kind == u"template") {
        for (const script::TemplateResource& entry : m_state.resources.templates) {
            names.append(text(entry.name));
        }
    } else if (kind == u"region") {
        for (const script::RegionResource& entry : m_state.resources.regions) {
            names.append(text(entry.name));
        }
    } else if (kind == u"point") {
        for (const script::PointResource& entry : m_state.resources.points) {
            names.append(text(entry.name));
        }
    } else if (kind == u"color") {
        for (const script::ColorResource& entry : m_state.resources.colors) {
            names.append(text(entry.name));
        }
    }
    return names;
}

void VisionWorkbench::advanceRequest()
{
    int current = -1;
    for (std::size_t index = 0; index < m_request.size(); ++index) {
        if (m_request[index].state == u"pending"_s) {
            current = static_cast<int>(index);
            break;
        }
    }
    const bool moved = current != m_requestCurrent;
    m_requestCurrent = current;
    emit requestChanged();
    if (moved) {
        emit requestCurrentChanged();
    }
}

QString VisionWorkbench::requestFilled(const QString& kind, const QString& name)
{
    if (m_request.empty()) {
        return name;
    }
    int index = -1;
    if (m_requestCurrent >= 0 && m_requestCurrent < static_cast<int>(m_request.size())) {
        const VisionRequestItem& current = m_request[static_cast<std::size_t>(m_requestCurrent)];
        if (current.state == u"pending"_s && current.kind == kind) {
            index = m_requestCurrent;
        }
    }
    if (index < 0) {
        for (std::size_t at = 0; at < m_request.size(); ++at) {
            if (m_request[at].state == u"pending"_s && m_request[at].kind == kind) {
                index = static_cast<int>(at);
                break;
            }
        }
    }
    if (index < 0) {
        return name;
    }
    VisionRequestItem& item = m_request[static_cast<std::size_t>(index)];
    QString kept = name;
    const QString wanted = requestNameFor(item);
    if (!wanted.isEmpty() && wanted != name && renameProblem(kind, name, wanted).isEmpty()) {
        renameNow(kind, name, wanted, {});
        kept = wanted;
    }
    item.state = u"done"_s;
    item.resource = kept;
    advanceRequest();
    return kept;
}

QVariantMap VisionWorkbench::requestValueOf(const VisionRequestItem& item) const
{
    const std::string name = utf8(item.resource);
    if (item.kind == u"region") {
        if (const script::RegionResource* entry = m_state.resources.findRegion(name)) {
            return areaMap(entry->area.x, entry->area.y, entry->area.width, entry->area.height);
        }
        return {};
    }
    if (item.kind == u"point") {
        if (const script::PointResource* entry = m_state.resources.findPoint(name)) {
            return {{u"x"_s, entry->at.x}, {u"y"_s, entry->at.y}};
        }
        return {};
    }
    if (item.kind == u"color") {
        if (const script::ColorResource* entry = m_state.resources.findColor(name)) {
            return {{u"color"_s, text(script::colorText(entry->color))}, {u"tolerance"_s, entry->tolerance}};
        }
        return {};
    }
    if (const script::TemplateResource* entry = m_state.resources.findTemplate(name)) {
        const QSize size = QImageReader(QString::fromStdWString(m_res.wstring()) + u'/' + text(entry->file)).size();
        return {{u"file"_s, QStringLiteral("res/") + text(entry->file)},
                {u"width"_s, size.width()},
                {u"height"_s, size.height()},
                {u"picture"_s, QVariantMap{{u"width"_s, entry->source.width}, {u"height"_s, entry->source.height}}},
                {u"threshold"_s, entry->threshold}};
    }
    return {};
}

std::vector<VisionRequestResult> VisionWorkbench::requestResults() const
{
    std::vector<VisionRequestResult> results;
    results.reserve(m_request.size());
    for (const VisionRequestItem& item : m_request) {
        const bool pending = item.state == u"pending";
        results.push_back({.name = item.name,
                           .kind = item.kind,
                           .state = pending ? u"skipped"_s : item.state,
                           .resource = pending ? QString() : item.resource,
                           .value = pending ? QVariantMap() : requestValueOf(item)});
    }
    return results;
}

QString VisionWorkbench::requestNameFor(const VisionRequestItem& item) const
{
    QString name;
    for (const QChar character : item.name.trimmed()) {
        name += character == u'/' || character == u'\\' || character == u':' || !character.isPrint() ? u' '
                                                                                                   : character;
    }
    name = name.simplified();
    return script::resourceName(name.toStdString()) ? name : QString();
}

QString VisionWorkbench::addPath(const QVariantList& points)
{
    std::vector<script::Point> made;
    for (const QVariant& point : points) {
        const QVariantMap at = point.toMap();
        made.push_back({rounded(clampRatio(at.value(u"x"_s).toDouble())), rounded(clampRatio(at.value(u"y"_s).toDouble()))});
    }
    if (made.size() < 2 || made.size() > script::kMostPathPoints) {
        emit notice(tr("A path has 2 to %1 points").arg(kMostPathPoints), u"warn"_s);
        return {};
    }
    const QString name = freeName(u"path"_s);
    State next = m_state;
    next.resources.paths.push_back({utf8(name), std::move(made), script::kDefaultPathMs});
    m_kind = u"path"_s;
    m_name = name;
    commit(std::move(next));
    return name;
}

QString VisionWorkbench::pickColor(double x, double y)
{
    if (!m_frozen || m_frozenImage.isNull()) {
        emit notice(tr("Freeze the picture to pick a color from it"), u"warn"_s);
        return {};
    }
    const QSize size = m_frozenImage.size();
    const int column = std::clamp(static_cast<int>(std::floor(clampRatio(x) * size.width())), 0, size.width() - 1);
    const int row = std::clamp(static_cast<int>(std::floor(clampRatio(y) * size.height())), 0, size.height() - 1);
    const QColor seen = m_frozenImage.pixelColor(column, row);
    const QString name = freeName(u"color"_s);
    State next = m_state;
    next.resources.colors.push_back({utf8(name),
                                     {static_cast<std::uint8_t>(seen.red()), static_cast<std::uint8_t>(seen.green()),
                                      static_cast<std::uint8_t>(seen.blue())},
                                     script::kDefaultTolerance,
                                     {}});
    m_kind = u"color"_s;
    m_name = name;
    commit(std::move(next));
    return requestFilled(u"color"_s, name);
}

void VisionWorkbench::select(const QString& kind, const QString& name)
{
    const bool known = !kind.isEmpty() && exists(kind, name);
    const QString chosenKind = known ? kind : QString();
    const QString chosenName = known ? name : QString();
    if (chosenKind == m_kind && chosenName == m_name) {
        return;
    }
    m_kind = chosenKind;
    m_name = chosenName;
    clearResults();
    emit selectionChanged();
}

QString VisionWorkbench::rename(const QString& kind, const QString& name, const QString& wanted)
{
    const QString trimmed = wanted.trimmed();
    if (trimmed == name) {
        return {};
    }
    if (const QString problem = renameProblem(kind, name, trimmed); !problem.isEmpty()) {
        return problem;
    }
    if (!askAboutLogic(kind, name, trimmed)) {
        renameNow(kind, name, trimmed, {});
    }
    return {};
}

QString VisionWorkbench::finishRename(bool alsoLogic)
{
    if (m_renaming.isEmpty()) {
        return {};
    }
    const QString kind = m_renaming.value(u"kind"_s).toString();
    const QString name = m_renaming.value(u"name"_s).toString();
    const QString wanted = m_renaming.value(u"wanted"_s).toString();
    m_renaming.clear();
    emit renamingChanged();
    if (const QString problem = renameProblem(kind, name, wanted); !problem.isEmpty()) {
        return problem;
    }
    renameNow(kind, name, wanted, alsoLogic ? changeLogic(name, wanted) : LogicChange{});
    return {};
}

void VisionWorkbench::cancelRename()
{
    if (m_renaming.isEmpty()) {
        return;
    }
    m_renaming.clear();
    emit renamingChanged();
}

QString VisionWorkbench::renameProblem(const QString& kind, const QString& name, const QString& wanted) const
{
    if (!exists(kind, name)) {
        return tr("This item no longer exists");
    }
    if (!script::resourceName(utf8(wanted))) {
        return tr("A name has 1 to 64 characters without / \\ or :, and does not end in .png, .jpg, .jpeg or .bmp");
    }
    if (exists(kind, wanted)) {
        return tr("Another item of this kind already has this name");
    }
    return {};
}

bool VisionWorkbench::askAboutLogic(const QString& kind, const QString& name, const QString& wanted)
{
    const ReferenceSearch search = findLogicReferences(m_setup.folder, {.text = utf8(name), .names = false});
    if (search.found.empty()) {
        return false;
    }
    const QStringList unsaved = unsavedFiles();
    QVariantList places;
    QStringList left;
    for (const LogicReference& place : search.found) {
        const bool kept = listedPath(unsaved, place.file);
        if (kept && !left.contains(place.file)) {
            left.append(place.file);
        }
        if (places.size() < kMostRenamePlaces) {
            places.append(QVariantMap{{u"file"_s, place.file},
                                      {u"line"_s, place.line},
                                      {u"column"_s, place.column},
                                      {u"place"_s, u"%1:%2"_s.arg(place.file).arg(place.line)},
                                      {u"text"_s, place.lineText.trimmed()},
                                      {u"unsaved"_s, kept}});
        }
    }
    const auto count = static_cast<int>(search.found.size());
    m_renaming = QVariantMap{{u"kind"_s, kind},
                             {u"name"_s, name},
                             {u"wanted"_s, wanted},
                             {u"count"_s, count},
                             {u"more"_s, count - static_cast<int>(places.size())},
                             {u"places"_s, places},
                             {u"unsaved"_s, left},
                             {u"shared"_s, sharedName(kind, name)}};
    emit renamingChanged();
    return true;
}

VisionWorkbench::LogicChange VisionWorkbench::changeLogic(const QString& name, const QString& wanted)
{
    LogicChange change{.from = utf8(name), .to = utf8(wanted), .files = {}};
    const QStringList unsaved = unsavedFiles();
    const QStringList files = logicFilesIn(m_setup.folder);
    QStringList unwritten;
    for (const QString& file : files) {
        if (listedPath(unsaved, file)) {
            continue;
        }
        const std::filesystem::path path = m_setup.folder / file.toStdWString();
        std::string before;
        if (platform::readFile(path, kMostLogicFileBytes, before)) {
            continue;
        }
        const LiteralRename renamed = renameLiterals(before, change.from, change.to);
        if (renamed.changed == 0) {
            continue;
        }
        if (platform::writeFileAtomically(path, renamed.text)) {
            unwritten.append(file);
            continue;
        }
        change.files.push_back(
            {.file = file, .before = QByteArray::fromStdString(before), .afterHash = digestOf(renamed.text)});
    }
    if (!unwritten.isEmpty()) {
        const auto count = static_cast<int>(unwritten.size());
        emit notice(tr("%1 could not be written; its text still uses the old name", "", count).arg(listOf(unwritten)),
                    u"bad"_s);
    }
    return change;
}

void VisionWorkbench::undoLogic(const LogicChange& change)
{
    QStringList moved;
    QStringList unwritten;
    for (const LogicEdit& edit : change.files) {
        const std::filesystem::path path = m_setup.folder / edit.file.toStdWString();
        std::string now;
        if (platform::readFile(path, kMostLogicFileBytes, now) || digestOf(now) != edit.afterHash) {
            moved.append(edit.file);
        } else if (platform::writeFileAtomically(path, bytesOf(edit.before))) {
            unwritten.append(edit.file);
        }
    }
    QStringList parts;
    if (!moved.isEmpty()) {
        const auto count = static_cast<int>(moved.size());
        parts.append(tr("%1 changed since; its text was not changed back", "", count).arg(listOf(moved)));
    }
    if (!unwritten.isEmpty()) {
        const auto count = static_cast<int>(unwritten.size());
        parts.append(
            tr("%1 could not be written; its text still uses the new name", "", count).arg(listOf(unwritten)));
    }
    if (!parts.isEmpty()) {
        emit notice(parts.join(u' '), u"warn"_s);
    }
}

void VisionWorkbench::redoLogic(const LogicChange& change)
{
    QStringList moved;
    QStringList unwritten;
    for (const LogicEdit& edit : change.files) {
        const std::filesystem::path path = m_setup.folder / edit.file.toStdWString();
        std::string now;
        if (platform::readFile(path, kMostLogicFileBytes, now) || std::string_view(now) != bytesOf(edit.before)) {
            moved.append(edit.file);
            continue;
        }
        const LiteralRename renamed = renameLiterals(now, change.from, change.to);
        if (digestOf(renamed.text) != edit.afterHash) {
            moved.append(edit.file);
        } else if (platform::writeFileAtomically(path, renamed.text)) {
            unwritten.append(edit.file);
        }
    }
    QStringList parts;
    if (!moved.isEmpty()) {
        const auto count = static_cast<int>(moved.size());
        parts.append(tr("%1 changed since; its text was not changed", "", count).arg(listOf(moved)));
    }
    if (!unwritten.isEmpty()) {
        const auto count = static_cast<int>(unwritten.size());
        parts.append(
            tr("%1 could not be written; its text still uses the old name", "", count).arg(listOf(unwritten)));
    }
    if (!parts.isEmpty()) {
        emit notice(parts.join(u' '), u"warn"_s);
    }
}

QStringList VisionWorkbench::unsavedFiles() const
{
    return m_setup.unsaved ? m_setup.unsaved() : QStringList();
}

bool VisionWorkbench::sharedName(const QString& kind, const QString& name) const
{
    return std::ranges::any_of(std::array{u"template"_s, u"region"_s, u"point"_s, u"path"_s, u"color"_s},
                               [&](const QString& other) { return other != kind && exists(other, name); });
}

void VisionWorkbench::renameNow(const QString& kind, const QString& name, const QString& wanted, LogicChange logic)
{
    const std::string from = utf8(name);
    const std::string to = utf8(wanted);
    State next = m_state;
    if (kind == u"template") {
        script::TemplateResource* entry = itemNamed(next.resources.templates, from);
        const std::string file = fileFor(wanted, entry->file);
        if (file != entry->file) {
            if (const auto image = next.images.find(entry->file); image != next.images.end()) {
                const QByteArray bytes = image->second;
                next.images.erase(image);
                next.images[file] = bytes;
            }
            entry->file = file;
        }
        entry->name = to;
        if (const auto cut = next.cuts.find(from); cut != next.cuts.end()) {
            const QRectF area = cut->second;
            next.cuts.erase(cut);
            next.cuts[to] = area;
        }
    } else if (kind == u"region") {
        itemNamed(next.resources.regions, from)->name = to;
        for (script::TemplateResource& entry : next.resources.templates) {
            if (entry.region == from) {
                entry.region = to;
            }
        }
        for (script::ColorResource& entry : next.resources.colors) {
            if (entry.region == from) {
                entry.region = to;
            }
        }
    } else if (kind == u"point") {
        itemNamed(next.resources.points, from)->name = to;
    } else if (kind == u"path") {
        itemNamed(next.resources.paths, from)->name = to;
    } else {
        itemNamed(next.resources.colors, from)->name = to;
    }
    if (m_kind == kind && m_name == name) {
        m_name = wanted;
    }
    commit(std::move(next), std::move(logic));
}

bool VisionWorkbench::setValue(const QString& kind, const QString& name, const QString& field, const QVariant& value)
{
    State next = m_state;
    const std::string key = utf8(name);
    bool ok = false;
    const auto ratioOf = [&value, &ok] {
        const double number = value.toDouble(&ok);
        ok = ok && number >= 0.0 && number <= 1.0;
        return rounded(number);
    };
    const auto regionOf = [&value, &ok, &next] {
        const std::string region = utf8(value.toString());
        ok = region.empty() || next.resources.findRegion(region) != nullptr;
        return region;
    };
    if (kind == u"template") {
        script::TemplateResource* entry = itemNamed(next.resources.templates, key);
        if (entry == nullptr) {
            return false;
        }
        if (field == u"threshold") {
            const double threshold = ratioOf();
            if (ok) {
                entry->threshold = threshold;
            }
        } else if (field == u"match") {
            const QString mode = value.toString();
            ok = mode == u"color" || mode == u"grey";
            entry->match = mode == u"color" ? script::MatchMode::Color : script::MatchMode::Grey;
        } else if (field == u"region") {
            const std::string region = regionOf();
            if (ok) {
                entry->region = region;
            }
        }
    } else if (kind == u"region") {
        script::RegionResource* entry = itemNamed(next.resources.regions, key);
        if (entry == nullptr) {
            return false;
        }
        script::Region area = entry->area;
        const double number = ratioOf();
        if (field == u"x") {
            area.x = number;
        } else if (field == u"y") {
            area.y = number;
        } else if (field == u"width") {
            area.width = number;
        } else if (field == u"height") {
            area.height = number;
        } else {
            ok = false;
        }
        ok = ok && area.width >= kLeastRatio && area.height >= kLeastRatio && area.x + area.width <= 1.0 + 1e-9
          && area.y + area.height <= 1.0 + 1e-9;
        if (ok) {
            entry->area = area;
        }
    } else if (kind == u"point") {
        script::PointResource* entry = itemNamed(next.resources.points, key);
        if (entry == nullptr) {
            return false;
        }
        const double number = ratioOf();
        if (ok && field == u"x") {
            entry->at.x = number;
        } else if (ok && field == u"y") {
            entry->at.y = number;
        } else {
            ok = false;
        }
    } else if (kind == u"path") {
        script::PathResource* entry = itemNamed(next.resources.paths, key);
        if (entry == nullptr) {
            return false;
        }
        if (field == u"durationMs") {
            const double duration = value.toDouble(&ok);
            ok = ok && duration > 0.0 && duration <= script::kLongestPathMs;
            if (ok) {
                entry->durationMs = std::round(duration);
            }
        } else if (field == u"points") {
            std::vector<script::Point> made;
            for (const QVariant& point : value.toList()) {
                const QVariantMap at = point.toMap();
                made.push_back({rounded(clampRatio(at.value(u"x"_s).toDouble())),
                                rounded(clampRatio(at.value(u"y"_s).toDouble()))});
            }
            ok = made.size() >= 2 && made.size() <= script::kMostPathPoints;
            if (ok) {
                entry->points = std::move(made);
            }
        }
    } else if (kind == u"color") {
        script::ColorResource* entry = itemNamed(next.resources.colors, key);
        if (entry == nullptr) {
            return false;
        }
        if (field == u"color") {
            const std::optional<script::Rgb> parsed = script::parseColor(utf8(value.toString().trimmed()));
            ok = parsed.has_value();
            if (ok) {
                entry->color = *parsed;
            }
        } else if (field == u"tolerance") {
            const double tolerance = ratioOf();
            if (ok) {
                entry->tolerance = tolerance;
            }
        } else if (field == u"region") {
            const std::string region = regionOf();
            if (ok) {
                entry->region = region;
            }
        }
    }
    if (!ok) {
        return false;
    }
    if (!(next.resources == m_state.resources)) {
        commit(std::move(next));
    }
    return true;
}

bool VisionWorkbench::setArea(const QString& name, double x, double y, double width, double height)
{
    State next = m_state;
    script::RegionResource* entry = itemNamed(next.resources.regions, utf8(name));
    if (entry == nullptr) {
        return false;
    }
    const double left = rounded(clampRatio(x));
    const double top = rounded(clampRatio(y));
    const double right = rounded(clampRatio(x + width));
    const double bottom = rounded(clampRatio(y + height));
    if (right - left < kLeastRatio || bottom - top < kLeastRatio) {
        return false;
    }
    entry->area = {left, top, rounded(right - left), rounded(bottom - top)};
    if (!(next.resources == m_state.resources)) {
        commit(std::move(next));
    }
    return true;
}

bool VisionWorkbench::movePoint(const QString& name, double x, double y)
{
    State next = m_state;
    script::PointResource* entry = itemNamed(next.resources.points, utf8(name));
    if (entry == nullptr) {
        return false;
    }
    entry->at = {rounded(clampRatio(x)), rounded(clampRatio(y))};
    if (!(next.resources == m_state.resources)) {
        commit(std::move(next));
    }
    return true;
}

bool VisionWorkbench::movePath(const QString& name, double dx, double dy)
{
    State next = m_state;
    script::PathResource* entry = itemNamed(next.resources.paths, utf8(name));
    if (entry == nullptr || entry->points.empty() || !std::isfinite(dx) || !std::isfinite(dy)) {
        return false;
    }
    const auto [left, right] = std::ranges::minmax(entry->points, {}, &script::Point::x);
    const auto [top, bottom] = std::ranges::minmax(entry->points, {}, &script::Point::y);
    const double shiftX = std::clamp(dx, -left.x, 1.0 - right.x);
    const double shiftY = std::clamp(dy, -top.y, 1.0 - bottom.y);
    for (script::Point& point : entry->points) {
        point = {rounded(clampRatio(point.x + shiftX)), rounded(clampRatio(point.y + shiftY))};
    }
    if (!(next.resources == m_state.resources)) {
        commit(std::move(next));
    }
    return true;
}

bool VisionWorkbench::remove(const QString& kind, const QString& name)
{
    State next = m_state;
    const std::string key = utf8(name);
    bool removed = false;
    if (kind == u"template") {
        if (const script::TemplateResource* entry = next.resources.findTemplate(key)) {
            next.images.erase(entry->file);
            next.cuts.erase(key);
            removed = eraseNamed(next.resources.templates, key);
        }
    } else if (kind == u"region") {
        removed = eraseNamed(next.resources.regions, key);
        for (script::TemplateResource& entry : next.resources.templates) {
            if (entry.region == key) {
                entry.region.clear();
            }
        }
        for (script::ColorResource& entry : next.resources.colors) {
            if (entry.region == key) {
                entry.region.clear();
            }
        }
    } else if (kind == u"point") {
        removed = eraseNamed(next.resources.points, key);
    } else if (kind == u"path") {
        removed = eraseNamed(next.resources.paths, key);
    } else if (kind == u"color") {
        removed = eraseNamed(next.resources.colors, key);
    }
    if (!removed) {
        return false;
    }
    commit(std::move(next));
    return true;
}

void VisionWorkbench::undo()
{
    if (m_undo.empty()) {
        return;
    }
    m_redo.push_back(std::move(m_state));
    m_state = std::move(m_undo.back());
    m_undo.pop_back();
    undoLogic(m_redo.back().logic);
    changed();
}

void VisionWorkbench::redo()
{
    if (m_redo.empty()) {
        return;
    }
    m_undo.push_back(std::move(m_state));
    m_state = std::move(m_redo.back());
    m_redo.pop_back();
    redoLogic(m_state.logic);
    changed();
}

void VisionWorkbench::testFind()
{
    std::function<TestOutcome(const media::LumaSnapshot&)> job;
    const auto regionNamed = [this](const std::string& name) {
        const script::RegionResource* region = name.empty() ? nullptr : m_state.resources.findRegion(name);
        return region != nullptr ? region->area : script::Region{};
    };
    if (m_kind == u"template") {
        const script::TemplateResource* entry = m_state.resources.findTemplate(utf8(m_name));
        const auto image = entry != nullptr ? m_state.images.find(entry->file) : m_state.images.end();
        if (entry == nullptr || image == m_state.images.end()) {
            emit notice(tr("The picture of this template is missing from res/"), u"bad"_s);
            return;
        }
        job = [bytes = image->second, resource = *entry, region = regionNamed(entry->region),
               name = m_name](const media::LumaSnapshot& shot) { return testTemplate(shot, bytes, resource, region, name); };
    } else if (m_kind == u"color") {
        const script::ColorResource* entry = m_state.resources.findColor(utf8(m_name));
        if (entry == nullptr) {
            return;
        }
        job = [resource = *entry, region = regionNamed(entry->region), name = m_name](const media::LumaSnapshot& shot) {
            return testColor(shot, resource, region, name);
        };
    } else {
        emit notice(tr("Select a template or a color to test"), u"warn"_s);
        return;
    }
    const std::shared_ptr<const media::LumaSnapshot> still = m_frozen ? m_frozenShot : nullptr;
    const std::shared_ptr<media::FrameSnapshots> snapshots = m_feed.snapshots;
    if (still == nullptr && snapshots == nullptr) {
        emit notice(tr("No phone is mirroring, so there is no picture to test on"), u"warn"_s);
        return;
    }
    later(
        [job, still, snapshots] {
            const std::shared_ptr<const media::LumaSnapshot> shot = still != nullptr ? still : snapshots->capture(true);
            if (shot == nullptr) {
                TestOutcome none;
                none.summary.insert(u"problem"_s, VisionWorkbench::tr("There is no picture to test on yet"));
                return none;
            }
            return job(*shot);
        },
        [](VisionWorkbench& bench, const TestOutcome& outcome) { bench.showOutcome(outcome.results, outcome.summary); });
}

void VisionWorkbench::showOutcome(const QVariantList& results, const QVariantMap& summary)
{
    m_results = results;
    m_result = summary;
    emit resultsChanged();
}

void VisionWorkbench::clearResults()
{
    if (m_results.isEmpty() && m_result.isEmpty()) {
        return;
    }
    m_results.clear();
    m_result.clear();
    emit resultsChanged();
}

void VisionWorkbench::commit(State next, LogicChange logic)
{
    next.logic = std::move(logic);
    m_undo.push_back(std::move(m_state));
    if (m_undo.size() > kMostUndo) {
        m_undo.erase(m_undo.begin());
    }
    m_redo.clear();
    m_state = std::move(next);
    changed();
}

void VisionWorkbench::changed()
{
    ++m_revision;
    publishPictures();
    if (!m_kind.isEmpty() && !exists(m_kind, m_name)) {
        m_kind.clear();
        m_name.clear();
    }
    clearResults();
    static_cast<void>(save());
    emit resourcesChanged();
    emit historyChanged();
    emit selectionChanged();
}

bool VisionWorkbench::save()
{
    if (m_blocked) {
        setSaved(false);
        return false;
    }
    const auto failed = [this](const std::error_code& why) {
        setSaved(false);
        setProblem(platform::fileBusy(why) ? tr("Not saved: another program is using a file in res/")
                                           : tr("Not saved: the files in res/ cannot be written"));
        return false;
    };
    const script::LoadedResources disk = script::loadResources(m_res / "resources.json");
    if (!disk) {
        setSaved(false);
        setProblem(tr("res/resources.json has a problem, so changes are not saved until it is fixed: %1")
                       .arg(text(disk.error().empty() ? std::string() : disk.error().front())));
        return false;
    }
    if (!(*disk == m_base)) {
        takeForeign(*disk);
    }
    std::error_code error;
    std::filesystem::create_directories(m_res, error);
    if (error) {
        return failed(error);
    }
    for (const auto& [file, bytes] : m_state.images) {
        if (const auto known = m_onDisk.find(file); known != m_onDisk.end() && known->second == bytes) {
            continue;
        }
        if (const std::error_code written = platform::writeFileAtomically(
                m_res / pathOf(file), std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())))) {
            return failed(written);
        }
        m_onDisk[file] = bytes;
    }
    if (const std::error_code written = script::saveResources(m_res / "resources.json", m_state.resources)) {
        return failed(written);
    }
    for (auto known = m_onDisk.begin(); known != m_onDisk.end();) {
        const QString file = text(known->first);
        const bool kept = std::ranges::any_of(m_state.images, [&file](const auto& image) {
            return QString::compare(text(image.first), file, Qt::CaseInsensitive) == 0;
        });
        if (kept) {
            ++known;
            continue;
        }
        static_cast<void>(platform::removeFile(m_res / pathOf(known->first)));
        known = m_onDisk.erase(known);
    }
    m_base = m_state.resources;
    setSaved(true);
    setProblem({});
    watchFiles();
    return true;
}

void VisionWorkbench::takeForeign(const script::Resources& disk)
{
    const script::Resources before = m_state.resources;
    State next = m_state;
    next.resources = mergeResources(m_base, m_state.resources, disk);
    std::map<std::string, QByteArray> pool = m_state.images;
    for (const script::TemplateResource& entry : next.resources.templates) {
        const script::TemplateResource* fromDisk = disk.findTemplate(entry.name);
        if (fromDisk == nullptr || !(*fromDisk == entry)) {
            continue;
        }
        QFile file(QString::fromStdWString((m_res / pathOf(entry.file)).wstring()));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QByteArray bytes = file.read(kMostPictureBytes);
        pool[entry.file] = bytes;
        m_onDisk[entry.file] = bytes;
    }
    const auto keepImages = [&pool, &next](State& state) {
        std::map<std::string, QByteArray> images;
        for (const script::TemplateResource& entry : state.resources.templates) {
            const script::TemplateResource* current = next.resources.findTemplate(entry.name);
            const bool same = current != nullptr && *current == entry;
            const auto own = state.images.find(entry.file);
            const auto shared = pool.find(entry.file);
            if ((same || own == state.images.end()) && shared != pool.end()) {
                images[entry.file] = shared->second;
            } else if (own != state.images.end()) {
                images[entry.file] = own->second;
            }
        }
        state.images = std::move(images);
        std::erase_if(state.cuts, [&state](const auto& cut) { return state.resources.findTemplate(cut.first) == nullptr; });
    };
    keepImages(next);
    for (std::vector<State>* history : {&m_undo, &m_redo}) {
        for (State& kept : *history) {
            kept.resources = mergeResources(before, kept.resources, next.resources);
            keepImages(kept);
        }
    }
    m_state = std::move(next);
    m_base = disk;
    ++m_revision;
    publishPictures();
    if (!m_kind.isEmpty() && !exists(m_kind, m_name)) {
        m_kind.clear();
        m_name.clear();
    }
    clearResults();
    emit resourcesChanged();
    emit historyChanged();
    emit selectionChanged();
}

void VisionWorkbench::reloadFromDisk()
{
    watchFiles();
    const script::LoadedResources disk = script::loadResources(m_res / "resources.json");
    if (!disk) {
        return;
    }
    if (m_blocked) {
        m_blocked = false;
        setProblem({});
    } else if (*disk == m_base) {
        return;
    }
    takeForeign(*disk);
    if (m_state.resources == m_base) {
        setSaved(true);
        setProblem({});
    } else {
        static_cast<void>(save());
    }
}

void VisionWorkbench::watchFiles()
{
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    QStringList missing;
    for (const std::filesystem::path& path : {m_setup.folder, m_res, m_res / "resources.json"}) {
        const QString shown = QDir::fromNativeSeparators(QString::fromStdWString(path.wstring()));
        if (!shown.isEmpty() && !watched.contains(shown, Qt::CaseInsensitive) && QFileInfo::exists(shown)) {
            missing.append(shown);
        }
    }
    if (!missing.isEmpty()) {
        m_watcher.addPaths(missing);
    }
}

void VisionWorkbench::setBusy(bool busy)
{
    const bool before = this->busy();
    m_busy = std::max(0, m_busy + (busy ? 1 : -1));
    if (before != this->busy()) {
        emit busyChanged();
    }
}

void VisionWorkbench::setProblem(const QString& problem)
{
    if (problem != m_problem) {
        m_problem = problem;
        emit problemChanged();
    }
}

void VisionWorkbench::setSaved(bool saved)
{
    if (saved != m_saved) {
        m_saved = saved;
        emit savedChanged();
    }
}

void VisionWorkbench::publishPictures()
{
    for (const auto& [file, bytes] : m_state.images) {
        const QString key = pictureKey(bytes);
        if (m_published.insert(key).second) {
            VisionImageProvider::publish(key, QImage::fromData(bytes));
        }
    }
}

bool VisionWorkbench::exists(const QString& kind, const QString& name) const
{
    const std::string key = utf8(name);
    const script::Resources& resources = m_state.resources;
    if (kind == u"template") {
        return resources.findTemplate(key) != nullptr;
    }
    if (kind == u"region") {
        return resources.findRegion(key) != nullptr;
    }
    if (kind == u"point") {
        return resources.findPoint(key) != nullptr;
    }
    if (kind == u"path") {
        return resources.findPath(key) != nullptr;
    }
    if (kind == u"color") {
        return resources.findColor(key) != nullptr;
    }
    return false;
}

QString VisionWorkbench::freeName(const QString& kind) const
{
    for (int number = 1;; ++number) {
        const QString name = kind == u"template" ? tr("Template %1").arg(number)
                           : kind == u"region"   ? tr("Region %1").arg(number)
                           : kind == u"point"    ? tr("Point %1").arg(number)
                           : kind == u"path"     ? tr("Path %1").arg(number)
                                                 : tr("Color %1").arg(number);
        if (!exists(kind, name)) {
            return name;
        }
    }
}

std::string VisionWorkbench::fileFor(const QString& name, const std::string& keep) const
{
    return templateFileFor(name, m_state.resources.templates, keep, [this](const std::string& file) {
        std::error_code error;
        return !m_onDisk.contains(file) && std::filesystem::exists(m_res / pathOf(file), error);
    });
}

QString VisionWorkbench::pictureKey(const QByteArray& bytes) const
{
    return u"t%1-%2"_s.arg(static_cast<qulonglong>(qHash(bytes)), 0, 16).arg(bytes.size());
}

QSize VisionWorkbench::pictureSizeOf(const QByteArray& bytes) const
{
    const QString key = pictureKey(bytes);
    if (const auto found = m_sizes.find(key); found != m_sizes.end()) {
        return found->second;
    }
    QBuffer buffer;
    buffer.setData(bytes);
    QSize size;
    if (buffer.open(QIODevice::ReadOnly)) {
        QImageReader reader(&buffer);
        size = reader.size();
    }
    m_sizes[key] = size;
    return size;
}

}
