#define LOG_TAG "AdbProcess"
#include "Logger.h"
#include "ThreadDispatcher.h"

#include "adbprocess.h"
#include "adbprocessimpl.h"

std::string g_adbPath;

namespace qsc {

AdbProcess::AdbProcess()
    : m_adbImpl(new AdbProcessImpl())
{
    // Bridge callback → Signal<> (thread-safe via dispatch::postToMain)
    m_adbImpl->setResultCallback([this](ADB_EXEC_RESULT result) {
        dispatch::postToMain([this, result]() {
            adbProcessResult.fire(result);
        });
    });
}

AdbProcess::~AdbProcess()
{
    m_adbImpl->setResultCallback(nullptr);
    if (m_adbImpl->isRuning()) {
        m_adbImpl->kill();
    }
    delete m_adbImpl;
}

void AdbProcess::setAdbPath(const std::string &adbPath)
{
    g_adbPath = adbPath;
}

void AdbProcess::execute(const std::string &serial, const std::vector<std::string> &args)
{
    m_adbImpl->execute(serial, args);
}

bool AdbProcess::isRuning()
{
    return m_adbImpl->isRuning();
}

void AdbProcess::setShowTouchesEnabled(const std::string &serial, bool enabled)
{
    m_adbImpl->setShowTouchesEnabled(serial, enabled);
}

void AdbProcess::kill()
{
    m_adbImpl->kill();
}

std::vector<std::string> AdbProcess::arguments()
{
    return m_adbImpl->arguments();
}

std::vector<std::string> AdbProcess::getDevicesSerialFromStdOut()
{
    return m_adbImpl->getDevicesSerialFromStdOut();
}

std::string AdbProcess::getDeviceIPFromStdOut()
{
    return m_adbImpl->getDeviceIPFromStdOut();
}

std::string AdbProcess::getDeviceIPByIpFromStdOut()
{
    return m_adbImpl->getDeviceIPByIpFromStdOut();
}

std::string AdbProcess::getStdOut()
{
    return m_adbImpl->getStdOut();
}

std::string AdbProcess::getErrorOut()
{
    return m_adbImpl->getErrorOut();
}

void AdbProcess::forward(const std::string &serial, uint16_t localPort, const std::string &deviceSocketName)
{
    m_adbImpl->forward(serial, localPort, deviceSocketName);
}

void AdbProcess::forwardRemove(const std::string &serial, uint16_t localPort)
{
    m_adbImpl->forwardRemove(serial, localPort);
}

void AdbProcess::reverse(const std::string &serial, const std::string &deviceSocketName, uint16_t localPort)
{
    m_adbImpl->reverse(serial, deviceSocketName, localPort);
}

void AdbProcess::reverseRemove(const std::string &serial, const std::string &deviceSocketName)
{
    m_adbImpl->reverseRemove(serial, deviceSocketName);
}

void AdbProcess::push(const std::string &serial, const std::string &local, const std::string &remote)
{
    m_adbImpl->push(serial, local, remote);
}

void AdbProcess::install(const std::string &serial, const std::string &local)
{
    m_adbImpl->install(serial, local);
}

void AdbProcess::removePath(const std::string &serial, const std::string &path)
{
    m_adbImpl->removePath(serial, path);
}

}
