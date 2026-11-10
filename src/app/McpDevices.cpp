#include "McpDevices.h"

#include "DeviceManager.h"
#include "ProfileEditor.h"
#include "Recordings.h"
#include "ScriptRuntime.h"

#include <gs/media/FrameSnapshot.h>

#include <QVariantMap>

#include <algorithm>
#include <utility>

namespace gs::app {

McpDevices mcpDevicesOf(DeviceManager& devices)
{
    McpDevices hooks;
    hooks.phones = [&devices] {
        std::vector<McpPhone> phones;
        const std::vector<LogicPhone> logic = devices.logicPhones();
        for (const QVariant& item : devices.mirrors()) {
            const QVariantMap mirror = item.toMap();
            McpPhone phone{.serial = mirror.value(QStringLiteral("serial")).toString(),
                           .name = mirror.value(QStringLiteral("name")).toString(),
                           .app = mirror.value(QStringLiteral("app")).toString(),
                           .profileId = mirror.value(QStringLiteral("profileId")).toString(),
                           .profileName = mirror.value(QStringLiteral("profileName")).toString()};
            if (const std::optional<script::DeviceInfo> info = devices.deviceInfo(phone.serial)) {
                phone.width = info->width;
                phone.height = info->height;
            }
            const auto found = std::ranges::find(logic, phone.serial, &LogicPhone::serial);
            if (found != logic.end() && !found->folder.isEmpty()) {
                phone.folder = std::filesystem::path(found->folder.toStdWString());
            }
            phones.push_back(std::move(phone));
        }
        return phones;
    };
    hooks.screenshot = [&devices](const QString& serial, std::function<void(const QImage&)> done) {
        return devices.requestScreenshot(serial, std::move(done));
    };
    hooks.frames = [&devices](const QString& serial) -> McpFrameCapture {
        std::shared_ptr<media::FrameSnapshots> snapshots = devices.snapshotsOf(serial);
        if (snapshots == nullptr) {
            return {};
        }
        return [snapshots = std::move(snapshots)] { return snapshots->capture(true); };
    };
    hooks.startScript = [&devices](const QString& serial, ScriptJob job, quint64& number) {
        return devices.startScriptJob(serial, std::move(job), number);
    };
    hooks.stopScript = [&devices](quint64 number) { devices.stopScriptJob(number); };
    hooks.recording = [](const QString& serial) {
        const Recordings* recordings = Recordings::instance();
        return recordings != nullptr && recordings->running(serial);
    };
    hooks.startRecording = [&devices](const QString& serial) {
        devices.setRecording(serial, true);
        const Recordings* recordings = Recordings::instance();
        if (recordings != nullptr && recordings->running(serial)) {
            return QString();
        }
        return QStringLiteral("The recording did not start; the mirror window of the phone tells the user why");
    };
    hooks.stopRecording = [&devices](const QString& serial) { return devices.endRecording(serial); };
    hooks.recordingsFolder = [] {
        const Recordings* recordings = Recordings::instance();
        return recordings != nullptr ? recordings->location() : QString();
    };
    hooks.materials = [&devices](const McpMaterialsRequest& request, McpMaterialsDone done) {
        QVariantList items;
        for (const McpMaterialItem& item : request.items) {
            items.append(QVariantMap{{QStringLiteral("name"), item.name},
                                     {QStringLiteral("kind"), item.kind},
                                     {QStringLiteral("note"), item.note}});
        }
        return devices.requestMaterials(request.client, request.serial, request.profileId, request.profileName,
                                        QString::fromStdWString(request.folder.wstring()), items,
                                        [done = std::move(done)](const QVariantList& results) {
            std::vector<McpMaterialResult> made;
            made.reserve(static_cast<std::size_t>(results.size()));
            for (const QVariant& result : results) {
                const QVariantMap entry = result.toMap();
                made.push_back({.name = entry.value(QStringLiteral("name")).toString(),
                                .kind = entry.value(QStringLiteral("kind")).toString(),
                                .state = entry.value(QStringLiteral("state")).toString(),
                                .resource = entry.value(QStringLiteral("resource")).toString(),
                                .value = entry.value(QStringLiteral("value")).toMap()});
            }
            if (done) {
                done(made);
            }
        });
    };
    hooks.endMaterials = [&devices](const QString& client) { devices.endMaterials(client); };
    hooks.openEditor = [&devices](const QString& serial, const QString& profileId, QString& problem) -> ProfileEditor* {
        QString chosen = serial;
        if (chosen.isEmpty()) {
            for (const QVariant& item : devices.mirrors()) {
                const QVariantMap mirror = item.toMap();
                if (mirror.value(QStringLiteral("profileId")).toString() == profileId) {
                    chosen = mirror.value(QStringLiteral("serial")).toString();
                    break;
                }
            }
        }
        if (chosen.isEmpty()) {
            problem = QStringLiteral("No mirroring phone uses the profile %1; the keymap editor needs a phone's picture, "
                                     "so give serial")
                          .arg(profileId);
            return nullptr;
        }
        if (devices.editorOf(chosen) == nullptr) {
            if (profileId.isEmpty()) {
                devices.setEditing(chosen, true);
            } else {
                static_cast<void>(devices.editProfile(chosen, profileId));
            }
        }
        ProfileEditor* editor = devices.editorOf(chosen);
        if (editor == nullptr) {
            problem = QStringLiteral("The keymap editor could not be opened on %1; the phone must be mirroring and use a "
                                     "profile")
                          .arg(chosen);
        }
        return editor;
    };
    hooks.openDraftPreview = [&devices](const QString& target, const QString& serial, QString& problem) {
        return devices.openDraftPreviewAt(target, serial, problem);
    };
    return hooks;
}

}
