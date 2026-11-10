#pragma once

#include <gs/input/Geometry.h>
#include <gs/input/InputSource.h>
#include <gs/input/Profile.h>

#include "LogicReferences.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gs::app {

struct ComponentChild;
struct DeclaredModule;

class ProfileEditor : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The device manager makes an editor for a phone's profile")
    Q_PROPERTY(QString profileName READ profileName NOTIFY changed FINAL)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed FINAL)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed FINAL)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed FINAL)
    Q_PROPERTY(int conflictCount READ conflictCount NOTIFY changed FINAL)
    Q_PROPERTY(int revision READ revision NOTIFY changed FINAL)
    Q_PROPERTY(QStringList selection READ selection NOTIFY selectionChanged FINAL)
    Q_PROPERTY(bool trying READ trying NOTIFY tryingChanged FINAL)
    Q_PROPERTY(QString logicBlocked READ logicBlocked NOTIFY logicChanged FINAL)
    Q_PROPERTY(bool logicShown READ logicShown NOTIFY logicShownChanged FINAL)
    Q_PROPERTY(QObject* logic READ logic NOTIFY logicShownChanged FINAL)
    Q_PROPERTY(bool gamepadPassthrough READ gamepadPassthrough NOTIFY changed FINAL)
    Q_PROPERTY(QStringList handedOver READ handedOver NOTIFY handedOverChanged FINAL)

public:
    static constexpr std::size_t kMostSteps = 200;

    using LogicFolder = std::function<QString(QString& problem)>;

    ProfileEditor(input::Profile profile, input::ScreenGeometry screen, QObject* parent = nullptr);

    [[nodiscard]] const input::Profile& profile() const noexcept { return m_history[m_at].profile; }
    [[nodiscard]] QString profileName() const;
    [[nodiscard]] bool dirty() const;
    [[nodiscard]] bool canUndo() const noexcept { return m_at > 0; }
    [[nodiscard]] bool canRedo() const noexcept { return m_at + 1 < m_history.size(); }
    [[nodiscard]] int conflictCount() const noexcept { return static_cast<int>(m_conflicts.size()); }
    [[nodiscard]] int revision() const noexcept { return m_revision; }
    [[nodiscard]] QStringList selection() const { return m_selection; }
    [[nodiscard]] QStringList conflictingIds() const;
    [[nodiscard]] QHash<QString, int> conflictGroups() const;

    void setScreen(const input::ScreenGeometry& screen);
    void markSaved();
    void setLogic(QString folder, QString blocked, LogicFolder ensure);
    [[nodiscard]] QString logicBlocked() const { return m_logicBlocked; }
    [[nodiscard]] bool logicShown() const noexcept { return m_logicShown; }
    [[nodiscard]] QObject* logic() const noexcept { return m_logic.data(); }
    void attachLogic(QObject* logic);
    [[nodiscard]] QStringList handedOver() const { return m_handedOver; }
    void setHandedOver(const QStringList& controls);

    Q_INVOKABLE QVariantList markers(double width, double height) const;
    Q_INVOKABLE QVariantList keyList() const;
    Q_INVOKABLE QString addControl(const QString& type, double x, double y);
    Q_INVOKABLE QVariantMap addComponent(const QString& kind, double x, double y);
    Q_INVOKABLE bool bindInput(const QString& id, const QString& slot, const QString& input);
    Q_INVOKABLE bool rebind(const QString& id, const QString& slot, const QString& input);
    Q_INVOKABLE bool rename(const QString& id, const QString& name);
    Q_INVOKABLE QString codeNameProblem(const QString& id, const QString& wanted) const;
    Q_INVOKABLE bool renameCode(const QString& id, const QString& wanted);
    Q_INVOKABLE int logicUses();
    Q_INVOKABLE void deleteSelection(bool withBlocks);
    Q_INVOKABLE QVariantList form(const QString& id) const;
    Q_INVOKABLE QVariantList behavior(const QString& id) const;
    Q_INVOKABLE bool setField(const QString& id, const QString& key, const QVariant& value);
    Q_INVOKABLE bool setKey(const QString& id, const QString& binding, const QString& input);
    Q_INVOKABLE bool clearKey(const QString& id, const QString& binding);
    Q_INVOKABLE bool setWhen(const QString& id, const QString& when);
    Q_INVOKABLE QString componentOf(const QString& id) const;
    Q_INVOKABLE bool saveAsDefault(const QString& id);
    Q_INVOKABLE bool hasSavedDefault(const QString& id) const;
    Q_INVOKABLE bool restoreFactoryDefault(const QString& id);
    Q_INVOKABLE QString inputOfKey(quint32 nativeScanCode, int modifiers) const;
    Q_INVOKABLE QString inputOfMouse(int button, int modifiers) const;
    Q_INVOKABLE QString inputOfWheel(int dx, int dy, int modifiers) const;
    Q_INVOKABLE void select(const QStringList& ids);
    Q_INVOKABLE void toggleSelected(const QString& id);
    Q_INVOKABLE void selectConflicts();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void beginGesture();
    Q_INVOKABLE void endGesture();
    Q_INVOKABLE void moveSelection(double dx, double dy);
    Q_INVOKABLE void placeControl(const QString& id, double x, double y);
    Q_INVOKABLE void removeSelection();
    Q_INVOKABLE QStringList duplicateSelection();
    Q_INVOKABLE bool copySelection();
    Q_INVOKABLE bool canPaste() const;
    Q_INVOKABLE QStringList pasteAt(double x, double y);
    Q_INVOKABLE bool setValue(const QString& id, const QString& path, const QVariant& value);
    Q_INVOKABLE QVariant value(const QString& id, const QString& path) const;
    Q_INVOKABLE bool setLength(const QString& id, const QString& path, double width);
    Q_INVOKABLE bool setArea(const QString& id, const QString& path, double width, double height);
    Q_INVOKABLE bool setPoint(const QString& id, const QString& path, double x, double y);
    Q_INVOKABLE bool setPathPoint(const QString& id, int index, double x, double y);
    Q_INVOKABLE bool setPlacedPoint(const QString& id, const QString& name, double x, double y);
    Q_INVOKABLE QVariantList wheelCells(const QString& id) const;
    Q_INVOKABLE QVariantList wheelTargets(const QString& id) const;
    Q_INVOKABLE bool setWheelCell(const QString& id, int cell, const QString& key, const QVariant& value);
    Q_INVOKABLE bool setWheelCellCount(const QString& id, int count);
    Q_INVOKABLE QString placedRef(const QString& id) const;
    Q_INVOKABLE void alignSelection(const QString& edge);
    Q_INVOKABLE void distributeSelection(bool horizontal);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE bool showLogic(bool on);
    Q_PROPERTY(bool preview READ preview NOTIFY previewChanged FINAL)
    Q_PROPERTY(QString previewClient READ previewClient NOTIFY previewChanged FINAL)
    Q_PROPERTY(QString previewTarget READ previewTarget NOTIFY previewChanged FINAL)

    struct Preview {
        QString client;
        QString target;
        input::Profile live;
        QString liveFolder;
        QString draftFolder;
    };

    void setPreview(Preview plan);
    [[nodiscard]] bool preview() const noexcept { return m_preview.has_value(); }
    [[nodiscard]] QString previewClient() const;
    [[nodiscard]] QString previewTarget() const;
    Q_INVOKABLE QString liveTextOf(const QString& path) const;
    Q_INVOKABLE QVariantList codeDiff(const QString& path) const;
    [[nodiscard]] bool gamepadPassthrough() const noexcept
    {
        return profile().gamepad == input::GamepadMode::Passthrough;
    }
    Q_INVOKABLE bool setGamepadPassthrough(bool on);

signals:
    void changed();
    void selectionChanged();
    void tryingChanged();
    void defaultsBusy();
    void logicChanged();
    void logicShownChanged();
    void logicWanted(const QString& folder);
    void logicProblem(const QString& text);
    void logicActionWanted(const QString& action);
    void handedOverChanged();
    void previewChanged();
    void deleteAsked(int uses);

private:
    QVariantMap addPlaced(const QString& ref, double x, double y);
    [[nodiscard]] QVariantList placedFields(std::size_t index) const;
    bool setPlacedProp(const QString& id, const QString& name, const QVariant& value);
    void addChild(input::Profile& next, const std::string& group, input::Vec2 center, const ComponentChild& child,
                  std::size_t index, std::size_t count) const;
    [[nodiscard]] bool syncChildren(input::Profile& next) const;
    void carryChildren(input::Profile& next) const;

    [[nodiscard]] std::optional<std::size_t> indexOf(const QString& id) const;
    [[nodiscard]] QString displayName(std::size_t index) const;
    [[nodiscard]] QString conflictText(const std::string& path) const;
    [[nodiscard]] input::CoordinateMapper mapper() const;
    [[nodiscard]] std::string freeId(std::string_view base, const input::Profile& profile) const;
    void placeAt(input::Control& control, input::Vec2 position) const;
    [[nodiscard]] std::vector<std::pair<std::size_t, input::Vec2>> selectedCenters(const input::Profile& profile) const;

    struct LogicEdit {
        QString file;
        QByteArray before;
        QByteArray after;
        bool existedBefore = true;
        bool existsAfter = true;
    };

    struct Step {
        input::Profile profile;
        QStringList selectionBefore;
        QStringList selectionAfter;
        std::vector<LogicEdit> logic;
    };

    struct CodeRename {
        std::string from;
        std::string to;
    };

    [[nodiscard]] std::string codeBaseOf(const input::Control& control) const;
    [[nodiscard]] std::string keyPartOf(const input::Control& control) const;
    [[nodiscard]] std::string uniqueId(const std::string& wanted, const input::Profile& profile,
                                       std::string_view ignoring) const;
    [[nodiscard]] std::vector<CodeRename> followKeys(input::Profile& next) const;
    void renameReferences(input::Profile& next, const std::vector<CodeRename>& renames) const;
    [[nodiscard]] std::vector<LogicEdit> changeLogic(const std::vector<CodeRename>& renames,
                                                     const std::vector<std::string>& gone);
    void restoreLogic(const std::vector<LogicEdit>& edits, bool forward);
    [[nodiscard]] QStringList idsToRemove() const;
    [[nodiscard]] OpenTexts openLogicTexts() const;
    [[nodiscard]] QStringList unsavedLogicFiles() const;
    void rememberGone(const QString& id, const QString& name);

    void commit(input::Profile next, std::optional<QStringList> selection = std::nullopt,
                std::vector<LogicEdit> logic = {}, bool follow = true);
    void commitEdit(input::Profile next, const QString& id, const QString& key, bool mergeable);
    void refresh(const QStringList& before);
    void keepSelectionValid();
    [[nodiscard]] QVariant lengthValue(double width) const;
    [[nodiscard]] QVariant areaValue(double width, double height) const;
    [[nodiscard]] QVariant pointValue(double x, double y) const;
    [[nodiscard]] QString logicFolder(QString& problem);

    std::vector<Step> m_history;
    std::size_t m_at = 0;
    input::Profile m_saved;
    bool m_inGesture = false;
    bool m_gestureCommitted = false;
    int m_revision = 0;
    input::ScreenGeometry m_screen;
    QStringList m_selection;
    QStringList m_handedOver;
    std::vector<input::InputConflict> m_conflicts;
    std::optional<input::Chord> m_defaultLockKey;
    std::optional<std::pair<QString, QString>> m_lastEdit;
    QString m_logicFolder;
    QString m_logicBlocked;
    LogicFolder m_ensureLogic;
    QPointer<QObject> m_logic;
    bool m_logicShown = false;
    std::optional<Preview> m_preview;
    QHash<QString, QString> m_previewMarks;

public:
    void markSavedAs(const QString& name);
    void retranslate();
    void replaceSaved(input::Profile profile);

    [[nodiscard]] bool trying() const noexcept { return m_trying; }
    [[nodiscard]] std::shared_ptr<const input::Profile> trial() const;
    Q_INVOKABLE void setTrying(bool on);
    Q_INVOKABLE QVariantList previewMarkers(double width, double height) const;

private:
    bool m_trying = false;
    mutable std::shared_ptr<const input::Profile> m_trial;
    mutable int m_trialRevision = -1;

public:
    struct Suggested {
        input::Control control;
        std::optional<input::Vec2> at;
    };

    Q_PROPERTY(int suggestionRevision READ suggestionRevision NOTIFY suggestionsChanged FINAL)

    [[nodiscard]] int suggestionRevision() const noexcept { return m_suggestionRevision; }
    QStringList suggest(std::vector<Suggested> controls);
    [[nodiscard]] QString suggestionState(const QString& id) const;
    Q_INVOKABLE QVariantList suggestionMarkers(double width, double height) const;
    Q_INVOKABLE bool acceptSuggestion(const QString& id);
    Q_INVOKABLE bool rejectSuggestion(const QString& id);
    Q_INVOKABLE bool placeSuggestion(const QString& id, double x, double y);

signals:
    void suggestionsChanged();
    void suggestionDecided(const QString& id, const QString& state, const QString& control);

private:
    struct Suggestion {
        QString id;
        input::Control control;
        QString state;
    };

    std::vector<Suggestion> m_suggestions;
    int m_lastSuggestion = 0;
    int m_suggestionRevision = 0;

public:
    static QString nameFor(const QString& kind);
    Q_INVOKABLE QVariantMap addBehavior(const QString& module, double x, double y);
    Q_INVOKABLE QVariantList attachmentsOf(const QString& id) const;
    Q_INVOKABLE QVariantList attachable(const QString& id) const;
    Q_INVOKABLE bool addAttachment(const QString& id, const QString& module);
    Q_INVOKABLE bool removeAttachment(const QString& id, int index);
    Q_INVOKABLE bool moveAttachment(const QString& id, int index, int step);
    Q_INVOKABLE bool enableAttachment(const QString& id, int index, bool on);
    Q_INVOKABLE bool skipGlobal(const QString& id, const QString& module, bool skip);
    Q_INVOKABLE QVariantList behaviorParams(const QString& id) const;
    Q_INVOKABLE bool setModuleParam(const QString& id, int attachment, const QString& key, const QString& json);

private:
    [[nodiscard]] QVariantList behaviorFields(std::size_t index) const;
    [[nodiscard]] QVariantList modulePoints(std::size_t index, double width, double height) const;
    bool setBehaviorInput(const QString& id, const QString& slot, const QString& input);
    bool setModulePoint(const QString& id, const QString& name, double x, double y);
};

[[nodiscard]] std::shared_ptr<const input::Profile> profileInUse(const ProfileEditor* editor,
                                                                std::shared_ptr<const input::Profile> saved);
[[nodiscard]] QVariantList declaredParamMaps(const DeclaredModule& declared, const std::string& params);

}
