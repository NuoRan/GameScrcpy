#pragma once

#include "CommandTable.h"

#include <gs/protocol/Enums.h>

#include <QString>

#include <functional>
#include <optional>
#include <vector>

namespace gs::app {

struct DeviceFacts {
    QString serial;
    QString name;
    QString deviceId;
    bool canConnect = false;
    bool connected = false;
    bool busy = false;
    bool audio = false;
    bool muted = false;
    double volume = 1.0;
    bool diagnostics = false;
    bool fullScreen = false;
    bool mini = false;
    bool editing = false;
    bool trying = false;
    bool logic = false;
    bool latencyTest = false;
    bool measuringLatency = false;
    bool keymap = false;
    bool hasProfile = false;
    bool cameraLocked = false;
    bool keyHints = false;
    int profiles = 0;
    bool screenControl = false;
    bool screenOff = false;
    bool rotationControl = false;
    int orientation = 0;
    int transport = 0;
    bool transportChoice = true;
    bool wireless = false;
    bool apps = false;
    bool files = false;
    bool noPicture = false;
    bool recording = false;
    bool systemPanel = false;
};

class DeviceCommandHost
{
public:
    virtual ~DeviceCommandHost() = default;

    [[nodiscard]] virtual std::vector<DeviceFacts> devices() const = 0;
    [[nodiscard]] virtual std::optional<DeviceFacts> device(const QString& serial) const = 0;
    [[nodiscard]] virtual bool canRetryAdb() const = 0;

    virtual void retryAdb() = 0;
    virtual void connectDevice(const QString& serial) = 0;
    virtual void disconnectDevice(const QString& serial) = 0;
    virtual void showWindow(const QString& serial) = 0;
    virtual void setTransport(const QString& serial, int transport) = 0;
    virtual void setMuted(const QString& serial, bool muted) = 0;
    virtual void setVolume(const QString& serial, double volume) = 0;
    virtual void setScreenOff(const QString& serial, bool off) = 0;
    virtual void setOrientation(const QString& serial, int orientation) = 0;
    virtual void pressKey(const QString& serial, int keycode) = 0;
    virtual void setDiagnostics(const QString& serial, bool shown) = 0;
    virtual void setFullScreen(const QString& serial, bool on) = 0;
    virtual void setMini(const QString& serial, bool on) = 0;
    virtual void setEditing(const QString& serial, bool on) = 0;
    virtual void editorAction(const QString& serial, const QString& action) = 0;
    virtual void setMeasuringLatency(const QString& serial, bool on) = 0;
    virtual void toggleToolbar(const QString& serial) = 0;
    virtual void setKeymap(const QString& serial, bool on) = 0;
    virtual void setCameraLocked(const QString& serial, bool locked) = 0;
    virtual void setKeyHints(const QString& serial, bool shown) = 0;
    virtual void stepProfile(const QString& serial, int step) = 0;
    virtual void endWireless(const QString& serial) = 0;
    virtual void setRecording(const QString&, bool) {}
    virtual void showSystemPanel(const QString&, protocol::SystemPanelAction) {}
};

using UiRequest = std::function<void(const QString& id, const QString& target)>;

void addDeviceCommands(CommandTable& table, DeviceCommandHost& host, const UiRequest& request);
void addHubCommands(CommandTable& table, DeviceCommandHost& host, const UiRequest& request);
[[nodiscard]] bool passesTyping(QStringView command) noexcept;

}
