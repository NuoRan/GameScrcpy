#pragma once

#include "ControlChannel.h"
#include "InputHost.h"
#include "ScriptRuntime.h"

#include <gs/base/Expected.h>
#include <gs/script/Services.h>

#include <QDateTime>
#include <QImage>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

class QJSEngine;
class QQmlEngine;

namespace gs::media {
struct LumaSnapshot;
}

namespace gs::record {
class Recorder;
}

namespace gs::app {

class AppSettings;

class RecordingTap final : public OperationObserver
{
public:
    void setRecorder(std::shared_ptr<record::Recorder> recorder);
    [[nodiscard]] std::shared_ptr<record::Recorder> recorder() const;

    void touched(const protocol::TouchFrame& frame) override;
    void keyed(const protocol::Key& key) override;
    void scrolled(const protocol::Scroll& scroll) override;
    void typed(std::size_t characters) override;
    void pressed(std::string_view id, bool down);
    void stateChanged(std::string_view id, bool value);
    void answered(const QString& json);

private:
    mutable std::mutex m_mutex;
    std::shared_ptr<record::Recorder> m_recorder;
};

class RecordingLane final : public LaneAccess
{
public:
    RecordingLane(std::shared_ptr<LaneAccess> lane, std::shared_ptr<RecordingTap> tap);

    void attach(std::shared_ptr<const input::Profile> profile, std::shared_ptr<LaneObserver> observer) override;
    void detach(std::shared_ptr<LaneObserver> observer) override;
    void post(std::function<void(InputHost& host, Microseconds now)> task) override;
    void claimKeys(std::vector<input::InputSource> keys) override;
    void handOver(std::vector<std::string> ids) override;
    [[nodiscard]] const ControlChannel* channel() const noexcept override { return m_lane->channel(); }
    [[nodiscard]] std::optional<input::Vec2> cursor() const override { return m_lane->cursor(); }
    [[nodiscard]] bool held(std::string_view name) const override { return m_lane->held(name); }

private:
    std::shared_ptr<LaneAccess> m_lane;
    std::shared_ptr<RecordingTap> m_tap;
    std::shared_ptr<LaneObserver> m_observer;
    std::shared_ptr<LaneObserver> m_tee;
};

class RecordingThumbnails
{
public:
    static constexpr int kShown = 6;
    static constexpr int kFirstEvery = 3;

    void offer(const QImage& image);
    [[nodiscard]] QVariantList shown() const;
    [[nodiscard]] qsizetype kept() const noexcept { return m_kept.size(); }

private:
    QList<QImage> m_kept;
    int m_every = kFirstEvery;
    int m_tick = 0;
};

struct RecordingStart {
    QString serial;
    QString phone;
    QString profileId;
    QString profileName;
    bool sound = false;
    script::DeviceInfo device;
    double frameRate = 60.0;
    std::function<QImage()> thumbnail;
};

[[nodiscard]] QImage snapshotThumbnail(const media::LumaSnapshot& shot, int longSide);
[[nodiscard]] QString defaultRecordingsLocation();
[[nodiscard]] QString recordingsLocation(const AppSettings& settings);
[[nodiscard]] QString recordingFolderName(const QString& phone, const QDateTime& when);
[[nodiscard]] QString sizeText(quint64 bytes);
[[nodiscard]] QString clockText(qint64 milliseconds);
[[nodiscard]] QVariantList recordingEntries(const QString& location, const QString& serial, const QString& phone,
                                            int most);

class Recordings final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString location READ location NOTIFY locationChanged FINAL)
    Q_PROPERTY(QUrl locationUrl READ locationUrl NOTIFY locationChanged FINAL)

public:
    explicit Recordings(AppSettings& settings, QObject* parent = nullptr);
    ~Recordings() override;
    Recordings(const Recordings&) = delete;
    Recordings& operator=(const Recordings&) = delete;

    static Recordings* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    [[nodiscard]] static Recordings* instance() noexcept { return s_instance; }

    [[nodiscard]] QString location() const;
    [[nodiscard]] QUrl locationUrl() const;

    [[nodiscard]] Expected<std::shared_ptr<record::Recorder>, QString> start(RecordingStart start);
    [[nodiscard]] bool running(const QString& serial) const;
    [[nodiscard]] QVariantMap state(const QString& serial) const;
    QVariantMap stop(const QString& serial);
    void stopAll();

    Q_INVOKABLE void exportVideo(const QString& folder, const QString& serial);
    Q_INVOKABLE void showInFolder(const QString& path) const;
    Q_INVOKABLE QString folderOf(const QUrl& url) const;
    Q_INVOKABLE QVariantMap turnIntoTask(const QString& folder, const QString& profileId);
    Q_INVOKABLE int saveAsTest(const QString& folder, const QString& profileId);
    int listRecordings(const QString& serial, const QString& phone);
    Q_INVOKABLE QVariantMap describe(const QString& folder, const QString& serial) const;
    Q_INVOKABLE QString moveToTrash(const QString& folder);

signals:
    void locationChanged();
    void failed(const QString& serial, const QString& problem);
    void exported(const QString& serial, bool ok, const QString& text, const QString& shown);
    void testSaved(int ticket, bool ok, const QString& text);
    void recordingsListed(int ticket, const QVariantList& recordings);

private:
    struct Run;

    void tick();

    static inline Recordings* s_instance = nullptr;

    AppSettings& m_settings;
    std::map<QString, std::unique_ptr<Run>> m_runs;
    QSet<QString> m_exporting;
    QTimer m_clock;
    int m_testTickets = 0;
    int m_listTickets = 0;
};

}
