#pragma once

#include "AiService.h"
#include "LogicServices.h"

#include <gs/base/Clock.h>
#include <gs/media/FrameSnapshot.h>
#include <gs/script/Services.h>

#include <QByteArray>
#include <QObject>
#include <QSize>
#include <QString>
#include <QTimer>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace gs::app {

class AiAssistant;
class AppSettings;
class ExtensionHost;

using AiFrameCapture = std::function<std::shared_ptr<const media::LumaSnapshot>()>;

struct AiScriptHooks {
    std::function<bool()> allowed;
    std::function<bool()> ready;
    std::function<std::unique_ptr<AiProvider>()> provider;
    std::function<bool(const QString& extension)> permitted;
    std::function<AiFrameCapture(const QString& serial)> frames;
};

struct AiScriptReplies {
    std::function<void(std::uint64_t logic, quint64 request, const QString& json)> answer;
    std::function<void(std::uint64_t logic, quint64 request, const QString& message)> refuse;
};

[[nodiscard]] AiScriptHooks aiScriptHooks(const AppSettings& settings, AiAssistant& assistant,
                                          const ExtensionHost& extensions,
                                          std::function<AiFrameCapture(const QString& serial)> frames);

class AiScripts final : public QObject
{
    Q_OBJECT

public:
    static constexpr std::string_view kService = "ai.ask";
    static constexpr qsizetype kMostQuestionChars = 4000;
    static constexpr std::size_t kMostCallsPerMinute = 10;
    static constexpr std::chrono::seconds kCallWindow{60};
    static constexpr int kMostPictureSide = 1024;
    static constexpr int kMostTokens = 2048;
    static constexpr std::chrono::milliseconds kDefaultWait{60'000};
    static constexpr std::chrono::milliseconds kShortestWait{1'000};
    static constexpr std::chrono::milliseconds kLongestWait{120'000};

    AiScripts(AiScriptHooks hooks, const Clock& clock, QObject* parent = nullptr);
    ~AiScripts() override;
    AiScripts(const AiScripts&) = delete;
    AiScripts& operator=(const AiScripts&) = delete;

    void offerService();
    void setReplies(AiScriptReplies replies);
    void request(const LogicCaller& caller, quint64 id, const QString& json);
    void drop(const LogicCaller& caller, quint64 id);
    void endLogic(std::uint64_t logic);
    void expire();
    [[nodiscard]] std::size_t pending() const noexcept { return m_pending.size(); }

private:
    using Key = std::pair<std::uint64_t, quint64>;

    struct Pending {
        LogicCaller caller;
        QString question;
        Microseconds deadline{0};
        std::chrono::milliseconds wait{0};
        std::unique_ptr<AiProvider> provider;
    };

    [[nodiscard]] QString refusalFor(const LogicCaller& caller, bool picture);
    [[nodiscard]] bool admit(std::uint64_t logic);
    void look(const Key& key, AiFrameCapture capture, std::optional<script::Region> region);
    void pictured(const Key& key, const QByteArray& png, QSize size);
    void unpictured(const Key& key);
    void send(const Key& key, const QByteArray& png);
    void answered(const Key& key, AiReply reply);
    void schedule();
    void answer(std::uint64_t logic, quint64 request, const QString& json) const;
    void refuse(std::uint64_t logic, quint64 request, const QString& message) const;

    AiScriptHooks m_hooks;
    const Clock& m_clock;
    AiScriptReplies m_replies;
    QTimer m_deadline;
    std::map<Key, Pending> m_pending;
    std::map<std::uint64_t, std::deque<Microseconds>> m_recent;
    bool m_offered = false;
};

}
