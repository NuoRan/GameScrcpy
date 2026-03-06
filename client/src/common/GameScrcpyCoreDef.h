#pragma once
#include <string>
#include <cstdint>

namespace qsc {

/**
 * @brief 设备连接参数 / Device Connection Parameters
 *
 * 定义与 Android 设备建立 scrcpy 会话所需的全部参数。
 * Defines all parameters needed to establish a scrcpy session with an Android device.
 */
struct DeviceParams {
    // 必需参数 / Required parameters
    std::string serial;              // 设备序列号 / Device serial number
    std::string serverLocalPath;     // 本地 server jar 路径 / Local server jar path

    // 可选参数 / Optional parameters
    std::string serverRemotePath = "/data/local/tmp/scrcpy-server.jar";    // 远端设备 server 路径 / Remote device server path
    uint16_t maxSize = 720;            // 视频分辨率 / Video resolution
    uint32_t bitRate = 2000000;        // 视频比特率 / Video bitrate
    uint32_t maxFps = 0;               // 视频最大帧率 (0=不限制) / Max FPS (0=unlimited)
    int captureOrientationLock = 0;   // 采集方向锁定: 0=不锁 1=锁定指定 2=锁定原始 / Capture orientation lock
    int captureOrientation = 0;       // 采集方向 / Capture orientation (0/90/180/270)
    bool stayAwake = false;           // 保持唤醒 / Keep screen awake
    std::string serverVersion = "3.3.4";  // server 版本 / Server version
    std::string logLevel = "debug";     // 日志级别 / Log level
    // 视频编解码器 / Video codec: "h264"
    std::string videoCodec = "h264";
    // 编码选项 / Codec options ("" = default)
    std::string codecOptions;
    // 指定编码器名称 / Codec name ("" = default)
    std::string codecName;
    uint32_t scid = static_cast<uint32_t>(-1); // 随机数，作为 localsocket 名字后缀 / Random suffix for localsocket name

    // KCP 视频/控制传输端口 (UDP) - WiFi 模式 / KCP video/control port (UDP) - WiFi mode
    uint16_t kcpPort = 27185;

    // TCP 本地端口 - USB 模式 / TCP local port - USB mode
    uint16_t localPort = 27183;
    uint16_t localPortAudio = 27186;   // TCP 音频端口 / TCP audio port
    uint16_t localPortCtrl = 27184;    // TCP 控制端口 / TCP control port
    bool useReverse = true;           // TCP 模式: 优先 adb reverse / TCP mode: prefer adb reverse

    std::string recordPath;          // 视频保存路径 / Video save path
    std::string recordFileFormat = "mp4"; // 视频保存格式 / Video format (mp4/mkv)
    bool recordFile = false;          // 录制到文件 / Record to file

    bool closeScreen = false;         // 启动时自动息屏 / Turn off screen on start
    bool display = true;              // 是否显示画面 / Show display (or background-only recording)
    bool renderExpiredFrames = false; // 是否渲染延迟视频帧 / Render expired video frames
    std::string gameScript;          // 游戏映射脚本 / Game mapping script
};

}
