#pragma once

#include "BlueprintLabels.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

class QTextDocument;

namespace gs::blueprint {
class Api;
class Document;
struct Layout;
struct Outcome;
}

namespace gs::app {

[[nodiscard]] std::optional<std::string> instrumentModule(const std::filesystem::path& folder, const std::string& module,
                                                          const std::string& typescript);
[[nodiscard]] blueprint::Environment blueprintEnvironment(const std::filesystem::path& folder, const std::string& fileName,
                                                          const blueprint::Labels* labels);

struct TextChange {
    int start = 0;
    int end = 0;
    QString text;
};

class BlueprintText
{
public:
    BlueprintText() = default;
    BlueprintText(const BlueprintText&) = delete;
    BlueprintText& operator=(const BlueprintText&) = delete;
    virtual ~BlueprintText() = default;

    [[nodiscard]] virtual QString text() const = 0;
    virtual bool edit(std::span<const TextChange> changes) = 0;
    [[nodiscard]] virtual QTextDocument* editor() const = 0;
    virtual void setLayoutDirty(bool dirty) { static_cast<void>(dirty); }
};

}

Q_DECLARE_INTERFACE(gs::app::BlueprintText, "org.gamescrcpy.BlueprintText/1")

namespace gs::app {

class BlueprintModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject* bench READ bench WRITE setBench NOTIFY benchChanged FINAL)
    Q_PROPERTY(QObject* document READ document WRITE setDocument NOTIFY documentChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY graphChanged FINAL)
    Q_PROPERTY(QString problem READ problem NOTIFY graphChanged FINAL)
    Q_PROPERTY(int problemLine READ problemLine NOTIFY graphChanged FINAL)
    Q_PROPERTY(QVariantList pages READ pages NOTIFY graphChanged FINAL)
    Q_PROPERTY(QString page READ page WRITE setPage NOTIFY pageChanged FINAL)
    Q_PROPERTY(QVariantList crumbs READ crumbs NOTIFY graphChanged FINAL)
    Q_PROPERTY(QString graphText READ graphText NOTIFY graphChanged FINAL)
    Q_PROPERTY(QString sidebarText READ sidebarText NOTIFY graphChanged FINAL)
    Q_PROPERTY(QVariantMap viewState READ viewState NOTIFY graphChanged FINAL)
    Q_PROPERTY(QStringList selection READ selection WRITE setSelection NOTIFY selectionChanged FINAL)
    Q_PROPERTY(QVariantMap details READ details NOTIFY detailsChanged FINAL)
    Q_PROPERTY(QVariantMap variableEditor READ variableEditor NOTIFY variableEditorChanged FINAL)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged FINAL)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged FINAL)
    Q_PROPERTY(bool layoutDirty READ layoutDirty NOTIFY layoutDirtyChanged FINAL)
    Q_PROPERTY(bool debugging READ debugging WRITE setDebugging NOTIFY debuggingChanged FINAL)
    Q_PROPERTY(QString pausedNode READ pausedNode NOTIFY pausedChanged FINAL)
    Q_PROPERTY(QVariantMap pinValues READ pinValues NOTIFY pausedChanged FINAL)
    Q_PROPERTY(QStringList reached READ reached NOTIFY reachedChanged FINAL)
    Q_PROPERTY(QString diffBase READ diffBase WRITE setDiffBase NOTIFY diffBaseChanged FINAL)
    Q_PROPERTY(QVariantList diffRemoved READ diffRemoved NOTIFY graphChanged FINAL)

public:
    explicit BlueprintModel(QObject* parent = nullptr);
    ~BlueprintModel() override;
    BlueprintModel(const BlueprintModel&) = delete;
    BlueprintModel& operator=(const BlueprintModel&) = delete;

    [[nodiscard]] QObject* bench() const { return m_bench; }
    void setBench(QObject* bench);
    [[nodiscard]] QObject* document() const { return m_documentObject; }
    void setDocument(QObject* document);
    [[nodiscard]] bool ready() const noexcept { return m_ready; }
    [[nodiscard]] QString problem() const { return m_problem; }
    [[nodiscard]] int problemLine() const noexcept { return m_problemLine; }
    [[nodiscard]] QVariantList pages() const { return m_pages; }
    [[nodiscard]] QString page() const { return m_page; }
    void setPage(const QString& page);
    [[nodiscard]] QVariantList crumbs() const { return m_crumbs; }
    [[nodiscard]] QVariantMap graph() const { return m_graph; }
    [[nodiscard]] QVariantList sidebar() const { return m_sidebar; }
    [[nodiscard]] QString graphText() const { return m_graphText; }
    [[nodiscard]] QString sidebarText() const { return m_sidebarText; }
    [[nodiscard]] QVariantMap viewState() const { return m_viewState; }
    [[nodiscard]] QStringList selection() const { return m_selection; }
    void setSelection(const QStringList& keys);
    [[nodiscard]] QVariantMap details() const { return m_details; }
    [[nodiscard]] QVariantMap variableEditor() const { return m_variableEditor; }
    [[nodiscard]] bool canUndo() const noexcept { return !m_undo.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !m_redo.empty(); }
    [[nodiscard]] bool layoutDirty() const noexcept { return m_layoutDirty; }
    [[nodiscard]] bool debugging() const noexcept { return m_debugging; }
    void setDebugging(bool on);
    [[nodiscard]] QString pausedNode() const { return m_pausedNode; }
    [[nodiscard]] QVariantMap pinValues() const { return m_pinValues; }
    [[nodiscard]] QStringList reached() const { return m_reached; }
    [[nodiscard]] QString diffBase() const { return m_diffBase; }
    void setDiffBase(const QString& base);
    [[nodiscard]] QVariantList diffRemoved() const { return m_diffRemoved; }

    Q_INVOKABLE QString check(const QString& from, const QString& fromPin, const QString& to, const QString& toPin) const;
    Q_INVOKABLE QString connectPins(const QString& from, const QString& fromPin, const QString& to, const QString& toPin);
    Q_INVOKABLE QString rewire(const QString& node, const QString& pin, const QString& toNode, const QString& toPin);
    Q_INVOKABLE QString disconnectPin(const QString& node, const QString& pin);
    Q_INVOKABLE QString removeWire(const QString& wire);
    Q_INVOKABLE QString setValue(const QString& node, const QString& pin, const QVariant& value);
    Q_INVOKABLE QVariantList catalog(const QString& fromNode, const QString& fromPin, const QString& query) const;
    Q_INVOKABLE QVariantMap add(const QString& id, double x, double y, const QString& fromNode, const QString& fromPin);
    Q_INVOKABLE QVariantMap drop(const QVariant& item, double x, double y, const QString& node, const QString& pin);
    Q_INVOKABLE QVariantMap insertOnWire(const QString& wire, const QString& node);
    Q_INVOKABLE QString remove(const QStringList& keys);
    Q_INVOKABLE QString duplicate(const QStringList& keys);
    Q_INVOKABLE QString copyText(const QStringList& keys) const;
    Q_INVOKABLE QString paste(const QString& text, double x, double y);
    Q_INVOKABLE QString setCode(const QString& node, const QString& text);
    Q_INVOKABLE QString replaceControl(const QString& node, const QString& from, const QString& to);
    Q_INVOKABLE QVariantList controls() const;
    Q_INVOKABLE QString collapse(const QStringList& keys);
    Q_INVOKABLE QString move(const QStringList& keys, double dx, double dy);
    Q_INVOKABLE QString addComment(const QStringList& keys);
    Q_INVOKABLE QString editComment(const QString& key, const QVariantMap& changes);
    Q_INVOKABLE QString addReroute(const QString& wire, double x, double y);
    Q_INVOKABLE QString arrange();
    Q_INVOKABLE void measured(const QVariantMap& sizes);
    Q_INVOKABLE void setView(double x, double y, double zoom);
    Q_INVOKABLE QString open(const QString& node);
    Q_INVOKABLE QVariantMap jumpTarget(const QString& node) const;
    Q_INVOKABLE QString toggleBreakpoint(const QString& node);
    Q_INVOKABLE QString enableBreakpoint(const QString& node, bool on);
    Q_INVOKABLE QString editDetail(const QString& name, const QVariant& value);
    Q_INVOKABLE void newVariable();
    Q_INVOKABLE void editVariable(const QString& key);
    Q_INVOKABLE void closeVariable();
    Q_INVOKABLE QString saveVariable(const QVariantMap& spec);
    Q_INVOKABLE QString removeVariable(const QString& key);
    Q_INVOKABLE QString promoteToVariable(const QString& node, const QString& pin);
    Q_INVOKABLE QVariantMap addType(const QString& kind, const QString& name);
    Q_INVOKABLE QString editType(const QString& name, const QString& field, const QVariant& value);
    Q_INVOKABLE QVariantMap typeInfo(const QString& name) const;
    Q_INVOKABLE QString addState(double x, double y);
    Q_INVOKABLE QString connectStates(const QString& from, const QString& to);
    Q_INVOKABLE QString setTransitionLabel(const QString& wire, const QString& text);
    Q_INVOKABLE QString renameState(const QString& node, const QString& name);
    Q_INVOKABLE QString setStart(const QString& node);
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE bool saveLayout();

    Q_INVOKABLE void hit(const QString& position);
    Q_INVOKABLE void pausedAt(const QString& position, const QVariantMap& values);
    Q_INVOKABLE void resumed();
    Q_INVOKABLE void machineState(const QString& machinePosition, const QString& state, const QString& reason,
                                  double since, double at = -1);
    Q_INVOKABLE void resetMachines();
    Q_INVOKABLE QVariantList breakpointPositions() const;
    Q_INVOKABLE QString nodeAt(const QString& position) const;
    Q_INVOKABLE QVariantMap instrument(const QString& source) const;

    [[nodiscard]] QString layoutPath() const;

signals:
    void benchChanged();
    void documentChanged();
    void graphChanged();
    void pageChanged();
    void selectionChanged();
    void detailsChanged();
    void variableEditorChanged();
    void historyChanged();
    void layoutDirtyChanged();
    void debuggingChanged();
    void pausedChanged();
    void reachedChanged();
    void diffBaseChanged();
    void pulse(const QString& node, const QString& wire);
    void jumpRequested(const QString& path, int line, int column);
    void toast(const QString& text);

private slots:
    void textChanged();
    void documentSaved();
    void benchFilesChanged();
    void editorCommandAdded();

private:
    struct Step {
        bool text = false;
        bool external = false;
        int undoIndex = -1;
        QString before;
        QString after;
        std::shared_ptr<const blueprint::Layout> layoutBefore;
        std::shared_ptr<const blueprint::Layout> layoutAfter;
    };

    struct Transition {
        QString time;
        QString text;
        QString tone;
    };

    struct MachineRun {
        QString state;
        double since = 0;
        QString wire;
        std::set<QString> visited;
        std::map<QString, std::vector<Transition>> log;
    };

    [[nodiscard]] BlueprintText* text() const;
    [[nodiscard]] QString documentPath() const;
    [[nodiscard]] QString folder() const;
    [[nodiscard]] QString documentFolder() const;
    [[nodiscard]] QString layoutKey() const;
    void refreshEnvironment();
    void loadLayout();
    void scheduleParse();
    void parse();
    [[nodiscard]] blueprint::Document* doc();
    void rebuild(const QString& text, std::shared_ptr<const blueprint::Layout> layout);
    void publish();
    void publishDetails();
    [[nodiscard]] QVariantMap variableForm(QVariantMap spec) const;
    [[nodiscard]] QString apply(const blueprint::Outcome& outcome);
    [[nodiscard]] QString applyLayout(const blueprint::Outcome& outcome);
    void setLayoutDirty(bool dirty);
    void rememberLayout();
    void persistView();
    [[nodiscard]] std::shared_ptr<const blueprint::Layout> currentLayout() const;
    void restore(const Step& step, bool forward);
    void syncWithEditor();

    QPointer<QObject> m_bench;
    QPointer<QObject> m_documentObject;
    QPointer<QTextDocument> m_editor;
    BlueprintLabels m_labels;
    std::shared_ptr<const blueprint::Api> m_api;
    blueprint::Environment m_environment;
    std::unique_ptr<blueprint::Document> m_doc;
    std::string m_utf8;
    QString m_parsedText;
    std::map<QString, std::shared_ptr<const blueprint::Layout>> m_layouts;
    std::map<std::string, std::pair<double, double>> m_sizes;
    bool m_ready = false;
    QString m_problem;
    int m_problemLine = 0;
    QVariantList m_pages;
    QString m_page;
    QVariantList m_crumbs;
    QVariantMap m_graph;
    QVariantList m_sidebar;
    QString m_graphText = QStringLiteral("{}");
    QString m_sidebarText = QStringLiteral("[]");
    QVariantMap m_viewState;
    QStringList m_selection;
    QVariantMap m_details;
    QVariantMap m_variableEditor;
    std::vector<Step> m_undo;
    std::vector<Step> m_redo;
    std::shared_ptr<const blueprint::Layout> m_cleanLayout;
    bool m_layoutDirty = false;
    bool m_parseQueued = false;
    bool m_applying = false;
    bool m_debugging = false;
    QString m_pausedNode;
    QVariantMap m_pinValues;
    QStringList m_reached;
    QString m_diffBase;
    QVariantList m_diffRemoved;
    QString m_lastHit;
    std::map<QString, MachineRun> m_machines;
};

}
