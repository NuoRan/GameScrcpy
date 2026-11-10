#include "AppCommandHost.h"

#include "DeviceManager.h"
#include "WirelessController.h"

namespace gs::app {

AppCommandHost::AppCommandHost(DeviceManager& devices, WirelessController& wireless)
    : m_devices(devices), m_wireless(wireless)
{
}

std::vector<DeviceFacts> AppCommandHost::devices() const
{
    return m_devices.facts();
}

std::optional<DeviceFacts> AppCommandHost::device(const QString& serial) const
{
    return m_devices.facts(serial);
}

bool AppCommandHost::canRetryAdb() const
{
    return m_devices.canRetryAdb() && !m_devices.adbStarting();
}

void AppCommandHost::retryAdb()
{
    m_devices.retryAdb();
}

void AppCommandHost::connectDevice(const QString& serial)
{
    m_devices.connectDevice(serial);
}

void AppCommandHost::disconnectDevice(const QString& serial)
{
    m_devices.disconnectDevice(serial);
}

void AppCommandHost::showWindow(const QString& serial)
{
    m_devices.showWindow(serial);
}

void AppCommandHost::setTransport(const QString& serial, int transport)
{
    m_devices.setTransport(serial, transport);
}

void AppCommandHost::setMuted(const QString& serial, bool muted)
{
    m_devices.setMuted(serial, muted);
}

void AppCommandHost::setVolume(const QString& serial, double volume)
{
    m_devices.setVolume(serial, volume);
}

void AppCommandHost::setScreenOff(const QString& serial, bool off)
{
    m_devices.setScreenOff(serial, off);
}

void AppCommandHost::setOrientation(const QString& serial, int orientation)
{
    m_devices.setOrientation(serial, orientation);
}

void AppCommandHost::pressKey(const QString& serial, int keycode)
{
    m_devices.pressKey(serial, keycode);
}

void AppCommandHost::setDiagnostics(const QString& serial, bool shown)
{
    m_devices.setDiagnostics(serial, shown);
}

void AppCommandHost::setFullScreen(const QString& serial, bool on)
{
    m_devices.setFullScreen(serial, on);
}

void AppCommandHost::setMini(const QString& serial, bool on)
{
    m_devices.setMini(serial, on);
}

void AppCommandHost::setEditing(const QString& serial, bool on)
{
    m_devices.setEditing(serial, on);
}

void AppCommandHost::editorAction(const QString& serial, const QString& action)
{
    m_devices.editorAction(serial, action);
}

void AppCommandHost::setMeasuringLatency(const QString& serial, bool on)
{
    m_devices.setMeasuringLatency(serial, on);
}

void AppCommandHost::toggleToolbar(const QString& serial)
{
    m_devices.toggleToolbar(serial);
}

void AppCommandHost::setKeymap(const QString& serial, bool on)
{
    m_devices.setKeymap(serial, on);
}

void AppCommandHost::setCameraLocked(const QString& serial, bool locked)
{
    m_devices.setCameraLocked(serial, locked);
}

void AppCommandHost::setKeyHints(const QString&, bool shown)
{
    m_devices.setKeyHints(shown);
}

void AppCommandHost::stepProfile(const QString& serial, int step)
{
    m_devices.stepProfile(serial, step);
}

void AppCommandHost::endWireless(const QString& serial)
{
    if (const auto facts = m_devices.facts(serial); facts && (facts->connected || facts->busy)) {
        m_devices.disconnectDevice(serial);
    }
    m_wireless.disconnectDevice(serial);
}

void AppCommandHost::setRecording(const QString& serial, bool on)
{
    m_devices.setRecording(serial, on);
}

void AppCommandHost::showSystemPanel(const QString& serial, protocol::SystemPanelAction action)
{
    m_devices.showSystemPanel(serial, action);
}

}
