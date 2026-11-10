#pragma once

#include <gs/base/Expected.h>
#include <gs/esp32/Link.h>
#include <gs/esp32/Port.h>
#include <gs/esp32/TouchMethod.h>

#include <QObject>
#include <QString>
#include <QStringView>
#include <QTimer>

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace gs::platform {
class UsbWatcher;
}

namespace gs::app {

inline constexpr std::string_view kEsp32Extension = "gamescrcpy.esp32";
inline constexpr std::string_view kEsp32Method = "gamescrcpy.esp32/esp32";

[[nodiscard]] QString esp32SerialOf(const QString& where);
[[nodiscard]] bool isEsp32Serial(QStringView serial) noexcept;
[[nodiscard]] std::string esp32TouchOptions(std::string_view where, bool wifi, const std::filesystem::path& boardsFile);
[[nodiscard]] bool esp32SamePort(std::string_view a, std::string_view b) noexcept;

enum class Esp32Holder : std::uint8_t { Session, Flash };

struct Esp32Claim {
    std::string where;
    bool wifi = false;
    bool every = false;
    Esp32Holder holder = Esp32Holder::Session;

    friend bool operator==(const Esp32Claim&, const Esp32Claim&) = default;
};

[[nodiscard]] Esp32Claim esp32ClaimOf(const esp32::Esp32Settings& settings);
[[nodiscard]] bool esp32Covers(const Esp32Claim& claim, std::string_view where, bool wifi) noexcept;

struct Esp32Candidate {
    std::string where;
    bool wifi = false;
    esp32::PortKind kind = esp32::PortKind::Other;
    bool chosen = false;

    friend bool operator==(const Esp32Candidate&, const Esp32Candidate&) = default;
};

[[nodiscard]] std::vector<Esp32Candidate> esp32Candidates(const esp32::Esp32Settings& settings,
                                                         const std::vector<esp32::SerialPortInfo>& ports,
                                                         const std::vector<Esp32Claim>& blockers);

struct Esp32Probe {
    std::unique_ptr<esp32::Esp32Link> link;
    std::string where;
    bool wifi = false;
    esp32::PortKind kind = esp32::PortKind::Other;
};

using Esp32Probed = Expected<Esp32Probe, esp32::OpenFailure>;

[[nodiscard]] Esp32Probed probeEsp32(const Esp32Candidate& candidate, const esp32::Esp32Settings& settings,
                                     esp32::LinkEvents events, const std::stop_token& stop,
                                     esp32::BoardTrouble& trouble);

struct Esp32Hooks {
    std::function<std::vector<esp32::SerialPortInfo>()> list = esp32::listSerialPorts;
    std::function<Esp32Probed(const Esp32Candidate&, const esp32::Esp32Settings&, esp32::LinkEvents,
                              const std::stop_token&, esp32::BoardTrouble&)>
        probe = probeEsp32;
    bool watchUsb = true;
};

struct Esp32Found {
    std::string where;
    bool wifi = false;
    esp32::PortKind kind = esp32::PortKind::Other;
    std::optional<esp32::BoardInfo> info;
    esp32::BoardStatus status;
    bool legacy = false;
    bool connected = false;
    bool held = false;
    bool flashing = false;
    QString heldBy;
};

class Esp32Boards final : public QObject
{
    Q_OBJECT

public:
    explicit Esp32Boards(Esp32Hooks hooks = {}, QObject* parent = nullptr);
    ~Esp32Boards() override;
    Esp32Boards(const Esp32Boards&) = delete;
    Esp32Boards& operator=(const Esp32Boards&) = delete;

    [[nodiscard]] static Esp32Boards* instance() noexcept { return s_instance; }

    void start();
    void setActive(bool active);
    void setSettings(esp32::Esp32Settings settings);
    void rescan();

    [[nodiscard]] bool active() const noexcept { return m_active; }
    [[nodiscard]] const esp32::Esp32Settings& settings() const noexcept { return m_settings; }
    [[nodiscard]] std::vector<Esp32Found> boards() const;
    [[nodiscard]] std::optional<Esp32Found> board(std::string_view where, bool wifi) const;
    [[nodiscard]] std::optional<Esp32Found> matching(const esp32::Esp32Settings& settings) const;
    [[nodiscard]] esp32::Esp32Link* link(std::string_view where, bool wifi) const;
    [[nodiscard]] std::optional<esp32::OpenFailure> problem() const noexcept { return m_problem; }
    [[nodiscard]] std::optional<esp32::BoardTrouble> trouble() const { return m_trouble; }

    void claim(const QString& owner, Esp32Claim claim);
    void release(const QString& owner);
    [[nodiscard]] bool held(std::string_view where, bool wifi) const;
    [[nodiscard]] bool inSession(std::string_view where, bool wifi) const;
    void dismiss(std::string_view where, bool wifi);
    void forgive();

signals:
    void boardsChanged();
    void claimsChanged();
    void activeChanged();
    void scanned(bool complete);

private:
    struct Entry {
        Esp32Found found;
        std::unique_ptr<esp32::Esp32Link> link;
    };

    struct Job {
        bool active = false;
        std::uint64_t generation = 0;
        std::uint64_t claims = 0;
        esp32::Esp32Settings settings;
        std::vector<Esp32Claim> blockers;
    };

    struct Scan {
        std::uint64_t generation = 0;
        std::uint64_t claims = 0;
        bool complete = false;
        std::vector<Esp32Probe> found;
        std::vector<esp32::SerialPortInfo> ports;
        std::vector<std::string> silent;
        std::optional<esp32::OpenFailure> problem;
        std::optional<esp32::BoardTrouble> trouble;
    };

    struct Gate {
        std::mutex mutex;
        Esp32Boards* owner = nullptr;
    };

    static void post(const std::shared_ptr<Gate>& gate, std::function<void(Esp32Boards&)> work);
    void run(const std::stop_token& stop);
    [[nodiscard]] Scan scanOnce(const Job& job, const std::stop_token& cancel) const;
    [[nodiscard]] esp32::LinkEvents linkEvents() const;
    [[nodiscard]] Job jobNow() const;
    void requestScan();
    void settleWorker();
    [[nodiscard]] bool adopt(bool& complete);
    void drain();
    void refresh();
    void closeAll();
    void updateRetry();
    void updateFound(Entry& entry) const;
    [[nodiscard]] const Esp32Claim* claimOver(const Entry& entry, QString* owner = nullptr) const;
    [[nodiscard]] bool dismissed(std::string_view where, bool wifi) const;
    [[nodiscard]] Entry* entryAt(std::string_view where, bool wifi);
    [[nodiscard]] const Entry* entryAt(std::string_view where, bool wifi) const;

    static inline Esp32Boards* s_instance = nullptr;

    const Esp32Hooks m_hooks;
    const std::shared_ptr<Gate> m_gate = std::make_shared<Gate>();
    esp32::Esp32Settings m_settings;
    bool m_active = false;
    bool m_started = false;
    std::uint64_t m_generation = 1;
    std::uint64_t m_claimsVersion = 0;
    std::vector<Entry> m_entries;
    std::map<QString, Esp32Claim> m_claims;
    std::vector<Esp32Claim> m_dismissed;
    std::vector<Esp32Claim> m_quiet;
    std::map<int, int> m_strikes;
    std::optional<esp32::OpenFailure> m_problem;
    std::optional<esp32::BoardTrouble> m_trouble;
    QTimer m_retry;

    std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::condition_variable_any m_idle;
    Job m_job;
    bool m_dirty = false;
    bool m_scanning = false;
    std::stop_source m_cancel;
    std::vector<Scan> m_inbox;

    std::unique_ptr<platform::UsbWatcher> m_watcher;
    std::jthread m_thread;
};

}
