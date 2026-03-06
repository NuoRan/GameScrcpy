#ifndef ADBPROCESS_H
#define ADBPROCESS_H

#include "GameSignal.h"

#include <string>
#include <vector>
#include <cstdint>

class AdbProcessImpl;
namespace qsc {

// ---------------------------------------------------------
// ADB 进程管理器 / ADB Process Manager
// 封装 ADB 命令执行，提供设备连接、文件推送、端口转发等功能
// Wraps ADB command execution: device connection, file push, port forwarding, etc.
// ---------------------------------------------------------
class AdbProcess
{
public:
    enum ADB_EXEC_RESULT
    {
        AER_SUCCESS_START,        // 启动成功
        AER_ERROR_START,          // 启动失败
        AER_SUCCESS_EXEC,         // 执行成功
        AER_ERROR_EXEC,           // 执行失败
        AER_ERROR_MISSING_BINARY, // 找不到文件
    };

    explicit AdbProcess();
    virtual ~AdbProcess();

    static void setAdbPath(const std::string& adbPath);

    void execute(const std::string &serial, const std::vector<std::string> &args);
    void forward(const std::string &serial, uint16_t localPort, const std::string &deviceSocketName);
    void forwardRemove(const std::string &serial, uint16_t localPort);
    void reverse(const std::string &serial, const std::string &deviceSocketName, uint16_t localPort);
    void reverseRemove(const std::string &serial, const std::string &deviceSocketName);
    void push(const std::string &serial, const std::string &local, const std::string &remote);
    void install(const std::string &serial, const std::string &local);
    void removePath(const std::string &serial, const std::string &path);
    bool isRuning();
    void setShowTouchesEnabled(const std::string &serial, bool enabled);
    void kill();
    std::vector<std::string> arguments();
    std::vector<std::string> getDevicesSerialFromStdOut();
    std::string getDeviceIPFromStdOut();
    std::string getDeviceIPByIpFromStdOut();
    std::string getStdOut();
    std::string getErrorOut();

    // 信号 (Signal<>) — 替代 Qt signals
    Signal<ADB_EXEC_RESULT> adbProcessResult;

private:
    AdbProcessImpl* m_adbImpl = nullptr;
};

}
#endif // ADBPROCESS_H
