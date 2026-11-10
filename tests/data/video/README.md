# 视频通道录制数据

`gs_media_tests` 用来在没有设备的情况下测试解码。每个文件是一条视频通道收到的全部媒体包，按到达顺序原样拼接（每包 20 字节包头 + 负载，SPEC 3.2）。服务端是本项目自己的，画面为 Android 模拟器的设置页，没有版权问题。

| 文件 | 编码 | 编码会话 | 帧数（关键帧） | 录制命令 |
|---|---|---|---|---|
| `emulator_h264.media` | H.264 | 288×640 → 旋转后 640×288 | 71（2） | `python tools/server_smoke.py --serial emulator-5556 --video 3 --codec h264 --max-size 640 --max-fps 30 --bitrate 300000 --rotate --record tests/data/video/emulator_h264.media` |
| `emulator_h265.media` | H.265 | 288×640 | 52（1） | 同上，`--video 3 --codec h265`，不旋转 |
| `emulator_av1.media` | AV1 | 288×640 → 旋转后 640×288 | 80（2） | 同上，`--codec av1 --rotate` |

2026-10-01 录自 `emulator-5556`（Android 15，软件编码器 `c2.android.*.encoder`）。H.265 不带旋转：模拟器在旋转后重建 H.265 编码器较慢，而录制脚本的滑动坐标按竖屏写，横屏下画面不再变化、编码器不出帧，录不到第二个会话。

2026-10-02 包头由 16 字节扩为 20 字节（ADR-0048 增加 `encodeDelayUs`）：三个文件按原数据转换，在每个包头的 `ptsUs` 后插入 4 个 0 字节，所以录制数据里的编码耗时都是 0。
