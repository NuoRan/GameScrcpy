#pragma once

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <deque>
#include <functional>

namespace gs::app {

class DebugRecord : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("A debug record belongs to the script workbench")
    Q_PROPERTY(bool active READ active NOTIFY changed FINAL)
    Q_PROPERTY(bool live READ live NOTIFY changed FINAL)
    Q_PROPERTY(bool recording READ recording NOTIFY changed FINAL)
    Q_PROPERTY(bool opened READ opened NOTIFY changed FINAL)
    Q_PROPERTY(QString name READ name NOTIFY changed FINAL)
    Q_PROPERTY(QVariantMap timeline READ timeline NOTIFY changed FINAL)
    Q_PROPERTY(QVariantMap moment READ moment NOTIFY changed FINAL)

public:
    static constexpr double kWindowMs = 300000.0;
    static constexpr double kRefreshMs = 1000.0;
    static constexpr int kFrameSide = 480;
    static constexpr int kStepsPerSecond = 200;

    explicit DebugRecord(QObject* parent = nullptr);
    ~DebugRecord() override;
    DebugRecord(const DebugRecord&) = delete;
    DebugRecord& operator=(const DebugRecord&) = delete;

    [[nodiscard]] static QByteArray encodeFrame(const QImage& image);

    void setClock(std::function<double()> clock);
    void start(const QString& liveFolder);
    void stop();
    void addHits(const QVariantList& hits);
    void addPause(const QVariantMap& pause);
    void addResume(quint64 pause);
    void addFrame(const QByteArray& jpeg);
    void refresh();
    [[nodiscard]] bool active() const noexcept { return m_recording || m_opened; }
    [[nodiscard]] bool live() const noexcept { return m_live; }
    [[nodiscard]] bool recording() const noexcept { return m_recording; }
    [[nodiscard]] bool opened() const noexcept { return m_opened; }
    [[nodiscard]] QString name() const { return m_name; }
    [[nodiscard]] QVariantMap timeline() const;
    [[nodiscard]] QVariantMap moment() const;
    Q_INVOKABLE void seek(double at);
    Q_INVOKABLE void goLive();
    [[nodiscard]] QString save(const QString& root, const QString& codeFolder, const QString& name) const;
    bool open(const QString& folder);
    Q_INVOKABLE void close();
    [[nodiscard]] QString codeFolder() const { return m_opened ? m_code : QString(); }
    [[nodiscard]] QString recordedFile(const QString& path) const;

signals:
    void changed();
    void replayed(const QString& file, const QString& position, const QVariantMap& values);
    void wentLive();

private:
    struct Step {
        double at = 0.0;
        qint64 task = 0;
        int line = 0;
        QString label;
        QString file;
        QString position;
        QVariantMap values;
    };

    struct Event {
        double at = 0.0;
        QVariantMap data;
    };

    struct Frame {
        double at = 0.0;
        QString file;
    };

    [[nodiscard]] double now() const;
    [[nodiscard]] double first() const;
    [[nodiscard]] double last() const;
    [[nodiscard]] const Step* stepAt(double at) const;
    [[nodiscard]] const Event* pauseAt(double at) const;
    [[nodiscard]] QString laneTitle(const QString& label) const;
    [[nodiscard]] QString stepText(const Step& step) const;
    [[nodiscard]] QString eventText(const QVariantMap& data) const;
    [[nodiscard]] QString visionText(const QString& json) const;
    void keep(Step step);
    void notify();
    void trim();
    void clear();

    std::function<double()> m_clock;
    QString m_folder;
    QString m_name;
    QString m_code;
    std::deque<Step> m_steps;
    std::deque<Event> m_events;
    std::deque<Frame> m_frames;
    int m_framesMade = 0;
    qint64 m_second = -1;
    int m_secondKept = 0;
    double m_playhead = 0.0;
    double m_notified = 0.0;
    bool m_recording = false;
    bool m_opened = false;
    bool m_live = true;
};

}
