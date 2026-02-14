#pragma once
#include <QString>

namespace qsc {

/**
 * @brief 设备连接参数 / Device Connection Parameters
 *
 * 定义与 Android 设备建立 scrcpy 会话所需的全部参数。
 * Defines all parameters needed to establish a scrcpy session with an Android device.
 */
struct DeviceParams {
    // 必需参数 / Required parameters
    QString serial = "";              // 设备序列号 / Device serial number
    QString serverLocalPath = "";     // 本地 server jar 路径 / Local server jar path

    // 可选参数 / Optional parameters
    QString serverRemotePath = "/data/local/tmp/scrcpy-server.jar";    // 远端设备 server 路径 / Remote device server path
    quint16 maxSize = 720;            // 视频分辨率 / Video resolution
    quint32 bitRate = 2000000;        // 视频比特率 / Video bitrate
    quint32 maxFps = 0;               // 视频最大帧率 (0=不限制) / Max FPS (0=unlimited)
    int captureOrientationLock = 0;   // 采集方向锁定: 0=不锁 1=锁定指定 2=锁定原始 / Capture orientation lock
    int captureOrientation = 0;       // 采集方向 / Capture orientation (0/90/180/270)
    bool stayAwake = false;           // 保持唤醒 / Keep screen awake
    QString serverVersion = "3.3.4";  // server 版本 / Server version
    QString logLevel = "debug";     // 日志级别 / Log level
    // 视频编解码器 / Video codec: "h264"
    QString videoCodec = "h264";
    // 编码选项 / Codec options ("" = default)
    QString codecOptions = "";
    // 指定编码器名称 / Codec name ("" = default)
    QString codecName = "";
    quint32 scid = -1; // 随机数，作为 localsocket 名字后缀 / Random suffix for localsocket name

    // KCP 视频/控制传输端口 (UDP) - WiFi 模式 / KCP video/control port (UDP) - WiFi mode
    quint16 kcpPort = 27185;

    // TCP 本地端口 - USB 模式 / TCP local port - USB mode
    quint16 localPort = 27183;
    quint16 localPortCtrl = 27184;    // TCP 控制端口 / TCP control port
    bool useReverse = true;           // TCP 模式: 优先 adb reverse / TCP mode: prefer adb reverse

    QString recordPath = "";          // 视频保存路径 / Video save path
    QString recordFileFormat = "mp4"; // 视频保存格式 / Video format (mp4/mkv)
    bool recordFile = false;          // 录制到文件 / Record to file

    bool closeScreen = false;         // 启动时自动息屏 / Turn off screen on start
    bool display = true;              // 是否显示画面 / Show display (or background-only recording)
    bool renderExpiredFrames = false; // 是否渲染延迟视频帧 / Render expired video frames
    QString gameScript = "";          // 游戏映射脚本 / Game mapping script
};

}
