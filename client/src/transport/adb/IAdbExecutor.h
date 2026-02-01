#ifndef IADBEXECUTOR_H
#define IADBEXECUTOR_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>

#include "ErrorCode.h"

namespace qsc {

/**
 * @brief ADB 执行结果
 */
enum class AdbExecResult
{
    Success,            // 执行成功
    StartFailed,        // 启动失败
    ExecFailed,         // 执行失败
    Timeout,            // 超时
    Cancelled           // 已取消
};

/**
 * @brief 设备状态
 */
enum class DeviceState
{
    Unknown,            // 未知
    Online,             // 在线
    Offline,            // 离线
    Unauthorized,       // 未授权
    Bootloader,         // Bootloader 模式
    Recovery,           // 恢复模式
    Sideload,           // Sideload 模式
    Disconnected        // 已断开
};

/**
 * @brief 设备信息
 */
struct DeviceInfo
{
    QString serial;             // 设备序列号
    DeviceState state;          // 设备状态
    QString model;              // 设备型号
    QString product;            // 产品名
    QString device;             // 设备名
    QString transportId;        // 传输 ID
    bool isWireless = false;    // 是否为无线连接
};

/**
 * @brief ADB 命令执行回调
 * @param result 执行结果
 * @param output 标准输出
 * @param error 标准错误
 */
using AdbCallback = std::function<void(AdbExecResult result, const QString& output, const QString& error)>;

/**
 * @brief ADB 执行器抽象接口
 *
 * 定义 ADB 操作的标准接口，封装所有与 ADB 相关的操作。
 * 支持同步和异步两种执行模式。
 */
class IAdbExecutor : public QObject
{
    Q_OBJECT

public:
    explicit IAdbExecutor(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~IAdbExecutor() = default;

    // =========================================================================
    // 基础操作
    // =========================================================================

    /**
     * @brief 设置 ADB 可执行文件路径
     */
    virtual void setAdbPath(const QString& path) = 0;

    /**
     * @brief 获取 ADB 路径
     */
    virtual QString adbPath() const = 0;

    /**
     * @brief 检查 ADB 是否可用
     */
    virtual bool isAdbAvailable() const = 0;

    /**
     * @brief 获取 ADB 版本
     */
    virtual QString adbVersion() const = 0;

    // =========================================================================
    // 设备管理
    // =========================================================================

    /**
     * @brief 获取设备列表
     * @return 设备信息列表
     */
    virtual QList<DeviceInfo> devices() = 0;

    /**
     * @brief 等待设备连接
     * @param serial 设备序列号（空表示任意设备）
     * @param timeoutMs 超时时间
     * @return 操作结果
     */
    virtual VoidResult waitForDevice(const QString& serial = QString(), int timeoutMs = 30000) = 0;

    /**
     * @brief 获取设备状态
     */
    virtual DeviceState deviceState(const QString& serial) = 0;

    // =========================================================================
    // 命令执行
    // =========================================================================

    /**
     * @brief 异步执行 ADB 命令
     * @param serial 设备序列号
     * @param args 命令参数
     */
    virtual void executeAsync(const QString& serial, const QStringList& args) = 0;

    /**
     * @brief 同步执行 ADB 命令
     * @param serial 设备序列号
     * @param args 命令参数
     * @param timeoutMs 超时时间
     * @return 命令输出
     */
    virtual Result<QString> executeSync(
        const QString& serial,
        const QStringList& args,
        int timeoutMs = 10000
    ) = 0;

    /**
     * @brief 执行 shell 命令
     */
    virtual void shell(const QString& serial, const QString& command) = 0;

    /**
     * @brief 同步执行 shell 命令
     */
    virtual Result<QString> shellSync(
        const QString& serial,
        const QString& command,
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
    virtual void push(const QString& serial, const QString& localPath, const QString& remotePath) = 0;

    /**
     * @brief 从设备拉取文件
     */
    virtual void pull(const QString& serial, const QString& remotePath, const QString& localPath) = 0;

    /**
     * @brief 安装 APK
     */
    virtual void install(const QString& serial, const QString& apkPath, bool reinstall = false) = 0;

    // =========================================================================
    // 端口转发
    // =========================================================================

    /**
     * @brief 设置端口转发
     * @param serial 设备序列号
     * @param localPort 本地端口
     * @param remoteSocket 远程 Socket 名称
     */
    virtual VoidResult forward(const QString& serial, quint16 localPort, const QString& remoteSocket) = 0;

    /**
     * @brief 设置反向代理
     */
    virtual VoidResult reverse(const QString& serial, const QString& remoteSocket, quint16 localPort) = 0;

    /**
     * @brief 移除端口转发
     */
    virtual VoidResult forwardRemove(const QString& serial, quint16 localPort) = 0;

    /**
     * @brief 移除反向代理
     */
    virtual VoidResult reverseRemove(const QString& serial, const QString& remoteSocket) = 0;

    // =========================================================================
    // 连接管理
    // =========================================================================

    /**
     * @brief 无线连接设备
     * @param ip IP 地址
     * @param port 端口号
     */
    virtual void connect(const QString& ip, quint16 port = 5555) = 0;

    /**
     * @brief 断开无线连接
     */
    virtual void disconnect(const QString& ip, quint16 port = 5555) = 0;

    /**
     * @brief 在设备上启动 ADB 服务（tcpip 模式）
     */
    virtual void tcpip(const QString& serial, quint16 port = 5555) = 0;

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
    virtual QString stdOut() const = 0;

    /**
     * @brief 获取最后一次命令的标准错误
     */
    virtual QString stdError() const = 0;

signals:
    /**
     * @brief 命令执行完成信号
     */
    void executionFinished(AdbExecResult result);

    /**
     * @brief 设备列表变化信号
     */
    void devicesChanged(const QList<DeviceInfo>& devices);

    /**
     * @brief 设备状态变化信号
     */
    void deviceStateChanged(const QString& serial, DeviceState state);

    /**
     * @brief 进度更新信号（用于文件传输）
     */
    void progressUpdated(int percent, const QString& message);

    /**
     * @brief 输出信号
     */
    void outputReceived(const QString& output);

    /**
     * @brief 错误信号
     */
    void errorReceived(const QString& error);
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
    virtual std::unique_ptr<IAdbExecutor> createExecutor(QObject* parent = nullptr) = 0;
};

} // namespace qsc

#endif // IADBEXECUTOR_H
