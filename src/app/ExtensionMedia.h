#pragma once

#include "MediaOffers.h"

#include <gs/capture/CaptureSource.h>

#include <QString>
#include <QStringView>

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace gs::app {

[[nodiscard]] QString extensionSourceSerial(const MediaSourceDevice& source);
[[nodiscard]] bool isExtensionSourceSerial(QStringView serial) noexcept;
[[nodiscard]] std::optional<MediaSourceDevice> pairedSound(const std::vector<MediaSourceDevice>& sources,
                                                           const MediaSourceDevice& video);

struct SourcePlan {
    std::vector<MediaSourceDevice> added;
    std::vector<QString> removed;
};

[[nodiscard]] SourcePlan planExtensionSources(const std::vector<MediaSourceDevice>& sources,
                                              const std::vector<QString>& shown);

class ExtensionVideoSource final : public capture::CaptureSource
{
public:
    using Opener = std::function<std::unique_ptr<SourceLease>(exthost::MediaKind kind, const std::string& params)>;
    using Phone = std::function<std::string()>;

    ExtensionVideoSource(Opener open, Phone phone);
    ~ExtensionVideoSource() override;
    ExtensionVideoSource(const ExtensionVideoSource&) = delete;
    ExtensionVideoSource& operator=(const ExtensionVideoSource&) = delete;

    [[nodiscard]] bool start(capture::SampleSink& sink, const std::string& format) override;
    void stop() override;
    [[nodiscard]] std::string problem() const override;
    [[nodiscard]] bool givesSound() const override;

private:
    void showPictures(const std::stop_token& stop, capture::SampleSink& sink, exthost::MediaStream& stream);
    void playSound(const std::stop_token& stop, capture::SampleSink& sink, exthost::MediaStream& stream);

    Opener m_open;
    Phone m_phone;
    mutable std::mutex m_mutex;
    std::string m_problem;
    bool m_givesSound = false;
    std::unique_ptr<SourceLease> m_pictures;
    std::unique_ptr<SourceLease> m_sound;
    std::jthread m_picturePump;
    std::jthread m_soundPump;
};

}
