#pragma once
#include <QString>

namespace qsc {

struct DeviceParams {
    // necessary
    QString serial = "";              // 设备序列号
    QString serverLocalPath = "";     // 本地安卓server路径

    // optional
    QString serverRemotePath = "/data/local/tmp/scrcpy-server.jar";    // 要推送到远端设备的server路径
    quint16 maxSize = 720;            // 视频分辨率
    quint32 bitRate = 2000000;        // 视频比特率
    quint32 maxFps = 0;               // 视频最大帧率
    int captureOrientationLock = 0;   // 是否锁定采集方向 0不锁定 1锁定指定方向 2锁定原始方向
    int captureOrientation = 0;       // 采集方向 0 90 180 270
    bool stayAwake = false;           // 是否保持唤醒
    QString serverVersion = "3.3.4";  // server版本
    QString logLevel = "debug";     // log级别 verbose/debug/info/warn/error
    // 编码选项 ""表示默认
    // 例如 CodecOptions="profile=1,level=2"
    // 更多编码选项参考 https://d.android.com/reference/android/media/MediaFormat
    QString codecOptions = "";
    // 指定编码器名称(必须是H.264编码器)，""表示默认
    // 例如 CodecName="OMX.qcom.video.encoder.avc"
    QString codecName = "";
    quint32 scid = -1; // 随机数，作为localsocket名字后缀，方便同时连接同一个设备多次

    // KCP 视频/控制传输端口 (UDP) - WiFi 模式
    // 使用 KCP/UDP 传输，延迟比 TCP 低 30-40%
    // 视频端口 = kcpPort, 控制端口 = kcpPort + 1
    quint16 kcpPort = 27185;

    // TCP 本地端口 - USB 模式
    // 通过 adb forward/reverse 转发到设备
    quint16 localPort = 27183;
    quint16 localPortCtrl = 27184;    // TCP 控制端口
    bool useReverse = true;           // TCP 模式: true=优先使用 adb reverse, 失败后回退到 adb forward

    QString recordPath = "";          // 视频保存路径
    QString recordFileFormat = "mp4"; // 视频保存格式 mp4/mkv
    bool recordFile = false;          // 录制到文件

    QString pushFilePath = "/sdcard/"; // 推送到安卓设备的文件保存路径（必须以/结尾）

    bool closeScreen = false;         // 启动时自动息屏
    bool display = true;              // 是否显示画面（或者仅仅后台录制）
    bool renderExpiredFrames = false; // 是否渲染延迟视频帧
    QString gameScript = "";          // 游戏映射脚本
};

}
