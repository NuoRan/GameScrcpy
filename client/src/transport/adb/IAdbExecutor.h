#ifndef IADBEXECUTOR_H
#define IADBEXECUTOR_H

#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <memory>

#include "ErrorCode.h"
#include "GameSignal.h"

namespace qsc {

/**
 * @brief ADB 执行结果 / ADB Execution Result
 */
enum class AdbExecResult
{
    Success,            // 执行成功 / Success
    StartFailed,        // 启动失败 / Start failed
    ExecFailed,         // 执行失败 / Execution failed
    Timeout,            // 超时 / Timeout
    Cancelled           // 已取消 / Cancelled
};

/**
 * @brief 设备状态 / Device State
 */
enum class DeviceState
{
    Unknown,            // 未知 / Unknown
    Online,             // 在线 / Online
    Offline,            // 离线 / Offline
    Unauthorized,       // 未授权 / Unauthorized
    Bootloader,         // Bootloader 模式 / Bootloader mode
    Recovery,           // 恢复模式 / Recovery mode
    Sideload,           // Sideload 模式 / Sideload mode
    Disconnected        // 已断开 / Disconnected
};

/**
 * @brief 设备信息 / Device Information
 */
struct DeviceInfo
{
    std::string serial;             // 设备序列号 / Device serial number
    DeviceState state;          // 设备状态 / Device state
    std::string model;              // 设备型号 / Device model
    std::string product;            // 产品名 / Product name
    std::string device;             // 设备名 / Device name
    std::string transportId;        // 传输 ID
    bool isWireless = false;    // 是否为无线连接
};

/**
 * @brief ADB 命令执行回调
 * @param result 执行结果
 * @param output 标准输出
 * @param error 标准错误
 */
using AdbCallback = std::function<void(AdbExecResult result, const std::string& output, const std::string& error)>;

/**
 * @brief ADB 执行器抽象接口
 *
 * 定义 ADB 操作的标准接口，封装所有与 ADB 相关的操作。
 * 支持同步和异步两种执行模式。
 */
class IAdbExecutor
{
public:
    IAdbExecutor() = default;
    virtual ~IAdbExecutor() = default;

    // =========================================================================
    // 基础操作
    // =========================================================================

    /**
     * @brief 设置 ADB 可执行文件路径
     */
    virtual void setAdbPath(const std::string& path) = 0;

    /**
     * @brief 获取 ADB 路径
     */
    virtual std::string adbPath() const = 0;

    /**
     * @brief 检查 ADB 是否可用
     */
    virtual bool isAdbAvailable() const = 0;

    /**
     * @brief 获取 ADB 版本
     */
    virtual std::string adbVersion() const = 0;

    // =========================================================================
    // 设备管理
    // =========================================================================

    /**
     * @brief 获取设备列表
     * @return 设备信息列表
     */
    virtual std::vector<DeviceInfo> devices() = 0;

    /**
     * @brief 等待设备连接
     * @param serial 设备序列号（空表示任意设备）
     * @param timeoutMs 超时时间
     * @return 操作结果
     */
    virtual VoidResult waitForDevice(const std::string& serial = std::string(), int timeoutMs = 30000) = 0;

    /**
     * @brief 获取设备状态
     */
    virtual DeviceState deviceState(const std::string& serial) = 0;

    // =========================================================================
    // 命令执行
    // =========================================================================

    /**
     * @brief 异步执行 ADB 命令
     * @param serial 设备序列号
     * @param args 命令参数
     */
    virtual void executeAsync(const std::string& serial, const std::vector<std::string>& args) = 0;

    /**
     * @brief 同步执行 ADB 命令
     * @param serial 设备序列号
     * @param args 命令参数
     * @param timeoutMs 超时时间
     * @return 命令输出
     */
    virtual Result<std::string> executeSync(
        const std::string& serial,
        const std::vector<std::string>& args,
        int timeoutMs = 10000
    ) = 0;

    /**
     * @brief 执行 shell 命令
     */
    virtual void shell(const std::string& serial, const std::string& command) = 0;

    /**
     * @brief 同步执行 shell 命令
     */
    virtual Result<std::string> shellSync(
        const std::string& serial,
        const std::string& command,
        int timeoutMs = 10000
    ) = 0;

    // =========================================================================
    // 文件操作
    // =========================================================================

    /**
     * @brief 推送文件到设备
     * @param serial 设备序列号
     * @param localPath 本地路径
     * @param remotePath 远程路径
     */
    virtual void push(const std::string& serial, const std::string& localPath, const std::string& remotePath) = 0;

    /**
     * @brief 从设备拉取文件
     */
    virtual void pull(const std::string& serial, const std::string& remotePath, const std::string& localPath) = 0;

    /**
     * @brief 安装 APK
     */
    virtual void install(const std::string& serial, const std::string& apkPath, bool reinstall = false) = 0;

    // =========================================================================
    // 端口转发
    // =========================================================================

    /**
     * @brief 设置端口转发
     * @param serial 设备序列号
     * @param localPort 本地端口
     * @param remoteSocket 远程 Socket 名称
     */
    virtual VoidResult forward(const std::string& serial, uint16_t localPort, const std::string& remoteSocket) = 0;

    /**
     * @brief 设置反向代理
     */
    virtual VoidResult reverse(const std::string& serial, const std::string& remoteSocket, uint16_t localPort) = 0;

    /**
     * @brief 移除端口转发
     */
    virtual VoidResult forwardRemove(const std::string& serial, uint16_t localPort) = 0;

    /**
     * @brief 移除反向代理
     */
    virtual VoidResult reverseRemove(const std::string& serial, const std::string& remoteSocket) = 0;

    // =========================================================================
    // 连接管理
    // =========================================================================

    /**
     * @brief 无线连接设备
     * @param ip IP 地址
     * @param port 端口号
     */
    virtual void connect(const std::string& ip, uint16_t port = 5555) = 0;

    /**
     * @brief 断开无线连接
     */
    virtual void disconnect(const std::string& ip, uint16_t port = 5555) = 0;

    /**
     * @brief 在设备上启动 ADB 服务（tcpip 模式）
     */
    virtual void tcpip(const std::string& serial, uint16_t port = 5555) = 0;

    // =========================================================================
    // ADB 服务管理
    // =========================================================================

    /**
     * @brief 启动 ADB 服务
     */
    virtual void startServer() = 0;

    /**
     * @brief 停止 ADB 服务
     */
    virtual void killServer() = 0;

    /**
     * @brief 检查当前是否有命令在执行
     */
    virtual bool isRunning() const = 0;

    /**
     * @brief 终止当前执行的命令
     */
    virtual void kill() = 0;

    // =========================================================================
    // 输出获取
    // =========================================================================

    /**
     * @brief 获取最后一次命令的标准输出
     */
    virtual std::string stdOut() const = 0;

    /**
     * @brief 获取最后一次命令的标准错误
     */
    virtual std::string stdError() const = 0;

    // =========================================================================
    // 信号 / Signals
    // =========================================================================

    /**
     * @brief 命令执行完成信号
     */
    Signal<AdbExecResult> executionFinished;

    /**
     * @brief 设备列表变化信号
     */
    Signal<const std::vector<DeviceInfo>&> devicesChanged;

    /**
     * @brief 设备状态变化信号
     */
    Signal<const std::string&, DeviceState> deviceStateChanged;

    /**
     * @brief 进度更新信号（用于文件传输）
     */
    Signal<int, const std::string&> progressUpdated;

    /**
     * @brief 输出信号
     */
    Signal<const std::string&> outputReceived;

    /**
     * @brief 错误信号
     */
    Signal<const std::string&> errorReceived;
};

/**
 * @brief ADB 执行器工厂接口
 */
class IAdbExecutorFactory
{
public:
    virtual ~IAdbExecutorFactory() = default;

    /**
     * @brief 创建 ADB 执行器实例
     * @param parent 父对象
     * @return ADB 执行器实例
     */
    virtual std::unique_ptr<IAdbExecutor> createExecutor() = 0;
};

} // namespace qsc

#endif // IADBEXECUTOR_H
