#pragma once

#include "DeviceCommands.h"

namespace gs::app {

class DeviceManager;
class WirelessController;

class AppCommandHost final : public DeviceCommandHost
{
public:
    AppCommandHost(DeviceManager& devices, WirelessController& wireless);

    [[nodiscard]] std::vector<DeviceFacts> devices() const override;
    [[nodiscard]] std::optional<DeviceFacts> device(const QString& serial) const override;
    [[nodiscard]] bool canRetryAdb() const override;

    void retryAdb() override;
    void connectDevice(const QString& serial) override;
    void disconnectDevice(const QString& serial) override;
    void showWindow(const QString& serial) override;
    void setTransport(const QString& serial, int transport) override;
    void setMuted(const QString& serial, bool muted) override;
    void setVolume(const QString& serial, double volume) override;
    void setScreenOff(const QString& serial, bool off) override;
    void setOrientation(const QString& serial, int orientation) override;
    void pressKey(const QString& serial, int keycode) override;
    void setDiagnostics(const QString& serial, bool shown) override;
    void setFullScreen(const QString& serial, bool on) override;
    void setMini(const QString& serial, bool on) override;
    void setEditing(const QString& serial, bool on) override;
    void editorAction(const QString& serial, const QString& action) override;
    void setMeasuringLatency(const QString& serial, bool on) override;
    void toggleToolbar(const QString& serial) override;
    void setKeymap(const QString& serial, bool on) override;
    void setCameraLocked(const QString& serial, bool locked) override;
    void setKeyHints(const QString& serial, bool shown) override;
    void stepProfile(const QString& serial, int step) override;
    void endWireless(const QString& serial) override;
    void setRecording(const QString& serial, bool on) override;
    void showSystemPanel(const QString& serial, protocol::SystemPanelAction action) override;

private:
    DeviceManager& m_devices;
    WirelessController& m_wireless;
};

}
