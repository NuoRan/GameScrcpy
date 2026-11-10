#pragma once

#include <gs/media/FrameSnapshot.h>
#include <gs/render/FrameSlot.h>
#include <gs/script/Resources.h>

#include <QByteArray>
#include <QFileSystemWatcher>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace gs::render {
class VideoItem;
}

namespace gs::app {

struct VisionPhone {
    QString serial;
    QString name;
};

struct VisionFeed {
    std::shared_ptr<render::FrameSlot> frames;
    std::shared_ptr<media::FrameSnapshots> snapshots;
    std::function<void(bool)> feed;
};

struct VisionRequestItem {
    QString name;
    QString kind;
    QString note;
    QString state;
    QString resource;
};

struct VisionRequestResult {
    QString name;
    QString kind;
    QString state;
    QString resource;
    QVariantMap value;
};

using VisionRequestDone = std::function<void(const std::vector<VisionRequestResult>&)>;

struct VisionSetup {
    QString profileId;
    QString profileName;
    std::filesystem::path folder;
    QString phone;
    std::function<std::vector<VisionPhone>()> phones;
    std::function<VisionFeed(const QString&)> feedOf;
    std::function<QStringList()> unsaved;
    bool draft = false;
};

[[nodiscard]] script::Resources mergeResources(const script::Resources& base, const script::Resources& ours,
                                               const script::Resources& theirs);

class VisionImageProvider final : public QQuickImageProvider
{
public:
    VisionImageProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    static void publish(const QString& key, const QImage& image);
    static void clear();
};

class VisionWorkbench final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The vision workbench is opened from a profile or a mirror window")
    Q_PROPERTY(QString profileId READ profileId CONSTANT FINAL)
    Q_PROPERTY(QString profileName READ profileName CONSTANT FINAL)
    Q_PROPERTY(bool saved READ saved NOTIFY savedChanged FINAL)
    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged FINAL)
    Q_PROPERTY(bool frozen READ frozen NOTIFY frozenChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(QSize pictureSize READ pictureSize NOTIFY pictureChanged FINAL)
    Q_PROPERTY(QString frozenKey READ frozenKey NOTIFY pictureChanged FINAL)
    Q_PROPERTY(QVariantList phones READ phones NOTIFY phonesChanged FINAL)
    Q_PROPERTY(QString phone READ phone NOTIFY phoneChanged FINAL)
    Q_PROPERTY(int revision READ revision NOTIFY resourcesChanged FINAL)
    Q_PROPERTY(QVariantList templates READ templates NOTIFY resourcesChanged FINAL)
    Q_PROPERTY(QVariantList regions READ regions NOTIFY resourcesChanged FINAL)
    Q_PROPERTY(QVariantList points READ points NOTIFY resourcesChanged FINAL)
    Q_PROPERTY(QVariantList paths READ paths NOTIFY resourcesChanged FINAL)
    Q_PROPERTY(QVariantList colors READ colors NOTIFY resourcesChanged FINAL)
    Q_PROPERTY(QString selectedKind READ selectedKind NOTIFY selectionChanged FINAL)
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY selectionChanged FINAL)
    Q_PROPERTY(QVariantMap selected READ selected NOTIFY selectionChanged FINAL)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged FINAL)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged FINAL)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged FINAL)
    Q_PROPERTY(QVariantMap result READ result NOTIFY resultsChanged FINAL)
    Q_PROPERTY(QVariantMap pick READ pick NOTIFY pickChanged FINAL)
    Q_PROPERTY(QVariantMap renaming READ renaming NOTIFY renamingChanged FINAL)
    Q_PROPERTY(bool draft READ draft CONSTANT FINAL)
    Q_PROPERTY(bool requesting READ requesting NOTIFY requestChanged FINAL)
    Q_PROPERTY(QVariantList requestItems READ requestItems NOTIFY requestChanged FINAL)
    Q_PROPERTY(int requestCurrent READ requestCurrent NOTIFY requestCurrentChanged FINAL)
    Q_PROPERTY(bool requestAnswered READ requestAnswered NOTIFY requestChanged FINAL)
    Q_PROPERTY(int requestDoneCount READ requestDoneCount NOTIFY requestChanged FINAL)

public:
    static constexpr std::size_t kMostUndo = 100;
    static constexpr int kMostRenamePlaces = 200;
    static constexpr int kLeastTemplateSide = 4;
    static constexpr double kNewTemplateThreshold = script::kNewTemplateThreshold;

    explicit VisionWorkbench(VisionSetup setup, QObject* parent = nullptr);
    ~VisionWorkbench() override;
    VisionWorkbench(const VisionWorkbench&) = delete;
    VisionWorkbench& operator=(const VisionWorkbench&) = delete;

    [[nodiscard]] QString profileId() const { return m_setup.profileId; }
    [[nodiscard]] QString profileName() const { return m_setup.profileName; }
    [[nodiscard]] bool saved() const noexcept { return m_saved; }
    [[nodiscard]] QString problem() const { return m_problem; }
    [[nodiscard]] bool frozen() const noexcept { return m_frozen; }
    [[nodiscard]] bool busy() const noexcept { return m_busy > 0; }
    [[nodiscard]] QSize pictureSize() const;
    [[nodiscard]] QString frozenKey() const;
    [[nodiscard]] QVariantList phones() const;
    [[nodiscard]] QString phone() const { return m_phone; }
    [[nodiscard]] int revision() const noexcept { return m_revision; }
    [[nodiscard]] QVariantList templates() const;
    [[nodiscard]] QVariantList regions() const;
    [[nodiscard]] QVariantList points() const;
    [[nodiscard]] QVariantList paths() const;
    [[nodiscard]] QVariantList colors() const;
    [[nodiscard]] QString selectedKind() const { return m_kind; }
    [[nodiscard]] QString selectedName() const { return m_name; }
    [[nodiscard]] QVariantMap selected() const;
    [[nodiscard]] bool canUndo() const noexcept { return !m_undo.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !m_redo.empty(); }
    [[nodiscard]] QVariantList results() const { return m_results; }
    [[nodiscard]] QVariantMap result() const { return m_result; }
    [[nodiscard]] QVariantMap pick() const { return m_pick; }
    [[nodiscard]] QVariantMap renaming() const { return m_renaming; }
    [[nodiscard]] bool draft() const noexcept { return m_setup.draft; }
    [[nodiscard]] bool requesting() const noexcept { return !m_request.empty(); }
    [[nodiscard]] QVariantList requestItems() const;
    [[nodiscard]] int requestCurrent() const noexcept { return m_requestCurrent; }
    [[nodiscard]] bool requestAnswered() const;
    [[nodiscard]] int requestDoneCount() const;
    [[nodiscard]] QStringList existingOf(const QString& kind) const;
    void beginRequest(std::vector<VisionRequestItem> items, VisionRequestDone done);
    void abandonRequest();
    [[nodiscard]] const script::Resources& resources() const noexcept { return m_state.resources; }
    [[nodiscard]] QByteArray imageOf(const QString& templateName) const;
    void startPick(const QString& kind, const QString& prompt, std::function<void(const QString&)> done);

    Q_INVOKABLE void attachVideo(QQuickItem* host);
    Q_INVOKABLE void setPhone(const QString& serial);
    Q_INVOKABLE void refreshPhones();
    Q_INVOKABLE void setFrozen(bool frozen);
    Q_INVOKABLE QString addTemplate(double x, double y, double width, double height);
    Q_INVOKABLE QString addRegion(double x, double y, double width, double height);
    Q_INVOKABLE QString addPoint(double x, double y);
    Q_INVOKABLE QString addPath(const QVariantList& points);
    Q_INVOKABLE QString pickColor(double x, double y);
    Q_INVOKABLE void select(const QString& kind, const QString& name);
    Q_INVOKABLE QString rename(const QString& kind, const QString& name, const QString& wanted);
    Q_INVOKABLE QString finishRename(bool alsoLogic);
    Q_INVOKABLE void cancelRename();
    Q_INVOKABLE bool setValue(const QString& kind, const QString& name, const QString& field, const QVariant& value);
    Q_INVOKABLE bool setArea(const QString& name, double x, double y, double width, double height);
    Q_INVOKABLE bool movePoint(const QString& name, double x, double y);
    Q_INVOKABLE bool movePath(const QString& name, double dx, double dy);
    Q_INVOKABLE bool remove(const QString& kind, const QString& name);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void testFind();
    Q_INVOKABLE void cancelPick();
    Q_INVOKABLE void chooseRequest(int index);
    Q_INVOKABLE void skipRequest(int index);
    Q_INVOKABLE void useExistingRequest(int index, const QString& name);
    Q_INVOKABLE void giveRequest();
    void shutdown();

signals:
    void savedChanged();
    void problemChanged();
    void frozenChanged();
    void busyChanged();
    void pictureChanged();
    void phonesChanged();
    void phoneChanged();
    void resourcesChanged();
    void selectionChanged();
    void historyChanged();
    void resultsChanged();
    void pickChanged();
    void renamingChanged();
    void requestChanged();
    void requestCurrentChanged();
    void notice(const QString& text, const QString& tone);

private:
    struct LogicEdit {
        QString file;
        QByteArray before;
        QByteArray afterHash;
    };

    struct LogicChange {
        std::string from;
        std::string to;
        std::vector<LogicEdit> files;
    };

    struct State {
        script::Resources resources;
        std::map<std::string, QByteArray> images;
        std::map<std::string, QRectF> cuts;
        LogicChange logic;
    };

    void load();
    void commit(State next, LogicChange logic = {});
    void changed();
    bool save();
    void takeForeign(const script::Resources& disk);
    void reloadFromDisk();
    void watchFiles();
    void picked(const QString& kind, const QString& name);
    void setBusy(bool busy);
    void setProblem(const QString& problem);
    void setSaved(bool saved);
    void clearResults();
    void feedLive();
    void followVideo();
    void showOutcome(const QVariantList& results, const QVariantMap& summary);
    void freezeShot(std::shared_ptr<const media::LumaSnapshot> shot, QImage picture);
    void publishPictures();
    [[nodiscard]] QString renameProblem(const QString& kind, const QString& name, const QString& wanted) const;
    [[nodiscard]] QString requestFilled(const QString& kind, const QString& name);
    [[nodiscard]] QVariantMap requestValueOf(const VisionRequestItem& item) const;
    [[nodiscard]] QString requestNameFor(const VisionRequestItem& item) const;
    [[nodiscard]] std::vector<VisionRequestResult> requestResults() const;
    void advanceRequest();
    void renameNow(const QString& kind, const QString& name, const QString& wanted, LogicChange logic);
    [[nodiscard]] bool askAboutLogic(const QString& kind, const QString& name, const QString& wanted);
    [[nodiscard]] LogicChange changeLogic(const QString& name, const QString& wanted);
    void undoLogic(const LogicChange& change);
    void redoLogic(const LogicChange& change);
    [[nodiscard]] QStringList unsavedFiles() const;
    [[nodiscard]] bool sharedName(const QString& kind, const QString& name) const;
    template <typename Work, typename Done>
    void later(Work work, Done done);
    [[nodiscard]] bool exists(const QString& kind, const QString& name) const;
    [[nodiscard]] QString freeName(const QString& kind) const;
    [[nodiscard]] std::string fileFor(const QString& name, const std::string& keep) const;
    [[nodiscard]] QString pictureKey(const QByteArray& bytes) const;
    [[nodiscard]] QSize pictureSizeOf(const QByteArray& bytes) const;

    VisionSetup m_setup;
    std::filesystem::path m_res;
    State m_state;
    script::Resources m_base;
    QFileSystemWatcher m_watcher;
    std::vector<State> m_undo;
    std::vector<State> m_redo;
    std::map<std::string, QByteArray> m_onDisk;
    bool m_blocked = false;
    bool m_saved = true;
    QString m_problem;
    QString m_kind;
    QString m_name;
    bool m_frozen = false;
    int m_busy = 0;
    std::shared_ptr<const media::LumaSnapshot> m_frozenShot;
    QImage m_frozenImage;
    int m_frozenRevision = 0;
    QString m_phone;
    VisionFeed m_feed;
    bool m_feeding = false;
    QPointer<render::VideoItem> m_video;
    QSize m_liveSize;
    QVariantList m_results;
    QVariantMap m_result;
    QVariantMap m_pick;
    QVariantMap m_renaming;
    std::vector<VisionRequestItem> m_request;
    VisionRequestDone m_requestDone;
    int m_requestCurrent = -1;
    std::function<void(const QString&)> m_picked;
    int m_revision = 0;
    std::set<QString> m_published;
    mutable std::map<QString, QSize> m_sizes;
};

}
