#pragma once

#include "MediaOffers.h"

#include <gs/media/AudioPlayer.h>

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

class QJSEngine;
class QQmlEngine;

namespace gs::media {
struct LumaSnapshot;
}

namespace gs::app {

class NativeExtensions;

void letterboxBgra(const media::LumaSnapshot& shot, std::uint32_t width, std::uint32_t height,
                   std::vector<std::uint8_t>& out);

class MediaSinks final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantList sinks READ sinks NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList running READ running NOTIFY changed FINAL)

public:
    using Camera = std::function<std::shared_ptr<const media::LumaSnapshot>()>;

    struct Host {
        std::function<QVariantList()> windows;
        std::function<Camera(const QString& serial)> camera;
        std::function<bool(const QString& serial, media::AudioTap tap)> listen;
    };

    MediaSinks(NativeExtensions& extensions, Host host, QObject* parent = nullptr);
    ~MediaSinks() override;
    MediaSinks(const MediaSinks&) = delete;
    MediaSinks& operator=(const MediaSinks&) = delete;

    static MediaSinks* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    [[nodiscard]] static MediaSinks* instance() noexcept { return s_instance; }

    [[nodiscard]] QVariantList sinks() const;
    [[nodiscard]] QVariantList running() const;
    Q_INVOKABLE QString toggle(const QString& serial, const QString& key);
    Q_INVOKABLE bool sending(const QString& serial, const QString& key) const;
    void windowsChanged();
    void stopAll();

    struct Shared;

signals:
    void changed();
    void stopped(const QString& serial, const QString& text);

private:
    struct Run;
    struct Listener;

    void offersChanged();
    void ended(std::uint64_t run, const QString& why);
    void stop(std::uint64_t run);
    void listen(const QString& serial, const std::shared_ptr<exthost::SinkPort>& port);
    void unlisten(const QString& serial, const std::shared_ptr<exthost::SinkPort>& port);

    static inline MediaSinks* s_instance = nullptr;

    NativeExtensions& m_extensions;
    Host m_host;
    std::shared_ptr<Shared> m_shared;
    std::vector<std::unique_ptr<Run>> m_runs;
    std::map<QString, std::shared_ptr<Listener>> m_listeners;
    std::uint64_t m_lastRun = 0;
};

}
