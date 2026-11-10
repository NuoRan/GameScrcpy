# 识图测试数据

`gs_vision_tests` 的帧与模板。全部是 `tests/data/video` 三段录制数据（Android 模拟器的设置页，本项目自己的服务端录制，没有版权问题）解码出的亮度平面，存为二进制 PGM（`P5`，宽 高，最大值 255，之后逐行像素，无注释）。

## 怎么来的

由 `tools/vision_fixtures.cpp`（目标 `gs_vision_fixtures`）用项目自己的解码器（`gs_media` 的 `VideoDecoder`，软件解码）生成，帧号与裁剪位置写在该文件的 `kFrames`、`kCrops` 两张表里：

```powershell
.\build\core-only\bin\gs_vision_fixtures.exe write tests\data\video tests\data\vision
```

CTest 的 `gs_vision_fixtures_current` 用 `check` 子命令重新解码并逐字节比较，录制数据或解码器变了就会失败。挑帧时可用 `dump <录制文件> <目录>` 把一段录制的每一帧都写成 PGM。

## 帧

| 文件 | 来源（录制，帧号） | 尺寸 | 画面 |
|---|---|---|---|
| `frame_h265_top.pgm` | `emulator_h265.media`，0 | 288×640 | 设置页顶部，模板都从这里裁 |
| `frame_av1_top.pgm` | `emulator_av1.media`，0 | 288×640 | 同一页面，另一编码，整页下移 6 像素 |
| `frame_h264_scrolled.pgm` | `emulator_h264.media`，10 | 288×640 | 向下滚动约 240 像素 |
| `frame_av1_bottom.pgm` | `emulator_av1.media`，40 | 288×640 | 滚到 Location 一行 |
| `frame_av1_landscape.pgm` | `emulator_av1.media`，70 | 640×288 | 横屏，左侧黑边，底部有手势条压住一行 |
| `frame_h264_rotating.pgm` | `emulator_h264.media`，48 | 640×288 | 旋转动画中的一帧，界面斜了约 20°：内容都在，但平移匹配不应命中 |

## 模板

| 文件 | 裁自 | 位置（x, y, 宽, 高） |
|---|---|---|
| `template_search_bar.pgm` | `frame_h265_top` | 20, 130, 136, 30 |
| `template_notifications_row.pgm` | `frame_h265_top` | 16, 372, 200, 36 |
| `template_battery_row.pgm` | `frame_h265_top` | 18, 434, 104, 34 |
| `template_profile.pgm` | `frame_h265_top` | 236, 38, 38, 38 |
| `template_network_row.pgm` | `frame_h265_top` | 16, 186, 168, 40 |
| `template_bell.pgm` | `frame_h265_top` | 21, 378, 20, 22 |
| `template_wallpaper_row.pgm` | `frame_av1_bottom` | 16, 420, 180, 40 |
| `template_location_row.pgm` | `frame_av1_bottom` | 16, 600, 120, 36 |

## 期望结果从哪来

`FixtureTests.cpp` 的 `kSightings` 列出 8 个模板在 6 帧里的 48 种组合：每种组合的最佳位置与分数是另用 NumPy（FFT 求互相关、积分图求窗口方差，与本模块无关的实现）对整帧穷举算出的，并逐一看过截图。分数不低于阈值（0.8；横屏里被手势条压住的通知行是 0.78，用 0.75）的 26 种算"在"，其余 22 种算"不在"。测试要求：在的，金字塔查找给出同一位置、分数与 NumPy 相差小于 0.0005、`findAll` 只给一个结果；不在的，`findBest` 与 `findAll` 都为空，穷举查找的最高分位置与分数也与 NumPy 一致。

`screen1080()`（`TestImages.h`）把这 6 帧拼成 1920×1080 的画面供基准测试与"每个对象一个结果"的测试使用：3 列 × 4 行、每格 640×288，前 6 格依次为两张竖屏帧各取 288 行并排（右侧 64 列取自横屏帧）、横屏帧、旋转帧，后 6 格为前 6 格上下翻转，最后一行只露出 216 行。
