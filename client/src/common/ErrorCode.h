#ifndef ERRORCODE_H
#define ERRORCODE_H

#include <string>

namespace qsc {

/**
 * @brief 统一错误码枚举 / Unified Error Code Enum
 *
 * 错误码分段规则 / Error code segment rules:
 * - 0: 成功 / Success
 * - 1-99: 通用错误 / General errors
 * - 100-199: ADB相关错误 / ADB-related errors
 * - 200-299: 连接相关错误 / Connection errors
 * - 300-399: 解码相关错误 / Decoding errors
 * - 400-499: 控制相关错误 / Control errors
 * - 500-599: 文件操作错误 / File operation errors
 * - 600-699: 配置相关错误 / Configuration errors
 */
enum class ErrorCode : int
{
    // 成功
    Success = 0,

    // 通用错误 (1-99)
    Unknown = 1,
    InvalidParameter = 2,
    NullPointer = 3,
    NotInitialized = 4,
    AlreadyInitialized = 5,
    Timeout = 6,
    Cancelled = 7,
    NotSupported = 8,
    OutOfMemory = 9,
    PermissionDenied = 10,

    // ADB相关错误 (100-199)
    AdbNotFound = 100,
    AdbStartFailed = 101,
    AdbConnectionFailed = 102,
    AdbDeviceNotFound = 103,
    AdbDeviceOffline = 104,
    AdbDeviceUnauthorized = 105,
    AdbPushFailed = 106,
    AdbReverseFailed = 107,
    AdbForwardFailed = 108,
    AdbShellFailed = 109,
    AdbInstallFailed = 110,

    // 连接相关错误 (200-299)
    ConnectionFailed = 200,
    ConnectionLost = 201,
    ConnectionRefused = 202,
    ConnectionTimeout = 203,
    SocketCreateFailed = 204,
    SocketBindFailed = 205,
    SocketListenFailed = 206,
    SocketAcceptFailed = 207,
    SocketSendFailed = 208,
    SocketReceiveFailed = 209,
    ServerStartFailed = 210,
    ServerPushFailed = 211,
    HandshakeFailed = 212,

    // 解码相关错误 (300-399)
    DecoderInitFailed = 300,
    DecoderOpenFailed = 301,
    DecoderNotOpen = 302,
    CodecNotFound = 303,
    CodecConfigFailed = 304,
    HardwareDecoderFailed = 305,
    HardwareDecoderFallback = 306,
    FrameDecodeFailed = 307,
    FrameConvertFailed = 308,
    BufferAllocFailed = 309,

    // 控制相关错误 (400-499)
    ControllerNotReady = 400,
    ControlMsgSerializeFailed = 401,
    ControlMsgSendFailed = 402,
    ControlMsgQueueFull = 403,
    InputConvertFailed = 404,
    KeyMapLoadFailed = 405,
    KeyMapParseFailed = 406,
    ScriptEvalFailed = 407,

    // 文件操作错误 (500-599)
    FileNotFound = 500,
    FileOpenFailed = 501,
    FileReadFailed = 502,
    FileWriteFailed = 503,
    FileDeleteFailed = 504,
    DirectoryCreateFailed = 505,
    PathInvalid = 506,

    // 配置相关错误 (600-699)
    ConfigLoadFailed = 600,
    ConfigSaveFailed = 601,
    ConfigParseFailed = 602,
    ConfigInvalid = 603,
    ConfigKeyNotFound = 604,
};

/**
 * @brief 获取错误码的描述信息
 */
inline std::string errorCodeToString(ErrorCode code)
{
    switch (code) {
    case ErrorCode::Success: return "操作成功";
    case ErrorCode::Unknown: return "未知错误";
    case ErrorCode::InvalidParameter: return "无效参数";
    case ErrorCode::NullPointer: return "空指针";
    case ErrorCode::NotInitialized: return "未初始化";
    case ErrorCode::AlreadyInitialized: return "已初始化";
    case ErrorCode::Timeout: return "操作超时";
    case ErrorCode::Cancelled: return "操作已取消";
    case ErrorCode::NotSupported: return "不支持的操作";
    case ErrorCode::OutOfMemory: return "内存不足";
    case ErrorCode::PermissionDenied: return "权限不足";

    case ErrorCode::AdbNotFound: return "找不到ADB程序";
    case ErrorCode::AdbStartFailed: return "ADB启动失败";
    case ErrorCode::AdbConnectionFailed: return "ADB连接失败";
    case ErrorCode::AdbDeviceNotFound: return "找不到设备";
    case ErrorCode::AdbDeviceOffline: return "设备离线";
    case ErrorCode::AdbDeviceUnauthorized: return "设备未授权，请在手机上允许USB调试";
    case ErrorCode::AdbPushFailed: return "文件推送失败";
    case ErrorCode::AdbReverseFailed: return "ADB反向代理失败";
    case ErrorCode::AdbForwardFailed: return "ADB端口转发失败";
    case ErrorCode::AdbShellFailed: return "ADB命令执行失败";
    case ErrorCode::AdbInstallFailed: return "应用安装失败";

    case ErrorCode::ConnectionFailed: return "连接失败";
    case ErrorCode::ConnectionLost: return "连接丢失";
    case ErrorCode::ConnectionRefused: return "连接被拒绝";
    case ErrorCode::ConnectionTimeout: return "连接超时";
    case ErrorCode::SocketCreateFailed: return "Socket创建失败";
    case ErrorCode::SocketBindFailed: return "Socket绑定失败";
    case ErrorCode::SocketListenFailed: return "Socket监听失败";
    case ErrorCode::SocketAcceptFailed: return "Socket接受连接失败";
    case ErrorCode::SocketSendFailed: return "发送数据失败";
    case ErrorCode::SocketReceiveFailed: return "接收数据失败";
    case ErrorCode::ServerStartFailed: return "服务器启动失败";
    case ErrorCode::ServerPushFailed: return "服务器推送失败";
    case ErrorCode::HandshakeFailed: return "握手失败";

    case ErrorCode::DecoderInitFailed: return "解码器初始化失败";
    case ErrorCode::DecoderOpenFailed: return "解码器打开失败";
    case ErrorCode::DecoderNotOpen: return "解码器未打开";
    case ErrorCode::CodecNotFound: return "找不到解码器";
    case ErrorCode::CodecConfigFailed: return "解码器配置失败";
    case ErrorCode::HardwareDecoderFailed: return "硬件解码器失败";
    case ErrorCode::HardwareDecoderFallback: return "硬件解码不可用，已切换到软件解码";
    case ErrorCode::FrameDecodeFailed: return "帧解码失败";
    case ErrorCode::FrameConvertFailed: return "帧转换失败";
    case ErrorCode::BufferAllocFailed: return "缓冲区分配失败";

    case ErrorCode::ControllerNotReady: return "控制器未就绪";
    case ErrorCode::ControlMsgSerializeFailed: return "控制消息序列化失败";
    case ErrorCode::ControlMsgSendFailed: return "控制消息发送失败";
    case ErrorCode::ControlMsgQueueFull: return "控制消息队列已满";
    case ErrorCode::InputConvertFailed: return "输入转换失败";
    case ErrorCode::KeyMapLoadFailed: return "按键映射加载失败";
    case ErrorCode::KeyMapParseFailed: return "按键映射解析失败";
    case ErrorCode::ScriptEvalFailed: return "脚本执行失败";

    case ErrorCode::FileNotFound: return "文件不存在";
    case ErrorCode::FileOpenFailed: return "文件打开失败";
    case ErrorCode::FileReadFailed: return "文件读取失败";
    case ErrorCode::FileWriteFailed: return "文件写入失败";
    case ErrorCode::FileDeleteFailed: return "文件删除失败";
    case ErrorCode::DirectoryCreateFailed: return "目录创建失败";
    case ErrorCode::PathInvalid: return "路径无效";

    case ErrorCode::ConfigLoadFailed: return "配置加载失败";
    case ErrorCode::ConfigSaveFailed: return "配置保存失败";
    case ErrorCode::ConfigParseFailed: return "配置解析失败";
    case ErrorCode::ConfigInvalid: return "配置无效";
    case ErrorCode::ConfigKeyNotFound: return "配置项不存在";

    default: return std::string("未定义错误: ") + std::to_string(static_cast<int>(code));
    }
}

/**
 * @brief 获取用户友好的错误提示和解决建议
 */
inline std::string errorCodeToUserHint(ErrorCode code)
{
    switch (code) {
    case ErrorCode::AdbDeviceNotFound:
        return "请确保设备已连接并开启USB调试模式";
    case ErrorCode::AdbDeviceUnauthorized:
        return "请在手机上点击\"允许USB调试\"，如果没有弹窗请重新插拔数据线";
    case ErrorCode::AdbDeviceOffline:
        return "请尝试重新插拔数据线或重启ADB服务";
    case ErrorCode::ConnectionTimeout:
        return "网络连接超时，请检查网络状况或尝试使用USB连接";
    case ErrorCode::HardwareDecoderFallback:
        return "您的显卡可能不支持硬件解码，已自动切换到软件解码，可能会增加CPU占用";
    case ErrorCode::ControlMsgQueueFull:
        return "控制消息积压过多，可能是网络不稳定导致";
    default:
        return std::string();
    }
}

/**
 * @brief 结果类模板，用于返回操作结果
 *
 * 使用示例：
 *   Result<int> result = someFunction();
 *   if (result) {
 *       int value = result.value();
 *   } else {
 *       qWarning() << result.errorMessage();
 *   }
 */
template<typename T>
class Result
{
public:
    // 成功构造
    static Result success(const T& value) {
        Result r;
        r.m_success = true;
        r.m_value = value;
        r.m_code = ErrorCode::Success;
        return r;
    }

    static Result success(T&& value) {
        Result r;
        r.m_success = true;
        r.m_value = std::move(value);
        r.m_code = ErrorCode::Success;
        return r;
    }

    // 失败构造
    static Result failure(ErrorCode code, const std::string& detail = std::string()) {
        Result r;
        r.m_success = false;
        r.m_code = code;
        r.m_detail = detail;
        return r;
    }

    bool isSuccess() const { return m_success; }
    bool isFailure() const { return !m_success; }
    explicit operator bool() const { return m_success; }

    const T& value() const { return m_value; }
    T& value() { return m_value; }
    T valueOr(const T& defaultValue) const { return m_success ? m_value : defaultValue; }

    ErrorCode errorCode() const { return m_code; }
    std::string errorMessage() const { return errorCodeToString(m_code); }
    std::string errorDetail() const { return m_detail; }
    std::string fullErrorMessage() const {
        if (m_detail.empty()) return errorMessage();
        return errorMessage() + ": " + m_detail;
    }
    std::string userHint() const { return errorCodeToUserHint(m_code); }

private:
    Result() = default;
    bool m_success = false;
    T m_value{};
    ErrorCode m_code = ErrorCode::Unknown;
    std::string m_detail;
};

/**
 * @brief void 特化版本
 */
template<>
class Result<void>
{
public:
    static Result success() {
        Result r;
        r.m_success = true;
        r.m_code = ErrorCode::Success;
        return r;
    }

    static Result failure(ErrorCode code, const std::string& detail = std::string()) {
        Result r;
        r.m_success = false;
        r.m_code = code;
        r.m_detail = detail;
        return r;
    }

    bool isSuccess() const { return m_success; }
    bool isFailure() const { return !m_success; }
    explicit operator bool() const { return m_success; }

    ErrorCode errorCode() const { return m_code; }
    std::string errorMessage() const { return errorCodeToString(m_code); }
    std::string errorDetail() const { return m_detail; }
    std::string fullErrorMessage() const {
        if (m_detail.empty()) return errorMessage();
        return errorMessage() + ": " + m_detail;
    }
    std::string userHint() const { return errorCodeToUserHint(m_code); }

private:
    Result() = default;
    bool m_success = false;
    ErrorCode m_code = ErrorCode::Unknown;
    std::string m_detail;
};

// 便捷类型别名
using VoidResult = Result<void>;

} // namespace qsc

#endif // ERRORCODE_H
