# GameScrcpy 六大任务执行方案

> 创建日期: 2026-03-03
> 状态: 执行中

---

## 目录

1. [任务1: 设置弹窗 UI 调整](#任务1-设置弹窗-ui-调整)
2. [任务2: 视频传输状态持久化](#任务2-视频传输状态持久化)
3. [任务3: 修复关闭窗口后重连闪退](#任务3-修复关闭窗口后重连闪退)
4. [任务4: 音频与剪切板功能](#任务4-音频与剪切板功能)
5. [任务5: 通道开关设置](#任务5-通道开关设置)
6. [任务6: 全部视频编码格式支持](#任务6-全部视频编码格式支持)

---

## 任务1: 设置弹窗 UI 调整

**需求**: 码率去掉 Mbps 后缀，码率/帧率/分辨率三个参数写到一行平均布局。

**当前状态**: `VideoSettingsPopup.cpp` 使用 2×2 QGridLayout，第一行放码率+帧率，第二行放分辨率。码率后面有 `"Mbps"` label。

### 步骤

- [ ] **1.1** 修改 `VideoSettingsPopup.cpp::setupUI()`
  - 删除 `brUnit` (Mbps label)
  - 将 Grid 布局改为单行 QHBoxLayout，三个参数等宽 (`stretch(1,1,1)`)
  - 码率 label + QLineEdit | 帧率 label + QComboBox | 分辨率 label + QComboBox
  - 修改码率 placeholder 提示为 `"Mbps"` (setPlaceholderText)

**涉及文件**:
- `client/src/ui/components/VideoSettingsPopup.cpp` — setupUI() 视频参数区域

**预估**: ★☆☆ 简单

---

## 任务2: 视频传输状态持久化

**需求**: 关闭设置弹窗再打开后，视频传输(暂停/播放)和息屏状态不应重置。

**当前状态**: `syncFromConfig()` 从 Config/ConfigCenter 读取参数，但 streaming 和 screenOff 是运行时状态，没有持久化。

### 步骤

- [ ] **2.1** 在 `ConfigCenter.h/.cpp` 添加两个持久化字段:
  - `videoStreaming` (默认 `true`) — `user/videoStreaming`
  - `screenOff` (默认 `false`) — `user/screenOff`
  - 各提供 get/set 方法
- [ ] **2.2** 修改 `VideoSettingsPopup.cpp`:
  - `flushAndSaveVideoParams()` 或 toggle 回调中保存 streaming/screenOff 到 ConfigCenter
  - `syncFromConfig()` 中从 ConfigCenter 读取 streaming/screenOff 状态

**涉及文件**:
- `client/src/common/ConfigCenter.h` — 添加 get/set
- `client/src/common/ConfigCenter.cpp` — 注册默认值 + 实现
- `client/src/ui/components/VideoSettingsPopup.cpp` — sync/save 逻辑

**预估**: ★☆☆ 简单

---

## 任务3: 修复关闭窗口后重连闪退

**需求**: USB 模式下关闭视频窗口后重新连接同一设备会闪退。

**根因分析**:

1. `VideoForm::closeEvent()` → `disconnectDevice(serial)` → `DeviceController::stop()` + `controller->deleteLater()`
2. `server->stop()` 异步触发 adb 进程终止 → `onServerStop()` → `emit disconnected(serial)` → `onDeviceDisconnected()` → `removeDevice(serial)` → `m_devices.take(serial)` 返回 **nullptr** (已被 step1 take走)
3. `removeDevice()` 对 nullptr controller 调用 `stop()` + `deleteLater()` → **double-free/空指针崩溃**
4. 即使不崩溃，旧 controller 的 `deleteLater()` 仍在事件队列中。立即重连创建新 controller，但旧回调(server crashed signal)可能触发到已析构对象。

### 步骤

- [ ] **3.1** 修复 `removeDevice()` — 添加 nullptr 保护:
  ```cpp
  auto* controller = m_devices.take(serial);
  if (!controller) return;  // 已被其他路径清理
  ```
- [ ] **3.2** 修复 `onDeviceDisconnected()` — 避免重复清理:
  - 检查 `m_devices.contains(serial)` 再调用 `removeDevice`
- [ ] **3.3** 修复 `disconnectDevice()` — 断开信号后再 stop:
  - 在 `stop()` 之前先 `disconnect(controller, ...)` 断开所有信号连接，防止 `onServerStop` 回调
  ```cpp
  auto* controller = m_devices.take(serial);
  if (!controller) return;
  controller->disconnect();  // 断开所有信号
  controller->stop();
  controller->deleteLater();
  ```
- [ ] **3.4** 增加连接时的旧实例保护:
  - `connectDevice()` 中如果 `m_devices` 已有该 serial 的 controller，先清理旧实例

**涉及文件**:
- `client/src/transport/server/devicemanage.cpp` — disconnectDevice, removeDevice, onDeviceDisconnected, connectDevice

**预估**: ★★☆ 中等（需处理竞态）

---

## 任务4: 音频与剪切板功能

**需求**: 参考 scrcpy 上游添加音频流和剪切板同步功能。

**当前状态**:
- 服务端 `audio` 参数被 ignored，无 AudioCodec/AudioEncoder/AudioCapture 代码
- 剪切板: 服务端有 `ClipboardManager` wrapper 和 `clipboardAutosync` 选项，但客户端未传参、未处理
- AuxChannel 注释已预留音频/剪切板扩展

### 架构设计

```
┌───────────────────────────────────┐
│  Android 设备 (Server)            │
│ ┌──────────────┐ ┌──────────────┐ │
│ │ AudioCapture │ │ ClipboardMgr │ │
│ │ (AAC/OPUS)   │ │ (getText/set)│ │
│ └──────┬───────┘ └──────┬───────┘ │
│        │ encoded         │ text    │
│        └───┬─────────────┘         │
│      Aux Channel (TCP/UDP)         │
└────────┬──────────────────────────┘
         │
┌────────▼──────────────────────────┐
│  PC 客户端                        │
│ ┌──────────────┐ ┌──────────────┐ │
│ │ AudioDecoder │ │ ClipboardSync│ │
│ │ (FFmpeg)     │ │ (QClipboard) │ │
│ │ → QAudioSink │ │ ← → 系统板   │ │
│ └──────────────┘ └──────────────┘ │
│      AuxChannelClient             │
└───────────────────────────────────┘
```

### 步骤

#### 4A. 服务端音频

- [ ] **4A.1** 创建 `server/src/main/java/.../audio/AudioCodec.java`
  - 枚举: `OPUS`, `AAC`, `RAW`
  - 对应 MIME_TYPE: `audio/opus`, `audio/mp4a-latm`, `audio/raw`
- [ ] **4A.2** 创建 `server/src/main/java/.../audio/AudioCapture.java`
  - 使用 `AudioRecord` (API 29+) 或 `AudioPlaybackCapture` (API 29+)
  - 配置: 48kHz, stereo, 16bit PCM
- [ ] **4A.3** 创建 `server/src/main/java/.../audio/AudioEncoder.java`
  - 使用 `MediaCodec` 编码 OPUS/AAC
  - 输出 encoded packets
- [ ] **4A.4** 创建 `server/src/main/java/.../audio/AudioStream.java`
  - 组合 AudioCapture + AudioEncoder
  - 通过 AuxChannel 发送音频数据 (msgType=0x03)
- [ ] **4A.5** 修改 `Options.java` — 启用 `audio` 参数解析
- [ ] **4A.6** 修改 `ScrcpySession.java` — 启动 AudioStream

#### 4B. 客户端音频

- [ ] **4B.1** 创建 `client/src/decoder/AudioDecoder.h/.cpp`
  - FFmpeg 解码 OPUS/AAC → PCM
  - 使用 `avcodec_find_decoder(AV_CODEC_ID_OPUS/AAC)`
- [ ] **4B.2** 创建 `client/src/render/AudioPlayer.h/.cpp`
  - 使用 Qt Multimedia `QAudioSink` 播放 PCM
  - 音频缓冲队列，抗抖动
- [ ] **4B.3** 修改 `AuxChannelClient` — 添加音频数据接收解析 (msgType=0x03)
- [ ] **4B.4** 修改 `AuxMessage.java` / `AuxChannelClient.h` — 添加 MSG_TYPE_AUDIO_DATA
- [ ] **4B.5** 修改 `DeviceSession` — 集成 AudioDecoder + AudioPlayer
- [ ] **4B.6** 修改 KCP/TCP 启动参数 — `audio=true/false` 根据设置

#### 4C. 剪切板

- [ ] **4C.1** 服务端: 修改 `Controller.java` — 启用 clipboard autosync
  - 监听设备剪切板变化 → 通过 AuxChannel 发送 (msgType=0x04)
- [ ] **4C.2** 服务端: 在 AuxChannel 接收端处理 PC→设备 剪切板 (msgType=0x05)
- [ ] **4C.3** 客户端: 创建 `client/src/control/ClipboardSync.h/.cpp`
  - 监听 `QClipboard::dataChanged` → 通过 AuxChannel 发送到设备
  - 接收设备剪切板 → 设置 `QClipboard::setText`
- [ ] **4C.4** 修改 `AuxChannelClient` — 添加 clipboard 消息类型
- [ ] **4C.5** 修改 DeviceSession — 集成 ClipboardSync
- [ ] **4C.6** 修改 KCP/TCP 启动参数 — `clipboard_autosync=true/false`

#### 4D. CMakeLists.txt

- [ ] **4D.1** 添加 Qt Multimedia 依赖
- [ ] **4D.2** 添加新源文件到构建

**涉及文件** (新建):
- `server/src/main/java/.../audio/AudioCodec.java`
- `server/src/main/java/.../audio/AudioCapture.java`
- `server/src/main/java/.../audio/AudioEncoder.java`
- `server/src/main/java/.../audio/AudioStream.java`
- `client/src/decoder/AudioDecoder.h/.cpp`
- `client/src/render/AudioPlayer.h/.cpp`
- `client/src/control/ClipboardSync.h/.cpp`

**涉及文件** (修改):
- `server/.../Options.java`
- `server/.../auxiliary/AuxMessage.java`
- `server/.../ScrcpySession.java`
- `server/.../Controller.java`
- `client/src/transport/auxiliary/AuxChannelClient.h/.cpp`
- `client/src/core/service/DeviceSession.h/.cpp`
- `client/src/transport/kcp/kcpserver.cpp`
- `client/src/transport/tcp/tcpserverhandler.cpp`
- `client/CMakeLists.txt`

**预估**: ★★★ 复杂（需端到端实现音频管线+剪切板同步）

---

## 任务5: 通道开关设置

**需求**: 在主设置页"显示选项"下方添加新区域，包含视频通道、控制通道、辅助通道三个开关。

### 架构设计

```
┌─────────────────────────────────┐
│ 通道控制                          │
│ ┌─────────────────────────────┐ │
│ │ 视频通道   [████████████ ON] │ │
│ │ 控制通道   [████████████ ON] │ │
│ │ 辅助通道   [████████████ ON] │ │
│ └─────────────────────────────┘ │
└─────────────────────────────────┘
```

### 步骤

- [ ] **5.1** 在 `ConfigCenter.h/.cpp` 添加三个持久化字段:
  - `videoChannelEnabled` (默认 `true`) — `user/videoChannelEnabled`
  - `controlChannelEnabled` (默认 `true`) — `user/controlChannelEnabled`
  - `auxChannelEnabled` (默认 `true`) — `user/auxChannelEnabled`
- [ ] **5.2** 修改 `SettingsPage.cpp::setupUI()`:
  - 在"显示选项"区域下方添加分隔线 + "通道控制"标题
  - 添加 FluentCard 包含 3 个 SettingRow + FluentToggle
  - 描述: "启用/禁用视频流通道" / "启用/禁用触控命令通道" / "启用/禁用辅助数据通道(实时参数、音频、剪切板)"
- [ ] **5.3** 在 `SettingsPage` 添加信号:
  - `channelSettingsChanged(bool video, bool control, bool aux)`
- [ ] **5.4** 修改 `DeviceController::start()` — 根据 ConfigCenter 的通道开关决定是否建立对应连接:
  - 视频通道关闭 → 不创建 StreamManager，不安装 video socket
  - 控制通道关闭 → 不创建 InputManager / ControlSender
  - 辅助通道关闭 → 不创建 AuxChannelClient
- [ ] **5.5** 修改 VideoSettingsPopup — 同步显示通道状态

**涉及文件**:
- `client/src/common/ConfigCenter.h/.cpp`
- `client/src/ui/pages/SettingsPage.h/.cpp`
- `client/src/transport/server/devicemanage.cpp`
- `client/src/ui/components/VideoSettingsPopup.cpp` (可选)

**预估**: ★★☆ 中等

---

## 任务6: 全部视频编码格式支持

**需求**: 添加 H.265 (HEVC) 和 AV1 编码格式支持。

**当前状态**: 端到端只有 H.264 硬编码路径。框架预留了 `setVideoCodec()` 接口但未使用。

### 步骤

#### 6A. 服务端

- [ ] **6A.1** 修改 `VideoCodec.java` — 添加 H265 和 AV1 枚举:
  ```java
  H264(0x68_32_36_34, "h264", MIMETYPE_VIDEO_AVC),
  H265(0x68_32_36_35, "h265", MIMETYPE_VIDEO_HEVC),
  AV1 (0x00_61_76_31, "av1",  MIMETYPE_VIDEO_AV1);
  ```
- [ ] **6A.2** 确认 `SurfaceEncoder.java` / `ScreenEncoder.java` 已通过 `VideoCodec.mimeType` 创建 MediaCodec — 如果是，添加枚举即可；如果不是，需要改编码器选择逻辑

#### 6B. 客户端解码器

- [ ] **6B.1** 修改 `ZeroCopyStreamManager.cpp` — 根据 `m_videoCodec` 选择 `AVCodecID`:
  ```cpp
  AVCodecID codecId = AV_CODEC_ID_H264;
  if (m_videoCodec == "h265") codecId = AV_CODEC_ID_HEVC;
  else if (m_videoCodec == "av1") codecId = AV_CODEC_ID_AV1;
  ```
- [ ] **6B.2** 修改 `demuxer.cpp::run()` — 同上逻辑替换硬编码的 `AV_CODEC_ID_H264`
- [ ] **6B.3** 修改 `decoder.cpp` — 同上
- [ ] **6B.4** 修改 KCP demuxer (`demuxer.cpp`) — 使用从 video header 读到的 codecId 而非 Q_UNUSED

#### 6C. UI 与配置

- [ ] **6C.1** 修改 `SettingsPage.cpp` — 编码下拉框添加 `"H.265 (HEVC)"`, `"AV1"`:
  ```cpp
  m_codecBox->addItems({"H.264", "H.265 (HEVC)", "AV1"});
  ```
- [ ] **6C.2** 修改 `SettingsPage::getVideoCodecName()`:
  ```cpp
  switch (index) {
      case 0: return "h264";
      case 1: return "h265";
      case 2: return "av1";
  }
  ```
- [ ] **6C.3** `Config` 已有 `videoCodecIndex` 字段 — 无需改动
- [ ] **6C.4** 确认 `ServerParams::videoCodec` 正确传递到 KCP/TCP 启动参数

**涉及文件**:
- `server/src/.../video/VideoCodec.java`
- `client/src/core/service/ZeroCopyStreamManager.cpp`
- `client/src/decoder/demuxer.cpp`
- `client/src/decoder/decoder.cpp`
- `client/src/ui/pages/SettingsPage.cpp`

**预估**: ★★☆ 中等

---

## 执行顺序

| 顺序 | 任务 | 依赖 | 预估 |
|------|------|------|------|
| 1 | 任务1: 设置弹窗 UI 调整 | 无 | ★☆☆ |
| 2 | 任务2: 视频传输状态持久化 | 无 | ★☆☆ |
| 3 | 任务3: 修复重连闪退 | 无 | ★★☆ |
| 4 | 任务6: 全部视频编码格式 | 无 | ★★☆ |
| 5 | 任务5: 通道开关设置 | 任务4(ConfigCenter) | ★★☆ |
| 6 | 任务4: 音频+剪切板 | 任务5(通道设置), 任务6(编码) | ★★★ |

> 先完成简单独立任务(1,2,3)，再做基础设施(6-编解码)，然后通道控制(5)，最后音频剪切板(4)。

---

## 完成检查清单

- [ ] 所有任务编译通过 (零警告/零错误)
- [ ] 设置弹窗三参一行平均布局，无 Mbps
- [ ] 关闭/重打开设置弹窗，视频传输状态保持
- [ ] USB 断开后重连不闪退
- [ ] H.265/AV1 编解码端到端工作
- [ ] 主设置页通道开关 UI 正确显示/持久化
- [ ] 音频流通过 AuxChannel 传输并播放
- [ ] 剪切板双向同步
