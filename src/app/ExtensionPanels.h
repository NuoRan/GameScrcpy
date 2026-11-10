#pragma once

#include <gs/ext/Manifest.h>
#include <gs/script/Services.h>

#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class QJSEngine;
class QQmlEngine;

namespace gs::app {

class ExtensionHost;
struct LogicCaller;

inline constexpr std::size_t kMostUiTreeBytes = 256 * 1024;
inline constexpr std::size_t kMostUiNodes = 2000;
inline constexpr std::size_t kMostUiDepth = 16;
inline constexpr std::size_t kMostStatusNodes = 64;
inline constexpr std::size_t kMostMenuItems = 16;
inline constexpr std::uintmax_t kMostUiImageBytes = 2 * 1024 * 1024;
inline constexpr std::chrono::milliseconds kUiRenderGap{34};

struct PanelDeclaration {
    std::string extension;
    std::string name;
    ext::PanelPlace where = ext::PanelPlace::Mirror;
    std::string script;
    QString title;
    QString icon;
    QString extensionName;
    std::filesystem::path folder;
    QString iconPath;
    std::vector<std::string> on;

    friend bool operator==(const PanelDeclaration&, const PanelDeclaration&) = default;
};

struct UiCheck {
    QVariant value;
    std::string problem;

    [[nodiscard]] bool ok() const noexcept { return problem.empty(); }
};

[[nodiscard]] UiCheck checkUiTree(std::string_view json, const std::filesystem::path& folder);
[[nodiscard]] UiCheck checkStatusTree(std::string_view json);
[[nodiscard]] UiCheck checkMenuItems(std::string_view json);
[[nodiscard]] UiCheck checkToolbarState(std::string_view json, const std::filesystem::path& folder);
[[nodiscard]] std::vector<PanelDeclaration> panelDeclarations(const ExtensionHost& host, const QString& language);

class ExtensionPanels : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantList panels READ panels NOTIFY panelsChanged FINAL)
    Q_PROPERTY(QVariantList menus READ menus NOTIFY menusChanged FINAL)

public:
    using Teller = std::function<bool(std::uint64_t logic, const QString& kind, const QString& id, const QString& json)>;

    explicit ExtensionPanels(QObject* parent);
    ~ExtensionPanels() override;
    ExtensionPanels(const ExtensionPanels&) = delete;
    ExtensionPanels& operator=(const ExtensionPanels&) = delete;

    static ExtensionPanels* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    [[nodiscard]] static ExtensionPanels* instance() noexcept { return s_instance; }

    void watch(ExtensionHost& host);
    void setDeclarations(std::vector<PanelDeclaration> declarations);
    void setTeller(Teller teller);
    [[nodiscard]] QVariantList panels() const { return m_panels; }
    [[nodiscard]] QVariantList menus() const { return m_menus; }

    Q_INVOKABLE QVariant tree(const QString& key) const;
    Q_INVOKABLE bool send(quint64 logic, const QString& panel, const QString& type, const QString& id,
                          const QVariant& value) const;
    Q_INVOKABLE bool choose(quint64 logic, const QString& menu, const QString& id, const QVariant& value,
                            const QVariantMap& target) const;
    Q_INVOKABLE QString iconOf(const QString& name) const;
    Q_INVOKABLE QVariantMap sideState(const QString& serial) const;
    Q_INVOKABLE void setSideState(const QString& serial, const QString& panel, bool open, int width);

signals:
    void panelsChanged();
    void menusChanged();
    void treeChanged(const QString& key);
    void closeRequested(const QString& key);

private:
    enum class Kind : std::uint8_t { Panel, Status, Menu, Toolbar };

    struct Slot {
        std::uint64_t logic = 0;
        Kind kind = Kind::Panel;
        std::string name;

        friend auto operator<=>(const Slot&, const Slot&) = default;
    };

    struct Shown {
        QVariant value;
        QVariant next;
        bool waiting = false;
        std::chrono::steady_clock::time_point at{};
    };

    struct Inbox {
        std::mutex mutex;
        ExtensionPanels* owner = nullptr;
        std::vector<PanelDeclaration> declarations;
        std::map<Slot, QVariant> pending;
    };

    struct Side {
        QString panel;
        bool open = false;
        int width = 0;
    };

    [[nodiscard]] static script::ServiceAnswer render(Inbox& inbox, const LogicCaller& caller, std::string_view json);
    [[nodiscard]] static script::ServiceAnswer status(Inbox& inbox, const LogicCaller& caller, std::string_view json);
    [[nodiscard]] static script::ServiceAnswer menu(Inbox& inbox, const LogicCaller& caller, std::string_view json);
    [[nodiscard]] static script::ServiceAnswer namedMenu(Inbox& inbox, const LogicCaller& caller, std::string_view json,
                                                         ext::PanelPlace where);
    [[nodiscard]] static script::ServiceAnswer toolbar(Inbox& inbox, const LogicCaller& caller, std::string_view json);
    [[nodiscard]] static script::ServiceAnswer close(Inbox& inbox, const LogicCaller& caller, std::string_view json);
    [[nodiscard]] static std::optional<PanelDeclaration> declarationFor(Inbox& inbox, const LogicCaller& caller,
                                                                        Kind kind, std::string_view name,
                                                                        std::string& problem);
    static void post(Inbox& inbox, Slot slot, QVariant value);
    void collect(const Slot& slot);
    void deliver(const Slot& slot, QVariant value);
    void show(const Slot& slot, QVariant value);
    void flush();
    void forget(std::uint64_t logic);
    void rebuild();
    void readIcons() const;
    [[nodiscard]] static QString keyOf(const Slot& slot);
    [[nodiscard]] static std::optional<Slot> slotOf(const QString& key);

    static inline ExtensionPanels* s_instance = nullptr;

    std::shared_ptr<Inbox> m_inbox = std::make_shared<Inbox>();
    std::vector<PanelDeclaration> m_declarations;
    std::map<Slot, Shown> m_shown;
    QVariantList m_panels;
    QVariantList m_menus;
    QTimer m_flush;
    Teller m_teller;
    mutable QSet<QString> m_icons;
    mutable bool m_iconsRead = false;
    std::map<QString, Side> m_sides;
};

}
