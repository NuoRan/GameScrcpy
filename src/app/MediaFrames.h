#pragma once

#include "LogicServices.h"
#include "NativeExtensions.h"

#include <gs/exthost/Module.h>
#include <gs/media/FrameSnapshot.h>
#include <gs/script/Services.h>

#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class QJSEngine;
class QQmlEngine;

namespace gs::app {

class AppSettings;
class ExtensionHost;

struct FramePhone {
    QString serial;
    QString deviceKey;
    std::shared_ptr<media::FrameSnapshots> snapshots;
    int rotation = 0;
};

struct MediaSources {
    std::function<std::vector<FramePhone>()> phones;
    std::function<std::vector<FrameTapSource>()> taps;
    std::function<void(const QString& serial, const QString& group, const QVariantList& shapes)> overlay;
};

struct MediaReplies {
    std::function<void(std::uint64_t logic, quint64 request, const QString& json)> answer;
    std::function<void(std::uint64_t logic, quint64 request, const QString& message)> refuse;
    std::function<void(std::uint64_t logic, const QString& kind, const QString& id, const QString& json)> tell;
    std::function<std::uint64_t(std::uint64_t logic, const QImage& picture, script::PictureSize source, quint64 frames)>
        keep;
    std::function<void(std::uint64_t logic, quint64 frames)> forget;
};

[[nodiscard]] std::vector<std::uint8_t> framePixels(const media::LumaSnapshot& shot, int left, int top, int width,
                                                    int height, int outWidth, int outHeight);
[[nodiscard]] QVariantMap overlayShape(const exthost::Shape& shape, const QString& extension);

class MediaFrames final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantList taps READ taps NOTIFY tapsChanged FINAL)

public:
    static constexpr std::string_view kPermission = "media.read";
    static constexpr std::string_view kEventKind = "media";
    static constexpr std::size_t kMostSubscriptions = 4;
    static constexpr double kLeastFps = 0.1;
    static constexpr double kMostFps = 10.0;
    static constexpr double kDefaultFps = 2.0;
    static constexpr int kLeastSize = 64;
    static constexpr int kMostSize = 1920;
    static constexpr int kDefaultSize = 640;

    explicit MediaFrames(QObject* parent);
    ~MediaFrames() override;
    MediaFrames(const MediaFrames&) = delete;
    MediaFrames& operator=(const MediaFrames&) = delete;

    static MediaFrames* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    [[nodiscard]] static MediaFrames* instance() noexcept { return s_instance; }

    void offerServices();
    void setSettings(AppSettings* settings);
    void setReplies(MediaReplies replies);
    void setSources(MediaSources sources);
    void setPermitted(const QStringList& extensions);
    void follow(ExtensionHost& extensions);
    void sync();

    void request(const LogicCaller& caller, std::string_view service, quint64 id, const QString& json);
    [[nodiscard]] script::ServiceAnswer call(const LogicCaller& caller, std::string_view service,
                                             std::string_view json);
    void endLogic(std::uint64_t logic);

    [[nodiscard]] QVariantList taps() const { return m_tapList; }
    Q_INVOKABLE bool setTapOn(const QString& serial, const QString& key, bool on);

signals:
    void tapsChanged();

private:
    struct Shared;
    struct Feed;

    struct Subscription {
        std::uint64_t logic = 0;
        QString owner;
        QString serial;
    };

    struct Running {
        quint64 instance = 0;
        const exthost::Module* identity = nullptr;
        int fps = 0;
        int size = 0;
        QString extensionName;
    };

    [[nodiscard]] static script::ServiceAnswer callWith(const std::shared_ptr<Shared>& shared, const LogicCaller& caller,
                                                        std::string_view service, std::string_view json);

    [[nodiscard]] Feed* feedOf(const QString& serial);
    [[nodiscard]] Feed* feedIf(const QString& serial) const;
    void prune(const QString& serial);
    void syncTaps();
    void refreshList();
    [[nodiscard]] std::vector<std::string> chosenOn(const QString& deviceKey) const;
    [[nodiscard]] bool choose(const QString& deviceKey, std::vector<std::string> chosen);
    void deliver(quint64 handle, const QImage& picture, script::Region region, script::PictureSize source, double time);
    void ready(std::uint64_t logic, quint64 handle);
    void close(std::uint64_t logic, quint64 handle, const QString& reason);
    void showOverlay(const QString& serial, const std::string& key, quint64 instance,
                     const std::vector<exthost::Shape>& shapes);
    void tapFailed(const QString& serial, const std::string& key, quint64 instance, const QString& problem);

    static inline MediaFrames* s_instance = nullptr;

    std::shared_ptr<Shared> m_shared;
    AppSettings* m_settings = nullptr;
    MediaReplies m_replies;
    MediaSources m_sources;
    std::map<QString, FramePhone> m_phones;
    std::vector<FrameTapSource> m_available;
    std::map<QString, std::unique_ptr<Feed>> m_feeds;
    std::map<quint64, Subscription> m_subscriptions;
    std::map<std::pair<QString, std::string>, Running> m_running;
    std::map<QString, std::vector<std::string>> m_chosen;
    QVariantList m_tapList;
    quint64 m_lastHandle = 0;
    quint64 m_lastInstance = 0;
    bool m_offered = false;
};

}
