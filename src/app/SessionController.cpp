#include "SessionController.h"

#include "UhidMouse.h"

#include <gs/base/BuildInfo.h>
#include <gs/media/AudioPlayer.h>
#include <gs/media/LumaSampler.h>
#include <gs/media/Thumbnailer.h>
#include <gs/media/VideoDecoder.h>
#include <gs/record/MediaFile.h>
#include <gs/record/Recorder.h>
#include <gs/render/GraphicsDevice.h>

#include <QClipboard>
#include <QGuiApplication>
#include <QMetaObject>
#include <QtLogging>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <utility>
#include <variant>
#include <vector>

namespace gs::app {

namespace {

std::int64_t steadyMicros() noexcept
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool sameRecorder(const std::weak_ptr<record::Recorder>& primed, const std::shared_ptr<record::Recorder>& recorder)
{
    return !primed.owner_before(recorder) && !recorder.owner_before(primed);
}

TouchBackend backendOf(settings::TouchMethod method) noexcept
{
    switch (method) {
    case settings::TouchMethod::Uhid:
        return TouchBackend::Uhid;
    case settings::TouchMethod::Aoa:
        return TouchBackend::Aoa;
    case settings::TouchMethod::None:
        return TouchBackend::None;
    case settings::TouchMethod::Inject:
        break;
    }
    return TouchBackend::Injection;
}

}

class SessionController::Bridge final : public session::SessionListener, private media::FrameSink
{
public:
    explicit Bridge(SessionController& controller) : m_controller(controller) {}

    void setAudioMuted(bool muted) noexcept { m_audio.setMuted(muted); }
    void setAudioVolume(float volume) noexcept { m_audio.setVolume(volume); }

    void onVideoOpened() override
    {
        const render::GraphicsDevice* graphics = m_controller.m_graphics;
        m_decoder = std::make_unique<media::VideoDecoder>(graphics != nullptr ? graphics->device() : nullptr);
        m_thumbnailer = std::make_unique<media::Thumbnailer>(graphics != nullptr ? graphics->device() : nullptr);
        m_awaitingKeyFrame = false;
    }

    void onVideoClosed() override
    {
        m_controller.m_snapshots->forget();
        m_previous = media::VideoFrame();
        m_sampler.reset();
        m_thumbnailer.reset();
        m_decoder.reset();
        m_videoHead.clear();
    }

    void onVideoPacket(const protocol::MediaPacket& packet) override
    {
        keep(packet, protocol::MediaChannel::Video, m_videoHead, m_videoPrimed);
        if (const auto* info = std::get_if<protocol::VideoSessionInfo>(&packet.session)) {
            if (UhidTouchSink* uhid = m_controller.m_uhidSink.get()) {
                uhid->setVideo(packet.header.session, info->rotation);
            }
            if (AoaTouchSink* aoa = m_controller.m_aoaSink.get()) {
                aoa->setVideo(packet.header.session, info->rotation);
            }
            if (ExtensionTouch* extension = m_controller.m_extensionTouch.get()) {
                extension->setVideo(packet.header.session, info->rotation);
            }
            m_controller.m_channel->setVideo(packet.header.session, info->frameWidth, info->frameHeight);
            const int displayLong = std::max(info->displayWidth, info->displayHeight);
            m_frameScale = displayLong > 0
                               ? static_cast<double>(std::max(info->frameWidth, info->frameHeight)) / displayLong
                               : 1.0;
        }
        if (m_awaitingKeyFrame && packet.header.kind == protocol::MediaKind::Frame && !packet.header.keyFrame) {
            return;
        }
        const std::int64_t receivedUs = steadyMicros();
        const auto pushed = m_decoder->push(packet, *this);
        if (packet.header.kind == protocol::MediaKind::Frame) {
            m_controller.m_metrics.videoFrame(packet.header.size, packet.header.ptsUs, packet.header.encodeDelayUs,
                                              receivedUs, steadyMicros() - receivedUs);
        }
        if (pushed) {
            m_awaitingKeyFrame = m_awaitingKeyFrame && !packet.header.keyFrame;
        } else if (!m_awaitingKeyFrame) {
            m_awaitingKeyFrame = true;
            qWarning("video of %s: %s", qUtf8Printable(m_controller.m_serial), pushed.error().message.c_str());
            m_controller.m_session->send(protocol::RequestKeyFrame{});
        }
    }

    void onHeartbeat(const session::Heartbeat& heartbeat) override
    {
        m_controller.m_metrics.heartbeat(heartbeat);
    }

    [[nodiscard]] media::AudioStats audioStats() const { return m_audio.stats(); }

    void onAudioPacket(const protocol::MediaPacket& packet) override
    {
        keep(packet, protocol::MediaChannel::Audio, m_audioHead, m_audioPrimed);
        if (const auto pushed = m_audio.push(packet); !pushed && m_audioWorking) {
            m_audioWorking = false;
            qWarning("audio of %s: %s", qUtf8Printable(m_controller.m_serial), pushed.error().c_str());
        }
    }

    void onAudioClosed() override
    {
        m_audio.close();
        m_audioWorking = true;
        m_audioHead.clear();
    }

    void onBulkOpened() override { bulkChanged(true); }

    void onBulkClosed() override { bulkChanged(false); }

    void onStateChanged(const session::SessionState& state) override
    {
        if (UhidTouchSink* uhid = m_controller.m_uhidSink.get()) {
            uhid->phaseChanged(state.phase);
        }
        if (AoaTouchSink* aoa = m_controller.m_aoaSink.get()) {
            aoa->phaseChanged(state.phase);
        }
        QMetaObject::invokeMethod(&m_controller, [controller = &m_controller, state] {
            if (state.phase == session::SessionPhase::Streaming &&
                controller->m_phase != session::SessionPhase::Streaming) {
                ++controller->m_padEpoch;
            }
            controller->m_phase = state.phase;
            controller->updatePadLink();
            if (state.phase == session::SessionPhase::Streaming) {
                controller->applyScreen();
                controller->applyOrientation();
                controller->applyStatus();
                controller->applyLaunch();
                controller->applyMouse();
            } else {
                controller->m_mouseReady = false;
                controller->stopLatencyTest();
                controller->endClipboardReads();
            }
            emit controller->stateChanged(state);
            if (state.phase == session::SessionPhase::Streaming && std::exchange(controller->m_touchMissing, false)) {
                emit controller->touchUnavailable();
            }
        }, Qt::QueuedConnection);
    }

    void onTransport(session::TransportKind kind) override
    {
        QMetaObject::invokeMethod(&m_controller, [controller = &m_controller, kind] {
            emit controller->transportUsed(kind);
        }, Qt::QueuedConnection);
    }

    void onWelcome(const protocol::Welcome& welcome) override
    {
        if (UhidTouchSink* uhid = m_controller.m_uhidSink.get()) {
            uhid->welcomed(welcome.capabilities);
            QMetaObject::invokeMethod(&m_controller, [controller = &m_controller, phone = welcome.capabilities] {
                controller->noteTouch(phone);
            }, Qt::QueuedConnection);
        }
        if (!welcome.displays.empty()) {
            m_controller.m_channel->setRefreshHz(welcome.displays.front().refreshMilliHz / 1000.0);
            m_densityDpi.store(welcome.displays.front().densityDpi, std::memory_order_relaxed);
            describe(welcome.displays.front());
        }
        QStringList codecs;
        for (const protocol::VideoCodec codec : {protocol::VideoCodec::H264, protocol::VideoCodec::H265,
                                                 protocol::VideoCodec::Av1}) {
            if (std::ranges::any_of(welcome.encoders, [codec](const protocol::Encoder& encoder) {
                    return encoder.codec == codec;
                })) {
                codecs.append(codecName(codec));
            }
        }
        QMetaObject::invokeMethod(&m_controller, [controller = &m_controller,
                                                  manufacturer = QString::fromStdString(welcome.manufacturer),
                                                  model = QString::fromStdString(welcome.model),
                                                  audio = welcome.audioStatus, capabilities = welcome.capabilities,
                                                  resumed = welcome.resumed, codecs] {
            controller->m_capabilities = capabilities;
            controller->updatePadLink();
            controller->m_clipboardShared =
                (capabilities & protocol::Welcome::kClipboard) != 0 && !controller->showsAppDisplay();
            controller->m_clipboardAsked = false;
            controller->m_launchPending = controller->m_launchPending || (controller->showsAppDisplay() && !resumed);
            emit controller->welcomed(manufacturer, model);
            emit controller->codecsDescribed(codecs);
            if (audio != protocol::AudioStatus::Available) {
                emit controller->audioUnavailable(audio);
            }
        }, Qt::QueuedConnection);
    }

    void onMessage(const protocol::ToClientMessage& message) override
    {
        if (const auto* display = std::get_if<protocol::DisplayChanged>(&message)) {
            if (display->display.displayId != 0 || m_controller.showsAppDisplay()) {
                return;
            }
            m_controller.m_channel->setRefreshHz(display->display.refreshMilliHz / 1000.0);
            m_densityDpi.store(display->display.densityDpi, std::memory_order_relaxed);
            describe(display->display);
            return;
        }
        if (const auto* clip = std::get_if<protocol::ClipboardChanged>(&message)) {
            QMetaObject::invokeMethod(&m_controller, [controller = &m_controller,
                                                      text = QString::fromStdString(clip->text)] {
                controller->deviceClipboardChanged(text);
            }, Qt::QueuedConnection);
            return;
        }
        if (const auto* app = std::get_if<protocol::ForegroundAppChanged>(&message)) {
            QMetaObject::invokeMethod(&m_controller, [controller = &m_controller,
                                                      package = QString::fromStdString(app->package),
                                                      activity = QString::fromStdString(app->activity),
                                                      game = app->game] {
                emit controller->foregroundAppChanged(package, activity, game);
            }, Qt::QueuedConnection);
            return;
        }
        if (const auto* status = std::get_if<protocol::DeviceStatus>(&message)) {
            QMetaObject::invokeMethod(&m_controller, [controller = &m_controller, reported = *status] {
                if (controller->m_statusReports) {
                    emit controller->deviceStatusArrived(reported);
                }
            }, Qt::QueuedConnection);
            return;
        }
        if (const auto* output = std::get_if<protocol::UhidOutput>(&message)) {
            QMetaObject::invokeMethod(
                &m_controller,
                [controller = &m_controller, id = output->id,
                 data = QByteArray(reinterpret_cast<const char*>(output->data.data()),
                                   static_cast<qsizetype>(output->data.size()))] { emit controller->uhidOutput(id, data); },
                Qt::QueuedConnection);
            return;
        }
        if (const auto* status = std::get_if<protocol::UhidStatus>(&message)) {
            QMetaObject::invokeMethod(&m_controller, [controller = &m_controller, id = status->id,
                                                      recognized = status->recognized, vibration = status->vibration] {
                emit controller->uhidStatus(id, recognized, vibration);
            }, Qt::QueuedConnection);
            return;
        }
        if (bulkReply(message)) {
            return;
        }
        const auto* notice = std::get_if<protocol::Notice>(&message);
        if (notice == nullptr) {
            return;
        }
        QMetaObject::invokeMethod(&m_controller, [controller = &m_controller, code = notice->code,
                                                  text = QString::fromStdString(notice->message)] {
            emit controller->refused(code, text);
        }, Qt::QueuedConnection);
    }

private:
    void keep(const protocol::MediaPacket& packet, protocol::MediaChannel channel, record::MediaHead& head,
              std::weak_ptr<record::Recorder>& primed)
    {
        const std::shared_ptr<record::Recorder> recorder = m_controller.recorder();
        const auto deliver = [&recorder, channel](const protocol::MediaPacket& shown) {
            if (channel == protocol::MediaChannel::Video) {
                recorder->video(shown);
            } else {
                recorder->audio(shown);
            }
        };
        if (recorder != nullptr && !sameRecorder(primed, recorder)) {
            primed = recorder;
            if (packet.header.kind != protocol::MediaKind::Session) {
                for (const protocol::MediaPacket& earlier : head.packets(channel)) {
                    deliver(earlier);
                }
            }
        }
        head.see(packet);
        if (recorder != nullptr) {
            deliver(packet);
        }
    }

    bool bulkReply(const protocol::ToClientMessage& message)
    {
        if (const auto* list = std::get_if<protocol::AppList>(&message)) {
            QMetaObject::invokeMethod(&m_controller, [controller = &m_controller, requestId = list->requestId,
                                                      apps = list->apps] {
                emit controller->appsListed(requestId, apps);
            }, Qt::QueuedConnection);
            return true;
        }
        if (const auto* icon = std::get_if<protocol::AppIcon>(&message)) {
            QMetaObject::invokeMethod(
                &m_controller,
                [controller = &m_controller, requestId = icon->requestId, package = QString::fromStdString(icon->package),
                 png = QByteArray(reinterpret_cast<const char*>(icon->png.data()),
                                  static_cast<qsizetype>(icon->png.size()))] {
                    emit controller->appIconArrived(requestId, package, png);
                },
                Qt::QueuedConnection);
            return true;
        }
        if (const auto* screenshot = std::get_if<protocol::Screenshot>(&message)) {
            decodeScreenshot(*screenshot);
            return true;
        }
        if (const auto* failed = std::get_if<protocol::RequestFailed>(&message)) {
            QMetaObject::invokeMethod(&m_controller, [controller = &m_controller, requestId = failed->requestId,
                                                      code = failed->code,
                                                      text = QString::fromStdString(failed->message)] {
                emit controller->bulkRequestFailed(requestId, code, text);
            }, Qt::QueuedConnection);
            return true;
        }
        return false;
    }

    void decodeScreenshot(const protocol::Screenshot& screenshot)
    {
        QImage image = QImage::fromData(
            QByteArrayView(screenshot.data.data(), static_cast<qsizetype>(screenshot.data.size())), "PNG");
        if (image.isNull()) {
            qWarning("screenshot %u of %s: %lld bytes that do not decode as PNG", screenshot.requestId,
                     qUtf8Printable(m_controller.m_serial), static_cast<long long>(screenshot.data.size()));
            QMetaObject::invokeMethod(&m_controller, [controller = &m_controller, requestId = screenshot.requestId] {
                emit controller->bulkRequestFailed(requestId, protocol::ErrorCode::InternalError,
                                                   QStringLiteral("the screenshot does not decode as PNG"));
            }, Qt::QueuedConnection);
            return;
        }
        QMetaObject::invokeMethod(&m_controller, [controller = &m_controller, requestId = screenshot.requestId,
                                                  image = std::move(image)] {
            emit controller->screenshotArrived(requestId, image);
        }, Qt::QueuedConnection);
    }

    void bulkChanged(bool ready)
    {
        QMetaObject::invokeMethod(&m_controller, [controller = &m_controller, ready] {
            controller->m_bulkReady = ready;
            emit controller->bulkChanged(ready);
        }, Qt::QueuedConnection);
    }

    void onFrame(media::VideoFrame frame) override
    {
        m_controller.m_snapshots->offer(frame);
        m_controller.m_snapshots->poll();
        if (auto thumbnail = m_thumbnailer->collect()) {
            QImage image(thumbnail->width, thumbnail->height, QImage::Format_RGB32);
            for (int y = 0; y < thumbnail->height; ++y) {
                std::memcpy(image.scanLine(y), thumbnail->pixels.data() + std::ptrdiff_t(y) * thumbnail->width,
                            std::size_t(thumbnail->width) * sizeof(std::uint32_t));
            }
            QMetaObject::invokeMethod(&m_controller, [controller = &m_controller, image = std::move(image)] {
                emit controller->thumbnailed(image);
            }, Qt::QueuedConnection);
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= m_nextThumbnail) {
            m_nextThumbnail = now + kThumbnailInterval;
            m_thumbnailer->request(frame, kThumbnailLongSide);
        }
        if (const std::shared_ptr<LatencyMeter> meter = m_controller.meter()) {
            measure(*meter, frame);
        } else if (m_previous) {
            m_previous = media::VideoFrame();
            m_sampler.reset();
        }
        if (m_controller.m_benchFed.load(std::memory_order_relaxed)) {
            m_controller.m_benchFrames->put(frame.share());
        }
        m_controller.m_frames->put(std::move(frame));
    }

    void measure(LatencyMeter& meter, const media::VideoFrame& frame)
    {
        const std::int64_t nowUs = steadyMicros();
        const int density = m_densityDpi.load(std::memory_order_relaxed);
        const double pixelsPerDp = (density > 0 ? density : kAssumedDensityDpi) / 160.0 * m_frameScale;
        const LatencyMeter::Plan plan = meter.plan(frame.width(), frame.height(), pixelsPerDp, nowUs);
        if (!plan.empty()) {
            if (!m_sampler) {
                const render::GraphicsDevice* graphics = m_controller.m_graphics;
                m_sampler = std::make_unique<media::LumaSampler>(graphics != nullptr ? graphics->device() : nullptr);
            }
            std::vector<media::LumaReading> fresh;
            if (!plan.freshSpots.empty() && m_previous) {
                if (auto read = m_sampler->read(m_previous, plan.freshSpots)) {
                    fresh = std::move(*read);
                }
            }
            std::vector<media::LumaReading> watched;
            if (!plan.watchedSpots.empty()) {
                if (auto read = m_sampler->read(frame, plan.watchedSpots)) {
                    watched = std::move(*read);
                }
            }
            meter.observe(plan, fresh, watched, frame.ptsUs(), nowUs);
        }
        m_previous = frame.share();
    }

    void describe(const protocol::Display& display)
    {
        QMetaObject::invokeMethod(&m_controller, [controller = &m_controller,
                                                  size = QSize(display.width, display.height),
                                                  refreshHz = display.refreshMilliHz / 1000.0,
                                                  rotation = static_cast<int>(display.rotation)] {
            emit controller->screenDescribed(size, refreshHz, rotation);
        }, Qt::QueuedConnection);
    }

    static QString codecName(protocol::VideoCodec codec)
    {
        switch (codec) {
        case protocol::VideoCodec::H264:
            return QStringLiteral("H.264");
        case protocol::VideoCodec::H265:
            return QStringLiteral("H.265");
        case protocol::VideoCodec::Av1:
            return QStringLiteral("AV1");
        }
        return {};
    }

    static constexpr int kThumbnailLongSide = 160;
    static constexpr std::chrono::milliseconds kThumbnailInterval{500};
    static constexpr int kAssumedDensityDpi = 420;

    SessionController& m_controller;
    std::unique_ptr<media::VideoDecoder> m_decoder;
    std::unique_ptr<media::Thumbnailer> m_thumbnailer;
    std::unique_ptr<media::LumaSampler> m_sampler;
    media::VideoFrame m_previous;
    double m_frameScale = 1.0;
    std::atomic<int> m_densityDpi{0};
    std::chrono::steady_clock::time_point m_nextThumbnail;
    bool m_awaitingKeyFrame = false;
    media::AudioPlayer m_audio;
    bool m_audioWorking = true;
    record::MediaHead m_videoHead;
    record::MediaHead m_audioHead;
    std::weak_ptr<record::Recorder> m_videoPrimed;
    std::weak_ptr<record::Recorder> m_audioPrimed;
};

SessionController::SessionController(adb::AdbClient adb, QString serial,
                                     std::shared_ptr<const session::ServerPackage> package,
                                     const render::GraphicsDevice* graphics, QObject* parent)
    : QObject(parent), m_adb(adb), m_serial(std::move(serial)), m_package(std::move(package)), m_graphics(graphics),
      m_snapshots(std::make_shared<media::FrameSnapshots>(graphics != nullptr ? graphics->device() : nullptr)),
      m_bridge(std::make_unique<Bridge>(*this))
{
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &SessionController::computerClipboardChanged);
    m_latencyPoll.setInterval(kLatencyPollInterval);
    connect(&m_latencyPoll, &QTimer::timeout, this, &SessionController::pollLatency);
    connect(this, &SessionController::refused, this, [this](protocol::ErrorCode code, const QString& text) {
        if (code == protocol::ErrorCode::PermissionDenied && text.startsWith(u"the phone refused to show touches")) {
            refuseLatencyTest();
        }
    });
    connect(this, &SessionController::refused, this, [this](protocol::ErrorCode code, const QString& text) {
        if (text.startsWith(u"system panel: ")) {
            qInfo("the notification shade of %s: %s", qUtf8Printable(m_serial), qUtf8Printable(text));
            emit systemPanelFailed(code == protocol::ErrorCode::UnsupportedAndroid);
        }
    });
}

void SessionController::computerClipboardChanged()
{
    if (!clipboardOpen()) {
        return;
    }
    const QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty() || text == m_sharedClipboard) {
        return;
    }
    shareClipboard(text);
}

void SessionController::deviceClipboardChanged(const QString& text)
{
    if (showsAppDisplay()) {
        return;
    }
    m_sharedClipboard = text;
    if (std::exchange(m_clipboardAsked, false)) {
        if (std::exchange(m_clipboardReads, 0) > 0) {
            emit phoneClipboardRead(true, text);
        }
        return;
    }
    if (QGuiApplication::clipboard()->text() != text) {
        QGuiApplication::clipboard()->setText(text);
    }
}

bool SessionController::clipboardOpen() const noexcept
{
    return m_session && m_clipboardShared && m_phase == session::SessionPhase::Streaming;
}

bool SessionController::shareClipboard(const QString& text)
{
    QByteArray utf8 = text.toUtf8();
    if (utf8.size() > kClipboardLimit) {
        qsizetype end = kClipboardLimit;
        while (end > 0 && (static_cast<unsigned char>(utf8[end]) & 0xC0) == 0x80) {
            --end;
        }
        qWarning("the clipboard of %lld bytes is cut to %lld for %s", static_cast<long long>(utf8.size()),
                 static_cast<long long>(end), qUtf8Printable(m_serial));
        utf8.truncate(end);
    }
    m_sharedClipboard = text;
    protocol::SetClipboard set;
    set.sequence = ++m_clipboardSequence;
    set.text = utf8.toStdString();
    return m_session->send(set);
}

bool SessionController::readPhoneClipboard()
{
    if (!clipboardOpen()) {
        return false;
    }
    if (!m_clipboardAsked) {
        protocol::GetClipboard get;
        get.copyKey = protocol::CopyKey::None;
        if (!m_session->send(get)) {
            return false;
        }
        m_clipboardAsked = true;
    }
    ++m_clipboardReads;
    return true;
}

bool SessionController::writePhoneClipboard(const QString& text)
{
    return clipboardOpen() && shareClipboard(text);
}

void SessionController::endClipboardReads()
{
    if (std::exchange(m_clipboardReads, 0) > 0) {
        emit phoneClipboardRead(false, {});
    }
}

SessionController::~SessionController()
{
    m_channel->setTouchObserver({});
    m_frames->setShownListener({});
    releaseSession();
}

void SessionController::releaseSession()
{
    m_mouseReady = false;
    endClipboardReads();
    m_clipboardAsked = false;
    m_channel->detach();
    if (m_uhidSink && m_uhidSink->ignoredContacts() > 0) {
        qInfo("%d touches on %s were ignored: the UHID touch screen takes %d fingers at most",
              m_uhidSink->ignoredContacts(), qUtf8Printable(m_serial), kUhidMaxContacts);
    }
    m_session.reset();
    m_uhidSink.reset();
    m_uhidWire.reset();
    m_aoaSink.reset();
    m_aoaPort.reset();
    m_extensionTouch.reset();
    m_noTouch.reset();
    m_controlSink.reset();
}

ControlSink* SessionController::extensionSink(const std::string& key)
{
    m_touchNoted = true;
    const QString title = m_touchMethods != nullptr ? m_touchMethods->touchTitle(key) : QString::fromStdString(key);
    m_extensionTouch = m_touchMethods != nullptr
                           ? m_touchMethods->openTouch(key, m_serial.toStdString(), *m_controlSink,
                                                       [this] {
                                                           QMetaObject::invokeMethod(
                                                               this, [this] { extensionTouchFailed(); },
                                                               Qt::QueuedConnection);
                                                       })
                           : nullptr;
    m_extensionTouchTitle = title;
    if (m_extensionTouch == nullptr) {
        qInfo("touch on %s goes through injection: the touch method %s of an extension is not available",
              qUtf8Printable(m_serial), qUtf8Printable(title));
        m_extensionTouchProblem = title;
        m_touchMissing = true;
        return nullptr;
    }
    qInfo("touch on %s goes through the touch method %s of an extension", qUtf8Printable(m_serial),
          qUtf8Printable(title));
    return m_extensionTouch.get();
}

void SessionController::extensionTouchFailed()
{
    if (m_extensionTouch == nullptr || !m_extensionTouch->fellBack() || !m_extensionTouchProblem.isEmpty()) {
        return;
    }
    qInfo("touch on %s goes through injection again: the touch method %s of an extension stopped working",
          qUtf8Printable(m_serial), qUtf8Printable(m_extensionTouchTitle));
    m_extensionTouchProblem = m_extensionTouchTitle;
    updatePadLink();
    emit touchUnavailable();
}

ControlSink& SessionController::touchSink()
{
    if (m_touchBackend == TouchBackend::None) {
        m_noTouch = std::make_unique<NoTouchSink>(*m_controlSink);
        qInfo("touch on %s is off: only keys and text reach the phone", qUtf8Printable(m_serial));
        return *m_noTouch;
    }
    if (m_touchBackend == TouchBackend::Injection) {
        return *m_controlSink;
    }
    if (showsAppDisplay()) {
        if (!std::exchange(m_touchNoted, true)) {
            qInfo("touch on the independent display of %s goes through injection: a UHID or AOA touch screen only "
                  "drives the main display",
                  qUtf8Printable(m_serial));
        }
        return *m_controlSink;
    }
    if (m_touchBackend == TouchBackend::Aoa) {
        return aoaSink();
    }
    m_uhidWire = std::make_unique<SessionUhidWire>(*m_session);
    m_uhidSink = std::make_unique<UhidTouchSink>(*m_controlSink, *m_uhidWire);
    return *m_uhidSink;
}

ControlSink& SessionController::aoaSink()
{
    m_touchNoted = true;
    m_aoaFailure = platform::UsbFailure::NotFound;
    std::unique_ptr<AoaLink> link = m_usbPhone ? openAoaLink(*m_usbPhone, m_aoaFailure) : nullptr;
    if (link == nullptr) {
        qInfo("touch on %s goes through injection: %s", qUtf8Printable(m_serial),
              m_aoaFailure == platform::UsbFailure::NoDriver ? "AOA needs the WinUSB driver for the phone"
              : m_aoaFailure == platform::UsbFailure::Busy   ? "another program holds the phone's USB"
                                                             : "the phone is not on USB for AOA");
        m_touchMissing = true;
        return *m_controlSink;
    }
    m_aoaPort = std::make_unique<AoaPort>(std::move(link), [this](AoaState state) {
        QMetaObject::invokeMethod(this, [this, state] { aoaChanged(state); }, Qt::QueuedConnection);
    });
    m_aoaSink = std::make_unique<AoaTouchSink>(m_controlSink.get(), *m_aoaPort, AoaDevices{});
    qInfo("touch on %s goes through an AOA touch screen on the USB cable", qUtf8Printable(m_serial));
    return *m_aoaSink;
}

void SessionController::aoaChanged(AoaState state)
{
    if (m_aoaSink == nullptr || m_aoaLost || state == AoaState::Ready || state == AoaState::Starting) {
        return;
    }
    m_aoaLost = true;
    m_aoaFailure = state == AoaState::Gone ? platform::UsbFailure::Gone : platform::UsbFailure::Refused;
    qInfo("touch on %s goes through injection again: %s", qUtf8Printable(m_serial),
          state == AoaState::Gone ? "the USB cable for AOA was unplugged" : "the phone refused the AOA touch screen");
    const std::optional<InputScreen> screen = m_channel->screen();
    m_channel->attach(*m_controlSink);
    if (screen) {
        m_channel->setVideo(screen->videoSession, static_cast<std::uint16_t>(screen->geometry.width),
                            static_cast<std::uint16_t>(screen->geometry.height));
    }
    updatePadLink();
    emit touchUnavailable();
}

void SessionController::updatePadLink()
{
    PadLink link{.route = PadRoute::None, .epoch = m_padEpoch, .reports = true};
    if (m_phase == session::SessionPhase::Streaming) {
        if (touchesOverAoa()) {
            link.route = PadRoute::Aoa;
        } else if (m_extensionTouch != nullptr && !m_extensionTouch->fellBack() &&
                   m_extensionTouch->capabilities().gamepad) {
            link.route = PadRoute::External;
        } else if ((m_capabilities & protocol::Welcome::kUhid) != 0 &&
                   (m_capabilities & protocol::Welcome::kUhidGamepad) != 0) {
            link.route = PadRoute::Uhid;
        } else {
            link.route = PadRoute::Missing;
        }
    }
    if (m_channel->setPadLink(link)) {
        emit padLinkChanged();
    }
}

void SessionController::noteTouch(std::uint32_t phone)
{
    if (std::exchange(m_touchNoted, true)) {
        return;
    }
    if ((phone & protocol::Welcome::kUhid) != 0) {
        qInfo("touch on %s goes through a UHID touch screen", qUtf8Printable(m_serial));
    } else {
        qInfo("touch on %s goes through injection: the phone cannot make UHID devices", qUtf8Printable(m_serial));
        m_touchMissing = true;
    }
}

bool SessionController::canMeasureLatency() const noexcept
{
    return m_session && m_phase == session::SessionPhase::Streaming &&
           m_pictureSource == settings::PictureSource::Phone && (m_capabilities & protocol::Welcome::kShowTouches) != 0;
}

std::shared_ptr<LatencyMeter> SessionController::meter() const
{
    const std::scoped_lock lock(m_meterMutex);
    return m_meter;
}

void SessionController::setRecorder(std::shared_ptr<record::Recorder> recorder)
{
    const bool asked = recorder != nullptr;
    {
        const std::scoped_lock lock(m_recorderMutex);
        m_recorder = std::move(recorder);
    }
    if (asked && m_session && m_phase == session::SessionPhase::Streaming) {
        m_session->send(protocol::RequestKeyFrame{});
    }
}

std::shared_ptr<record::Recorder> SessionController::recorder() const
{
    const std::scoped_lock lock(m_recorderMutex);
    return m_recorder;
}

bool SessionController::startLatencyTest()
{
    if (meter()) {
        return true;
    }
    if (!canMeasureLatency()) {
        return false;
    }
    auto meter = std::make_shared<LatencyMeter>();
    m_channel->setTouchObserver(
        [meter](const protocol::TouchFrame& frame, std::int64_t sentUs) { meter->touched(frame, sentUs); });
    m_frames->setShownListener([meter](std::int64_t ptsUs, std::int64_t shownUs) { meter->shown(ptsUs, shownUs); });
    {
        const std::scoped_lock lock(m_meterMutex);
        m_meter = meter;
    }
    protocol::SetShowTouches show;
    show.on = true;
    m_session->send(show);
    m_latency = meter->progress();
    m_latencyDeadline = std::chrono::steady_clock::now() + kLatencyTestLimit;
    m_latencyPoll.start();
    qInfo("measuring the real latency of %s with touch dots", qUtf8Printable(m_serial));
    emit latencyTestChanged();
    return true;
}

void SessionController::stopLatencyTest()
{
    if (const std::shared_ptr<LatencyMeter> running = meter()) {
        running->finish();
        endLatencyTest();
    }
}

void SessionController::pollLatency()
{
    const std::shared_ptr<LatencyMeter> running = meter();
    if (!running) {
        m_latencyPoll.stop();
        return;
    }
    if (std::chrono::steady_clock::now() >= m_latencyDeadline) {
        running->finish();
    }
    const LatencyProgress progress = running->progress();
    if (!progress.running) {
        endLatencyTest();
        return;
    }
    if (!m_latency || progress.measured != m_latency->measured || progress.missed != m_latency->missed) {
        m_latency = progress;
        emit latencyTestChanged();
    }
}

void SessionController::endLatencyTest()
{
    std::shared_ptr<LatencyMeter> ended;
    {
        const std::scoped_lock lock(m_meterMutex);
        ended = std::exchange(m_meter, nullptr);
    }
    if (!ended) {
        return;
    }
    m_latencyPoll.stop();
    m_channel->setTouchObserver({});
    m_frames->setShownListener({});
    if (m_session && m_phase == session::SessionPhase::Streaming) {
        protocol::SetShowTouches show;
        show.on = false;
        m_session->send(show);
    }
    m_latency = ended->progress();
    if (m_latency->result) {
        qInfo("real latency of %s: %.1f ms, the median of %d (%.1f to %.1f ms)", qUtf8Printable(m_serial),
              m_latency->result->medianMs, m_latency->result->samples, m_latency->result->lowMs,
              m_latency->result->highMs);
    } else {
        qInfo("the real latency of %s was not measured: %d seen, %d not seen", qUtf8Printable(m_serial),
              m_latency->measured, m_latency->missed);
    }
    emit latencyTestChanged();
}

void SessionController::refuseLatencyTest()
{
    const std::shared_ptr<LatencyMeter> running = meter();
    if (!running) {
        return;
    }
    qInfo("the real latency of %s cannot be measured: the phone does not allow touch dots", qUtf8Printable(m_serial));
    running->finish();
    endLatencyTest();
    if (m_latency) {
        m_latency->touchesRefused = true;
        emit latencyTestChanged();
    }
}

bool SessionController::active() const noexcept
{
    return m_phase != session::SessionPhase::Idle && m_phase != session::SessionPhase::Stopped &&
           m_phase != session::SessionPhase::Failed;
}

void SessionController::start(session::TransportChoice transport, const settings::Settings& settings)
{
    if (active()) {
        return;
    }
    releaseSession();
    m_screenOff = false;
    m_orientation = protocol::OrientationMode::Follow;
    session::SessionOptions options;
    options.clientVersion = std::string(buildInfo().version);
    protocol::ConfigureVideo video;
    switch (settings.video.codec) {
    case settings::Codec::H264:
        video.codec = protocol::VideoCodec::H264;
        break;
    case settings::Codec::H265:
        video.codec = protocol::VideoCodec::H265;
        break;
    case settings::Codec::Av1:
        video.codec = protocol::VideoCodec::Av1;
        break;
    }
    const bool controlOnly = m_pictureSource != settings::PictureSource::Phone && !showsAppDisplay();
    video.maxSize = controlOnly ? kControlOnlyMaxSize : static_cast<std::uint16_t>(settings.video.maxSize);
    video.maxFps = controlOnly ? kControlOnlyMaxFps : static_cast<std::uint16_t>(settings.video.maxFps);
    video.bitrate = controlOnly ? kControlOnlyBitrate : static_cast<std::uint32_t>(settings.video.bitrateKbps) * 1000U;
    if (showsAppDisplay()) {
        video.source = protocol::VideoSource::VirtualDisplay;
    }
    options.video = video;
    if (!showsAppDisplay()) {
        protocol::ConfigureAudio audio;
        audio.enabled = settings.audio.enabled && m_pictureSource != settings::PictureSource::Capture;
        audio.bitrate = static_cast<std::uint32_t>(settings.audio.bitrateKbps) * 1000U;
        options.audio = audio;
    }
    options.bulk = !showsAppDisplay();
    m_launchPending = false;
    m_bulkReady = false;
    std::unique_ptr<session::ServerHost> host = m_hostFactory ? m_hostFactory() : nullptr;
    if (host == nullptr) {
        host = std::make_unique<session::AdbServerHost>(m_adb, m_serial.toStdString(), m_package, transport);
    }
    m_session = std::make_unique<session::Session>(std::move(host), options, *m_bridge);
    if (controlOnly) {
        protocol::SetVideoPaused pause;
        pause.paused = true;
        m_session->send(pause);
        qInfo("%s sends no picture of its own: its video is %u pixels at most, %u frame a second and paused, which "
              "keeps the touch positions and the screen's turn",
              qUtf8Printable(m_serial), static_cast<unsigned>(kControlOnlyMaxSize),
              static_cast<unsigned>(kControlOnlyMaxFps));
    }
    m_controlSink = std::make_unique<SessionControlSink>(*m_session);
    m_touchBackend = touchBackendOf(backendOf(settings.touch.method), m_forcedTouch);
    m_touchNoted = false;
    m_touchMissing = false;
    m_aoaLost = false;
    m_aoaFailure = platform::UsbFailure::None;
    m_extensionTouchTitle.clear();
    m_extensionTouchProblem.clear();
    ControlSink* extension = nullptr;
    if (!settings.touch.extension.empty() && !m_forcedTouch.has_value() && !showsAppDisplay()) {
        m_touchBackend = TouchBackend::Injection;
        extension = extensionSink(settings.touch.extension);
    }
    m_channel->attach(extension != nullptr ? *extension : touchSink());
    m_phase = session::SessionPhase::Deploying;
    m_session->start();
}

void SessionController::stop()
{
    if (m_session) {
        m_session->stop();
    }
}

void SessionController::showAppDisplay(const QString& package)
{
    m_appPackage = package;
}

bool SessionController::requestAppList(std::uint32_t requestId)
{
    if (!m_session || m_phase != session::SessionPhase::Streaming || !m_bulkReady ||
        (m_capabilities & protocol::Welcome::kApps) == 0) {
        return false;
    }
    protocol::RequestAppList request;
    request.requestId = requestId;
    request.includeSystem = true;
    return m_session->send(request);
}

bool SessionController::requestAppIcon(std::uint32_t requestId, const QString& package, int sizePx)
{
    if (!m_session || m_phase != session::SessionPhase::Streaming || !m_bulkReady ||
        (m_capabilities & protocol::Welcome::kApps) == 0) {
        return false;
    }
    protocol::RequestAppIcon request;
    request.requestId = requestId;
    request.package = package.toStdString();
    request.sizePx = static_cast<std::uint16_t>(std::clamp(sizePx, 16, 512));
    return m_session->send(request);
}

bool SessionController::requestScreenshot(std::uint32_t requestId, std::uint32_t displayId)
{
    if (!m_session || m_phase != session::SessionPhase::Streaming || !m_bulkReady ||
        (m_capabilities & protocol::Welcome::kScreenshot) == 0) {
        return false;
    }
    protocol::RequestScreenshot request;
    request.requestId = requestId;
    request.displayId = displayId;
    request.format = protocol::ScreenshotFormat::Png;
    return m_session->send(request);
}

bool SessionController::startApp(const QString& package, bool forceStop)
{
    if (!m_session || m_phase != session::SessionPhase::Streaming || (m_capabilities & protocol::Welcome::kApps) == 0) {
        return false;
    }
    protocol::StartApp start;
    start.package = package.toStdString();
    start.forceStop = forceStop;
    start.onVideoDisplay = showsAppDisplay();
    return m_session->send(start);
}

void SessionController::applyLaunch()
{
    if (!m_launchPending || !showsAppDisplay()) {
        return;
    }
    m_launchPending = false;
    if (startApp(m_appPackage)) {
        qInfo("starting %s on an independent display of %s", qUtf8Printable(m_appPackage), qUtf8Printable(m_serial));
    }
}

void SessionController::setScreenOff(bool off)
{
    if (off == m_screenOff) {
        return;
    }
    m_screenOff = off;
    sendScreen();
}

void SessionController::setOrientation(protocol::OrientationMode mode)
{
    if (mode == m_orientation) {
        return;
    }
    m_orientation = mode;
    sendOrientation();
}

void SessionController::applyScreen()
{
    if (m_screenOff) {
        sendScreen();
    }
}

void SessionController::applyOrientation()
{
    if (m_orientation != protocol::OrientationMode::Follow) {
        sendOrientation();
    }
}

void SessionController::sendScreen()
{
    if (m_session && m_phase == session::SessionPhase::Streaming &&
        (m_capabilities & protocol::Welcome::kDisplayPower) != 0) {
        protocol::SetDisplayPower power;
        power.on = !m_screenOff;
        m_session->send(power);
    }
}

void SessionController::sendOrientation()
{
    if (m_session && m_phase == session::SessionPhase::Streaming &&
        (m_capabilities & protocol::Welcome::kRotation) != 0) {
        protocol::SetOrientation orientation;
        orientation.mode = m_orientation;
        m_session->send(orientation);
    }
}

void SessionController::setStatusReports(bool on)
{
    if (on == m_statusReports) {
        return;
    }
    m_statusReports = on;
    sendStatus();
}

void SessionController::applyStatus()
{
    if (m_statusReports) {
        sendStatus();
    }
}

bool SessionController::showSystemPanel(protocol::SystemPanelAction action)
{
    if (!m_session || m_phase != session::SessionPhase::Streaming) {
        return false;
    }
    protocol::SystemPanel panel;
    panel.action = action;
    return m_session->send(panel);
}

void SessionController::sendStatus()
{
    if (m_session && m_phase == session::SessionPhase::Streaming &&
        (m_capabilities & protocol::Welcome::kDeviceStatus) != 0) {
        protocol::SubscribeStatus subscribe;
        subscribe.on = m_statusReports;
        subscribe.periodMs = kStatusPeriodMs;
        m_session->send(subscribe);
    }
}

media::AudioStats SessionController::audioStats() const
{
    return m_bridge->audioStats();
}

void SessionController::setAudioMuted(bool muted)
{
    m_bridge->setAudioMuted(muted);
}

void SessionController::setAudioVolume(float volume)
{
    m_bridge->setAudioVolume(volume);
}

void SessionController::pressKey(std::uint32_t keycode)
{
    if (!m_session) {
        return;
    }
    protocol::Key key;
    key.keycode = keycode;
    key.action = protocol::KeyAction::Down;
    m_session->send(key);
    key.action = protocol::KeyAction::Up;
    m_session->send(key);
}

void SessionController::applyMouse()
{
    if (!m_session || m_mouseReady || m_pictureSource != settings::PictureSource::None || showsAppDisplay() ||
        (m_capabilities & protocol::Welcome::kUhid) == 0) {
        return;
    }
    m_mouseButtons = 0;
    m_session->send(uhidMouseCreate());
    m_mouseReady = true;
    qInfo("%s shows no picture: a virtual mouse on the phone follows this computer's mouse", qUtf8Printable(m_serial));
}

void SessionController::moveMouse(int dx, int dy)
{
    if (!m_session || !m_mouseReady || (dx == 0 && dy == 0)) {
        return;
    }
    m_session->send(uhidMouseReport(m_mouseButtons, dx, dy, 0, 0));
}

void SessionController::setMouseButtons(int buttons)
{
    const auto next = static_cast<std::uint8_t>(buttons & 0x1F);
    if (!m_session || !m_mouseReady || next == m_mouseButtons) {
        return;
    }
    m_mouseButtons = next;
    m_session->send(uhidMouseReport(m_mouseButtons, 0, 0, 0, 0));
}

}
