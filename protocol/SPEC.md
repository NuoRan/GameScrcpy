# GameScrcpy 协议规范 v1

> 状态：**v1 已定稿**（2026-10-01 用户批准，ADR-0025）。同日在发布前修订（尚无任何已部署实现，不升版本号）：触控帧与安全区（ADR-0030）、会话续连 / 可丢弃帧 / 统一时钟（ADR-0035）、服务端就绪信号（ADR-0037 R1）。接收端反馈消息 `ReceiverReport` 于 2026-10-03 补充（ADR-0051，用户确认）；WiFi 直连传输（第 10 章）在 M5 补充。`ForegroundAppChanged` 末尾的 `flags`（位 0 = 游戏）于 2026-10-30 补充（ADR-0072 第 1 节，起点面板据此判断前台是不是游戏；仍在发布前、尚无已部署实现，不升版本号）。伴侣 App 连接（第 11 章：分区 `0x06`、帧上下文"伴侣连接"、基本类型 `blob8`）于 2026-11-02 补充（ADR-0086）：只新增类型与上下文，已有消息的字节布局不变，不升版本号。设备状态（`SubscribeStatus` 0x030B、`DeviceStatus` 0x8305、能力位 11）于 2026-11-06 补充（ADR-0098）：只新增消息与能力位，已有消息的字节布局不变，不升版本号。手柄直通（`UhidCreate` 末尾的 `bus`、`UhidFeature` 0x0113、`UhidStatus` 0x8111、能力位 12）于 2026-11-06 补充（ADR-0103）：仍在发布前、尚无已部署实现，客户端与服务端随程序一起发布，不升版本号。伴侣截图（`ScreenshotRequest` 0x0607、`ScreenshotChunk` 0x8604、`ScreenshotFailed` 0x8605，11.8 节）于 2026-11-06 补充（ADR-0105 第 5 节）：只新增伴侣分区的消息，已有消息的字节布局不变，不升版本号。伴侣连接的版本 2（`CompanionSettings` 0x0608、`CompanionSettingChange` 0x8606、11.2 节的版本协商与 11.9 节的设置同步）于 2026-11-11 补充（ADR-0112 第 5.2 节）：只新增伴侣分区的消息，已有消息的字节布局不变；升的是伴侣连接自己的版本（`CompanionHello.version`），主协议版本不变，电脑对只会版本 1 的伴侣照旧说版本 1。
> 本文件是客户端（C++，`src/protocol`）、服务端与伴侣 App（Java，`android/protocol`）的唯一事实来源。
> 改动流程：先改本文件 → 再改 `vectors/` → 两侧实现 → 两侧测试全绿，才可提交。任何字节布局变化都必须提升协议版本。

---

## 1. 通用约定

### 1.1 基本类型

| 记法 | 含义 |
|---|---|
| `u8 / u16 / u32 / u64` | 无符号整数，**大端序** |
| `i16 / i32 / i64` | 二进制补码有符号整数，大端序 |
| `bool` | `u8`，只允许 0 或 1；其他值为解码错误 `InvalidBool` |
| `bytes[N]` | 定长 N 字节 |
| `str8 / str16 / str32` | 长度前缀（`u8 / u16 / u32`）+ UTF-8 字节，不含结尾 `\0`；非法 UTF-8 为 `MalformedUtf8` |
| `blob8 / blob16 / blob32` | 长度前缀（`u8 / u16 / u32`）+ 原始字节 |

- 每个变长字段在定义处给出**最大字节数**，超出即 `LengthExceeded`。
- 枚举字段出现未定义的值即 `InvalidEnum`；保留位必须为 0，否则 `ReservedBitsSet`。
- 负载解码后若还有剩余字节即 `TrailingBytes`；不足即 `Truncated`。

### 1.2 坐标

- 触摸与滚轮坐标为**相对当前视频画面**的归一化定点数：`x, y : u16`，0 = 左 / 上边缘，65535 = 右 / 下边缘。
- 每个坐标事件携带 `videoSession : u16`（见 3.2 节），服务端用该视频会话的几何信息把坐标映射到显示器坐标。

### 1.3 方向位与类型号分区

- 消息类型 `type : u16`。**第 15 位为 1 表示由服务端发出**，为 0 表示由客户端发出；接收方收到方向错误的类型即 `WrongDirection`。
- 分区（低 15 位的高字节）：`0x00` 握手、`0x01` 输入、`0x02` 媒体配置、`0x03` 设备能力与事件、`0x04` 连接维护、`0x05` 大块数据响应、`0x06` 伴侣 App（第 11 章）。

---

## 2. 会话与通道

### 2.1 启动参数

- 客户端生成 16 字节随机**会话令牌** `token`，以及随机的抽象命名空间 socket 名 `gamescrcpy_<16 位十六进制>`。
- 服务端命令行只携带非机密参数：协议版本、socket 名、传输方式（`transport=adb`，或 `transport=udp` 加可选的 `udpPort`，见 10.1 节）、日志级别、会话保留时长 `lingerMs`（默认 15000，0 = 不保留，见第 7 章）。
- `token` 经服务端进程**标准输入**的第一行传入（`GS_TOKEN=<32 位小写十六进制>`），**绝不出现在命令行**（其他进程可能读取命令行参数）。
- **就绪信号**：服务端读到令牌并开始监听后，向标准输出写一行 `GS_READY <协议版本>`（ASCII，以 `\n` 结尾）；客户端读到这一行才建立连接，不做重试轮询。启动失败时服务端写 `GS_FAILED <错误码> <说明>`（错误码见第 8 章）后退出。

### 2.2 通道

| 编号 | 通道 | 方向 | 内容 |
|---|---|---|---|
| 0 | control | 双向 | 握手、输入、配置、设备能力与事件、维护消息（消息帧） |
| 1 | video | 服务端 → 客户端 | 视频媒体包 |
| 2 | audio | 服务端 → 客户端 | 音频媒体包 |
| 3 | bulk | 服务端 → 客户端 | 截图、应用列表、应用图标（消息帧） |

adb 隧道传输（v1）：服务端在抽象 socket 上监听，客户端经 `adb forward tcp:<本地端口> localabstract:<socket 名>` 连接；**每个通道一条独立连接**，互不阻塞。

### 2.3 连接开局（所有连接一致）

1. 服务端接受连接后立即发送 `ServerHello`（含本连接专属的 16 字节随机数 `nonce`）。
2. 第一条连接：客户端回复 `ClientHello` → 服务端校验后回复 `Welcome`，或回复 `HandshakeError` 并关闭。此连接即 control 通道。
3. 会话建立后的其他连接：客户端回复 `ChannelOpen` → 服务端校验后回复 `ChannelAccepted`，随后开始发送该通道的数据；否则回复 `HandshakeError` 并关闭。
4. 会话建立前收到 `ChannelOpen` 为 `UnexpectedMessage`。会话建立后又在一条新连接上收到 `ClientHello`：协议版本与证明都通过，表示同一客户端换了一条连接回来（WiFi 直连换了地址、或客户端先察觉断线；只有持有令牌的客户端做得出证明）——服务端把当前 control 连接按断开处理（第 7 章：释放触点、关闭其他通道、进入保留），随即以 `resumed` = 1 的 `Welcome` 接受这一条，即"接替"（ADR-0056）；版本或证明不通过仍为 `UnexpectedMessage`，当前会话不受影响。
5. 握手阶段的帧负载上限 16 KiB；从接受连接到握手完成超过 5 秒即关闭连接。

### 2.4 认证

- `proof = HMAC-SHA256(token, context ‖ nonce [‖ channel])`，输出完整 32 字节。
- `ClientHello` 的 `context` 为 ASCII `"GSCP/1 hello"`；`ChannelOpen` 的 `context` 为 ASCII `"GSCP/1 channel"`，末尾追加 1 字节通道编号。
- 每条连接的 `nonce` 不同，证明不可跨连接重放。比较证明必须使用常量时间比较。

---

## 3. 帧格式

### 3.1 消息帧（control、bulk 通道，所有连接的握手阶段，以及伴侣连接）

| 偏移 | 字段 | 类型 | 说明 |
|---|---|---|---|
| 0 | type | u16 | 消息类型（见 1.3） |
| 2 | length | u32 | 负载字节数 |
| 6 | payload | bytes[length] | |

负载上限：握手阶段 16 KiB；control 通道 256 KiB；bulk 通道 64 MiB；伴侣连接 16 KiB。超限即 `FrameTooLarge`，关闭连接。

各上下文允许的消息类型：

| 上下文 | 客户端 → 服务端 | 服务端 → 客户端 |
|---|---|---|
| 握手阶段 | ClientHello、ChannelOpen | ServerHello、Welcome、HandshakeError、ChannelAccepted |
| control | 0x01xx、0x02xx、0x03xx、0x04xx | 0x8110、0x8111、0x83xx、0x84xx |
| bulk | （无） | 0x85xx |
| 伴侣连接（第 11 章） | 0x06xx、0x0401、0x0402 | 0x86xx、0x8401、0x8402 |

### 3.2 媒体包（video、audio 通道，`ChannelAccepted` 之后）

| 偏移 | 字段 | 类型 | 说明 |
|---|---|---|---|
| 0 | kind | u8 | 0 = 会话描述，1 = 编解码配置，2 = 数据帧 |
| 1 | flags | u8 | 位 0 = 关键帧；位 1 = 可丢弃（该帧不被其他帧引用，拥塞时发送端可丢弃）；两位均仅用于 kind 2，其余位保留为 0 |
| 2 | session | u16 | 编码会话号：第一个会话为 1，每次重建编码递增，65535 之后回到 1；0 保留 |
| 4 | ptsUs | i64 | 采集时刻（微秒，手机单调时钟，与 `Pong.serverTimeUs` 同一时钟，见第 7 章）；kind 0、1 固定为 0 |
| 12 | encodeDelayUs | u32 | 从采集到编码器交出这一帧（服务端准备发送）的微秒数，同一时钟；超出 u32 取最大值；kind 0、1 固定为 0（ADR-0048） |
| 16 | size | u32 | 负载字节数；视频 ≤ 16 MiB，音频 ≤ 64 KiB |
| 20 | payload | bytes[size] | |

- 每个编码会话以 **会话描述 → 编解码配置 → 数据帧…** 的顺序发送；客户端收到新会话描述时重建解码器。
- 视频会话描述负载（14 字节）：`codec u8 | frameWidth u16 | frameHeight u16 | displayId u32 | displayWidth u16 | displayHeight u16 | rotation u8`（rotation 为采集时显示器旋转的 90° 份数 0–3）。
- 音频会话描述负载（6 字节）：`codec u8 | sampleRate u32 | channels u8`。
- `codec`：视频 1 = H.264、2 = H.265、3 = AV1；音频 1 = Opus。
- 编解码配置负载（kind 1）：H.264 / H.265 为 Annex B 参数集（SPS、PPS，H.265 另含 VPS，带起始码），可直接放在码流前；AV1 为 `AV1CodecConfigurationRecord`（av1C，首字节 0x81），交给解码器作为额外数据，不属于 OBU 码流（序列头随关键帧在码流内）；Opus（音频）为 OpusHead（RFC 7845 标识头，以 `OpusHead` 开头），交给解码器作为额外数据。
- 数据帧负载：H.264 / H.265 为一个 Annex B 访问单元；AV1 为一个时间单元的 OBU（低开销格式，带 `obu_size`）。
- 画面静止时编码器可能不产生新帧，帧间隔不代表连接异常；连接存活由心跳判断（第 7 章）。

### 3.3 解码检查顺序（两侧必须一致，保证同一畸形输入报同一错误）

消息帧：
1. 帧头不足 6 字节 → `Truncated`
2. 类型未在本规范中定义 → `UnknownType`
3. 方向位与接收方不符 → `WrongDirection`
4. 类型不属于当前上下文的允许集合 → `UnexpectedType`
5. `length` 超过当前上下文上限 → `FrameTooLarge`
6. 负载不足 `length` 字节 → `Truncated`
7. 按字段顺序解码负载（见下）
8. 负载有剩余字节 → `TrailingBytes`
9. 消息级约束不满足 → `OutOfRange`（`TouchFrame`：同一帧内 `pointerId` 重复；`DeviceStatus`：`batteryPercent` 不在 0–100 且不是 255，或 `cpuPermille` 不在 0–1000 且不是 65535；`ScreenshotChunk`：`index` 不小于 `total`；`CompanionSettingChange`：`change` 为 0，或 `value` 不在 `item` 允许的范围内）

媒体包：包头不足 20 字节 → `Truncated`；`kind` 非法 → `InvalidEnum`；`flags` 含本 kind 不允许的位 → `ReservedBitsSet`；`session` 为 0 → `OutOfRange`；kind 0 / 1 的 `ptsUs` 或 `encodeDelayUs` 非 0 → `OutOfRange`；`size` 超上限 → `FrameTooLarge`；负载不足 → `Truncated`；会话描述按字段解码；负载有剩余 → `TrailingBytes`。

单个字段：
- 定长字段字节不足 → `Truncated`
- 变长字段：先读长度前缀（不足 → `Truncated`），长度超过上限 → `LengthExceeded`，再检查数据是否足够（不足 → `Truncated`），字符串再校验 UTF-8（→ `MalformedUtf8`）
- 列表：先读计数，超过上限 → `LengthExceeded`，低于下限（如 `TouchFrame` 至少 1 项）→ `OutOfRange`，再逐项解码
- 枚举 → `InvalidEnum`；布尔 → `InvalidBool`；保留位 → `ReservedBitsSet`；数值范围（如 `pointerId`、`sizePx`、`rotation`）→ `OutOfRange`；`ServerHello.magic` 不符 → `BadMagic`

---

## 4. 握手消息

| 类型 | 名称 | 方向 | 负载 |
|---|---|---|---|
| 0x8001 | ServerHello | S→C | `magic u32 = 0x47534350（"GSCP"）` · `protocolVersion u16` · `nonce bytes[16]` |
| 0x0002 | ClientHello | C→S | `protocolVersion u16` · `clientVersion str8（≤ 32）` · `proof bytes[32]` |
| 0x8003 | Welcome | S→C | 见 4.1 |
| 0x8004 | HandshakeError | S→C | `code u16`（第 8 章）· `message str16（≤ 1024）` |
| 0x0005 | ChannelOpen | C→S | `channel u8（1–3）` · `proof bytes[32]` |
| 0x8006 | ChannelAccepted | S→C | `channel u8` |

### 4.1 Welcome 负载

| 字段 | 类型 | 说明 |
|---|---|---|
| serverVersion | str8（≤ 32） | |
| manufacturer | str8（≤ 64） | `ro.product.manufacturer` |
| model | str8（≤ 64） | `ro.product.model` |
| androidRelease | str8（≤ 32） | `ro.build.version.release` |
| sdkInt | u16 | |
| serialNo | str8（≤ 64） | `ro.serialno`，客户端据此识别设备 |
| displayCount | u8（≤ 16） | 后随 `displayCount` 个 Display |
| encoderCount | u8（≤ 32） | 后随 `encoderCount` 个 Encoder |
| audioStatus | u8 | 0 可用；1 系统版本不支持（< Android 11）；2 采集失败 |
| capabilities | u32 | 能力位，见 4.2 |
| resumed | bool | 1 = 本次握手恢复了保留中的会话（第 7 章） |

- **Display**（23 字节）：`displayId u32 | width u16 | height u16 | rotation u8 | densityDpi u16 | refreshMilliHz u32 | safeLeft u16 | safeTop u16 | safeRight u16 | safeBottom u16`（`safe*` 为当前方向下的安全区内边距，单位像素，来自 DisplayCutout；无刘海 / 挖孔时为 0）
- **Encoder**：`codec u8 | name str8（≤ 128） | hardware bool`

### 4.2 能力位

| 位 | 能力 | 位 | 能力 |
|---|---|---|---|
| 0 | 剪贴板同步 | 5 | 应用列表与启动 |
| 1 | 文本注入 | 6 | 无损截图 |
| 2 | UHID 设备 | 7 | 前台应用事件 |
| 3 | 屏幕电源控制 | 8 | 虚拟显示 |
| 4 | 旋转控制 | 9 | 显示触摸操作（测延迟） |
| | | 10 | 独立显示器（第 7 章） |
| | | 11 | 设备状态：电量、温度、CPU 占用（第 7 章） |
| | | 12 | 手柄直通：`UhidFeature`、`UhidCreate.bus`、`UhidStatus`（第 7 章） |
| | | 13–31 | 保留（必须为 0） |

服务端只为实际可用的功能置位（例如设备没有剪贴板服务时不置位 0，输入注入被拒时不置位 1）；客户端只对已置位的功能发送对应消息。

---

## 5. 客户端 → 服务端消息（control 通道）

### 5.1 输入（0x01xx）

| 类型 | 名称 | 负载 |
|---|---|---|
| 0x0101 | TouchFrame | `videoSession u16` · `count u8（1–32）` · `count × TouchPoint`；**TouchPoint**（8 字节）=`action u8`（0 按下、1 抬起、2 移动、3 取消）· `pointerId u8（0–31）` · `x u16` · `y u16` · `pressure u16`；同一帧内 `pointerId` 不得重复 |
| 0x0102 | Key | `action u8`（0 按下、1 抬起）· `keycode u32`（Android KEYCODE）· `metaState u32` · `repeat u16`（共 11 字节） |
| 0x0103 | Text | `text str16（≤ 4096）` |
| 0x0104 | Scroll | `x u16` · `y u16` · `videoSession u16` · `hScroll i16` · `vScroll i16`（单位：1/256 格） |
| 0x0105 | BackOrScreenOn | `action u8`（0 按下、1 抬起） |
| 0x0110 | UhidCreate | `id u16` · `vendorId u16` · `productId u16` · `name str8（≤ 127）` · `reportDescriptor blob16（≤ 4096）` · `bus u8`（Linux 的总线号：0x03 USB、0x05 蓝牙；客户端不关心时为 0x03） |
| 0x0111 | UhidInput | `id u16` · `report blob16（≤ 512）` |
| 0x0112 | UhidDestroy | `id u16` |
| 0x0113 | UhidFeature | `id u16` · `reportNumber u8` · `data blob16（≤ 512）`（这台设备的一个特性报告，`data` 含开头的报告编号；在同一 `id` 的 `UhidCreate` 之前发，语义见第 7 章） |

### 5.2 媒体配置（0x02xx）

| 类型 | 名称 | 负载 |
|---|---|---|
| 0x0201 | ConfigureVideo | `source u8`（0 物理显示器、1 虚拟显示）· `displayId u32`（source 0 有效）· `virtualWidth u16` · `virtualHeight u16` · `virtualDensity u16`（source 1 有效，否则为 0）· `codec u8` · `maxSize u16`（0 = 原始尺寸）· `maxFps u16`（0 = 不限）· `bitrate u32`（bps）· `intraRefreshMs u16`（0 = 关闭） |
| 0x0202 | SetVideoBitrate | `bitrate u32` |
| 0x0203 | SetMaxFps | `maxFps u16` |
| 0x0204 | RequestKeyFrame | （空） |
| 0x0205 | SetVideoPaused | `paused bool` |
| 0x0211 | ConfigureAudio | `enabled bool` · `bitrate u32` |

### 5.3 设备能力（0x03xx）

| 类型 | 名称 | 负载 |
|---|---|---|
| 0x0301 | SetClipboard | `sequence u32` · `paste bool` · `text str32（≤ 262000）` |
| 0x0302 | GetClipboard | `copyKey u8`（0 不模拟、1 复制、2 剪切） |
| 0x0303 | SetDisplayPower | `on bool` |
| 0x0304 | SetOrientation | `mode u8`（0 跟随设备；1–4 锁定为 0°/90°/180°/270°） |
| 0x0305 | StartApp | `package str8（≤ 255）` · `flags u8`（位 0 = `forceStop` 先强制停止；位 1 = `onVideoDisplay` 启动到本会话视频所采集的显示器，为 0 时启动到主显示器；其余保留） |
| 0x0306 | SystemPanel | `action u8`（0 展开通知、1 展开快捷设置、2 收起） |
| 0x0307 | RequestAppList | `requestId u32` · `includeSystem bool` |
| 0x0308 | RequestAppIcon | `requestId u32` · `package str8（≤ 255）` · `sizePx u16（16–512）` |
| 0x0309 | RequestScreenshot | `requestId u32` · `displayId u32` · `format u8`（0 = PNG） |
| 0x030A | SetShowTouches | `on bool` |
| 0x030B | SubscribeStatus | `on bool` · `periodMs u16（500–60000）`（订阅设备状态，语义见第 7 章） |

### 5.4 连接维护（0x04xx）

| 类型 | 名称 | 负载 |
|---|---|---|
| 0x0401 | Ping | `nonce u32` · `clientTimeUs u64` |
| 0x0402 | Goodbye | `reason u16`（第 8 章） |
| 0x0403 | ReceiverReport | `videoSession u16` · `intervalUs u32` · `frames u16` · `bytes u32` · `delayMinUs i64` · `delayMeanUs i64` · `delayMaxUs i64` · `roundTripUs u32` · `lostPackets u32` · `recoveredPackets u32`（共 48 字节；语义见第 7 章，ADR-0051） |

---

## 6. 服务端 → 客户端消息

### 6.1 control 通道

| 类型 | 名称 | 负载 |
|---|---|---|
| 0x8110 | UhidOutput | `id u16` · `data blob16（≤ 512）` |
| 0x8111 | UhidStatus | `id u16` · `recognized bool` · `vibration bool`（建好设备后报一次，语义见第 7 章） |
| 0x8301 | ClipboardChanged | `text str32（≤ 262000）` |
| 0x8302 | ClipboardAck | `sequence u32` |
| 0x8303 | DisplayChanged | `display Display`（4.1 节结构） |
| 0x8304 | ForegroundAppChanged | `package str8（≤ 255）` · `activity str8（≤ 255）` · `flags u8`（位 0 = `game` 游戏；其余保留） |
| 0x8305 | DeviceStatus | `batteryPercent u8`（电量百分比 0–100，255 = 未知）· `charging bool`（正在充电）· `temperatureDeciC i16`（电池温度，单位 0.1 °C，-32768 = 未知）· `cpuPermille u16`（CPU 占用，千分之一 0–1000，65535 = 未知）（共 6 字节） |
| 0x8401 | Pong | `nonce u32` · `clientTimeUs u64` · `serverTimeUs u64` |
| 0x8402 | Goodbye | `reason u16` · `message str16（≤ 1024）` |
| 0x8403 | Notice | `level u8`（0 信息、1 警告、2 错误）· `code u16` · `message str16（≤ 1024）` |

### 6.2 bulk 通道

| 类型 | 名称 | 负载 |
|---|---|---|
| 0x8501 | AppList | `requestId u32` · `count u16（≤ 4096）` · `count ×（package str8（≤ 255）· label str8（≤ 255）· versionCode u64 · flags u8）`（versionCode 为应用的版本号 `PackageInfo.getLongVersionCode()`；flags 位 0 = `system` 系统应用、位 1 = `enabled` 已启用、位 2 = `game` 游戏，其余保留） |
| 0x8502 | AppIcon | `requestId u32` · `package str8（≤ 255）` · `png blob32（≤ 1 MiB）` |
| 0x8503 | Screenshot | `requestId u32` · `width u16` · `height u16` · `format u8` · `data blob32（≤ 48 MiB）` |
| 0x8504 | RequestFailed | `requestId u32` · `code u16` · `message str16（≤ 1024）` |

---

## 7. 语义与时序

- **触控帧**：一条 `TouchFrame` 表示同一时刻发生的全部触点变化；`pointerId` 由客户端分配，同时活跃的触点 ID 唯一。服务端规则：
  - 合成为最少的 MotionEvent，顺序固定：① 一次 `ACTION_MOVE`，携带本帧所有移动的触点以及即将抬起（抬起，不含取消）触点的最终位置（位置都没有变化则不发）；② 逐个注入抬起 / 取消；③ 逐个注入按下。
  - Android 的触点 ID 由服务端分配，与真实触摸屏一致：每个新按下的触点取当前最小的空闲 ID，抬起或取消后释放；MotionEvent 里的触点按 Android ID 从小到大排列。`pointerId` 只是客户端这边的身份，不原样交给 Android（虚幻 4 等游戏只认 0～10 的触点 ID，超出的触摸被丢弃）。
  - 按下已活跃的触点视为移动；移动 / 抬起 / 取消一个未活跃的触点直接忽略。
  - `videoSession` 不是当前编码会话时：丢弃按下与移动；**抬起与取消照常执行**（在该触点最后位置），保证不会遗留按住的触点。
  - 取消：在该触点最后位置立即释放（忽略本条的坐标，先移动可能触发拖动），不产生点击语义：最后一个活跃触点用 `ACTION_CANCEL`；其余触点在 Android 13+ 用带 `FLAG_CANCELED` 的 `ACTION_POINTER_UP`，更低版本退化为普通 `ACTION_POINTER_UP`。
  - control 连接断开时，服务端以同样的取消方式释放全部触点，并以带 `FLAG_CANCELED` 的抬起释放仍按住的按键。
- **UHID 设备**（能力位 2；`UhidFeature`、`bus` 与 `UhidStatus` 另需能力位 12，ADR-0009、ADR-0103）：
  - `UhidCreate` 经 `/dev/uhid` 的 `UHID_CREATE2` 在手机上建一台设备：名字、厂商号、产品号、总线号 `bus` 与报告描述符原样交给内核，版本号与国家码为 0。同一 `id` 再建时先销毁旧的；每个会话最多 8 台，超出的不建。
  - `UhidFeature` 先把一台设备将要用的特性报告交给服务端：服务端按 `id` 与 `reportNumber` 记下（同一编号再来时替换；每个 `id` 最多 16 个，最多为 8 个还没建的 `id` 记，超出的丢弃），建这个 `id` 的设备时交给它。内核要特性报告（`UHID_GET_REPORT`，类型为特性）时，服务端先按编号查这些报告，再查从描述符算出来的（目前只有触摸屏的"最多触点数"），都没有时回 `EIO`；回答的数据就是 `data` 本身（含开头的报告编号）。不少手柄驱动在建设备的那一刻就来要（例如 DualShock 4 的 0x02、0x81、0xA3、0x12，DualSense 的 0x05、0x09、0x20），所以必须在 `UhidCreate` 之前发。`UhidDestroy`、同一 `id` 的设备销毁与 control 连接断开时一并丢弃。
  - `UhidInput` 把一份输入报告交给这台设备（`UHID_INPUT2`），`UhidDestroy` 销毁它。设备的输出报告（`UHID_OUTPUT`，例如手柄震动）以 `UhidOutput` 转发，超过 512 字节的丢弃；`UHID_SET_REPORT` 一律回成功。
  - `UhidStatus`：每次 `UhidCreate` 之后报一次。服务端建好设备后最多看 1.5 秒：手机的输入设备里新出现一台厂商号与产品号都相同的设备（建之前就在的不算）就立即回 `recognized` = 1，`vibration` 为它有没有震动器；1.5 秒内没有出现回 `recognized` = 0、`vibration` = 0。建设备失败（含超出 8 台）时立即回 `recognized` = 0。服务端读不到输入设备列表时按认出处理（`recognized` = 1、`vibration` = 0）。设备在报告之前已被销毁时不报。客户端据此在手机不认这类设备时改建别的（ADR-0103：改成通用手柄）。
  - control 连接断开时，服务端先给每台设备发一份全零的输入报告（全部松开）再销毁全部设备；顶层应用集合是游戏手柄或摇杆（Generic Desktop 的 Game Pad、Joystick）的设备全零不代表松开，只销毁，由手机在设备移除时自己松开。
  - 服务端能打开 `/dev/uhid` 时同时置能力位 2 与 12；客户端只对置位 12 的服务端发 `UhidFeature`。
- **剪贴板**（能力位 0）：双方各记一份"已共享的文本"，只有内容与之不同时才发送，以免来回回声。服务端在剪贴板变化后发 `ClipboardChanged`；客户端经 `SetClipboard` 写入的内容、连接时剪贴板里已有的内容、文本注入临时借用剪贴板写入的内容都不回传。`SetClipboard` 写入后回 `ClipboardAck`（`sequence` 原样），`paste` = 1 时随后注入粘贴键；写入被拒绝时以 `Notice(ClipboardDenied)` 报告。`GetClipboard` 的 `copyKey` 为复制 / 剪切时先注入该键，结果经 `ClipboardChanged` 报告（与已共享的文本相同则不报告）；为 0 时总是回复，直接报告当前内容，剪贴板里没有文字时报告空文本。超过长度上限的文本在字符边界截断。
- **滚轮**：`hScroll` / `vScroll` 正值表示向右 / 向上（与 Windows 滚轮和 Android `AXIS_HSCROLL` / `AXIS_VSCROLL` 一致）；`videoSession` 不是当前编码会话时丢弃。
- **时钟**：`ptsUs`（视频与音频）与 `Pong.serverTimeUs` 均为手机单调时钟（`CLOCK_MONOTONIC`）的微秒值，`ptsUs` 取采集时刻；`clientTimeUs` 为客户端单调时钟，服务端原样回传。客户端据 `Ping` / `Pong` 估计时钟差，计算采集 → 上屏的端到端延迟。
- **视频会话**：服务端在旋转、尺寸变化、`ConfigureVideo`、虚拟显示调整，以及极窄链路下降级或恢复分辨率与帧率（ADR-0054）时重建编码，并以新的会话号发送会话描述。
- **心跳**：客户端至少每 2 秒发送一次 `Ping`；服务端 6 秒内未在 control 通道收到任何帧即视为连接断开（见下）；客户端 6 秒内未收到 `Pong` 即判定连接失效并尝试续连。
- **保留与续连**：control 连接断开或心跳超时时，服务端立即释放全部触点（按取消处理）、停止采集与发送、关闭其他通道，进入保留状态 `lingerMs` 毫秒。期间服务端继续监听：新连接按 2.3 节完成 `ClientHello`（同一令牌）后回复 `Welcome`（`resumed` = 1），客户端随后重新打开各通道，服务端以新的视频会话号从关键帧开始发送。保留期满仍未恢复，服务端清理并退出进程。服务端尚未察觉断开时（UDP 没有关闭信号），客户端的新握手按 2.3 节第 4 条接替旧连接，效果相同。
  - 服务端在保留期间保留视频设置与暂停状态。断线前发出的消息可能未送达，因此客户端在每次握手（含续连）后重发当前的视频配置（`ConfigureVideo`，含其后 `SetVideoBitrate` / `SetMaxFps` 的调整）与暂停状态；`ConfigureVideo` 与正在编码的设置完全相同时，服务端不重建编码。
- **结束**：任一方发送 `Goodbye` 即结束整个会话，服务端不进入保留状态，关闭所有通道并退出进程。发送 `Goodbye` 的一方随后继续读，直到对端关闭连接为止再关闭：带着未读数据关闭会使连接被重置，对端可能丢掉尚未读取的 `Goodbye`。不要只关闭发送方向——adb 转发在一侧结束时会拆掉整条连接，Android 9 上实测会因此丢掉 `Goodbye`。
- **请求 / 响应**：bulk 响应以 `requestId` 对应请求；同一时间最多 4 个未完成的 bulk 请求，超出时服务端以 `RequestFailed(ResourceBusy)` 拒绝。
- **应用列表与启动**（能力位 5，APP-001；2026-10-30 发布前补充，`AppList` 条目加 `versionCode` 与 `game`、`StartApp` 加 `onVideoDisplay`，尚无已部署实现，不升版本号）：
  - 客户端先打开 bulk 通道（adb 隧道上按 2.3 节 `ChannelOpen`；WiFi 直连时 bulk 是可靠通道的一条流，握手后即可用），再发 `RequestAppList` / `RequestAppIcon`；请求到达时 bulk 通道不存在，服务端丢弃该请求。
  - `AppList` 列出可启动的应用（有桌面入口 `CATEGORY_LAUNCHER` 或电视桌面入口 `CATEGORY_LEANBACK_LAUNCHER`），`includeSystem` 为 0 时不含系统应用；`label` 为手机当前语言下的名称，超过 255 字节在字符边界截断；顺序不定，超过 4096 个只给前 4096 个。`game` 表示应用类别为游戏（`ApplicationInfo.CATEGORY_GAME`，或旧的 `FLAG_IS_GAME`）。客户端以包名加 `versionCode` 作图标缓存的键。
  - `AppIcon` 是应用图标（与桌面所示相同）画到 `sizePx × sizePx` 的 PNG；包名不存在时回 `RequestFailed(NotFound)`。
  - `StartApp` 启动应用的桌面入口：`forceStop` 为 1 时先强制停止该应用；`onVideoDisplay` 为 0 时启动到主显示器（显示器 0），为 1 时启动到本会话视频所采集的显示器（通常是下文的独立显示器）。应用不存在或没有桌面入口时回 `Notice(error, NotFound)`，系统拒绝启动时回 `Notice(error, InternalError)`；为 1 而会话还没有视频配置时回 `Notice(error, NotFound)`。
- **截图**（能力位 6，VIS-001）：
  - 与应用列表相同，客户端先打开 bulk 通道再发 `RequestScreenshot`。`displayId` 为要截取的显示器（0 = 主显示器；本会话独立显示器的编号同样可用），`format` 目前只有 0（PNG）。
  - `Screenshot` 是该显示器当前方向下的全分辨率画面，`width` × `height` 即它当前方向下的像素尺寸；PNG，无损。
  - 标为安全（`FLAG_SECURE`）的窗口：Android 12 起截出来是黑的；Android 11 及以前服务端的镜像是安全显示器，这些窗口与视频里一样照常可见。
  - 显示器不存在时回 `RequestFailed(NotFound)`；采集失败（含 2 秒内没有得到画面）或 PNG 超过 48 MiB 时回 `RequestFailed(InternalError)`。
  - 与应用列表、应用图标共用上文"同一时间最多 4 个未完成的 bulk 请求"的限额（请求在它的回复写出之前都算未完成），超出时同样回 `RequestFailed(ResourceBusy)`。
  - 截图在服务端自己的线程上完成、经 bulk 通道发送，从不推迟 control 通道：输入、`Pong` 等照常处理。
- **独立显示器**（能力位 10，APP-002；Android 11 起提供）：
  - `ConfigureVideo` 的 `source` 为 1 时，服务端新建一个虚拟显示器（"独立显示器"），宽 `virtualWidth`、高 `virtualHeight` 像素、密度 `virtualDensity` dpi；三者为 0 时取主显示器自然方向下的尺寸与它的密度。本会话的视频采集它（会话描述里的 `displayId` 是它的编号），触控、滚轮、按键与文字注入到它上面；主显示器与其上的应用不受影响。服务端不置能力位 10 时，`source` 为 1 回 `Notice(error, ChannelUnavailable)`，没有视频。
  - 标志与 scrcpy 3.x 的 `--new-display` 默认相同：公开、演示、只显示自己的内容、支持触摸、随内容旋转、移除时销毁内容、显示系统装饰（导航栏、桌面、输入法可在其上出现）；Android 13 起另加受信任、独立显示组、始终解锁、关闭触摸反馈；Android 14 起另加独立焦点、设备显示组。
  - 生命周期：每个会话至多一个；重建编码与保留期（第 7 章"保留与续连"）都不影响它，续连后照常采集，上面的应用保持原状；会话结束（`Goodbye`、保留期满、服务端退出）或之后的 `ConfigureVideo` 改为 `source` 0、改了尺寸或密度时释放，上面运行的内容随之关闭，不会移到主显示器。
  - 旋转：显示器随内容旋转；内容把它转到 90° 或 270° 时，服务端交换它的宽高，使画面保持正立，并以新的视频会话重建编码（与手机旋转相同）。
  - 本会话的 `ForegroundAppChanged` 报告本会话视频所在显示器上的前台应用：主显示器的会话只看主显示器，独立显示器的会话只看独立显示器（Android 10 以前无法区分显示器时报告全局前台）。
- **剪贴板**（能力位 0）：设备剪贴板变为双方尚未共有的文本时，服务端发送 `ClipboardChanged`。以下三种不发送：客户端经 `SetClipboard` 写入的内容；服务端为文本注入临时写入再恢复的内容（ADR-0043）；control 连接建立时剪贴板里已有的内容。判断依据是变化通知到达时剪贴板的实际内容，而不是谁在写入，因为通知晚于写入。`SetClipboard` 写入后回复 `ClipboardAck`（同一 `sequence`）；`paste` 为真时随后注入粘贴键。`GetClipboard` 的 `copyKey` 为复制或剪切时，服务端先注入该键，结果照常经 `ClipboardChanged` 报告；为 0 时总是回复，直接发送当前内容，剪贴板里没有文字时发送空文本（不改动双方共有的文本）。文本超过 262000 字节时服务端在字符边界截断，客户端不发送超长文本。剪贴板拒绝访问时以 `Notice(ClipboardDenied)` 上报。客户端同样只在内容与双方共有的文本不同时才发送 `SetClipboard`，收到的 `ClipboardChanged` 写入本机剪贴板后不回传。
- **屏幕电源**（能力位 3）：`SetDisplayPower(false)` 关闭设备全部物理屏幕的显示，画面照常采集；`true` 重新点亮。会话结束时（`Goodbye`、保留期满、服务端退出），服务端点亮客户端关闭的屏幕。设备的电源键照常可以点亮屏幕。
- **方向**（能力位 4）：`SetOrientation` 的 1–4 把主显示器锁定在 0° / 90° / 180° / 270°；0 恢复设备在第一次锁定前的状态（原先锁定则锁回原方向，原先自动旋转则恢复自动旋转）。会话结束时同样恢复。
- 客户端在每次进入串流（含续连）后重发用户改过的屏幕电源与方向设置；用户没改过的不发，设备自己的屏幕与旋转状态保持不动。
- **显示触摸操作**（能力位 9，ADR-0076）：`SetShowTouches(true)` 打开手机的"显示触摸操作"（系统设置 `show_touches`），`false` 恢复第一次打开之前的值（原本就开着则保持开着）。只供客户端测量延迟，测完即发 `false`；control 连接断开（含进入保留状态）与会话结束时服务端同样恢复，续连后不重发。服务端读不到这项设置时不置能力位 9；写入被拒绝时以 `Notice(PermissionDenied)` 上报。
- **前台应用**（能力位 7）：control 连接建立（含续连）后，服务端发送一次当前的前台应用 `ForegroundAppChanged`，之后前台的应用、界面或游戏标记变化时再发送；三项都相同不重复发送。`package` 为包名，`activity` 为界面（Activity）的完整类名，拿不到时（例如刚开机、没有任务）两者为空，超过 255 字节时在字符边界截断。`flags` 位 0（`game`）表示手机把这个应用标为游戏：它的 `ApplicationInfo.category` 为 `CATEGORY_GAME`，或带有旧的 `FLAG_IS_GAME` 标记（清单里的 `android:appCategory="game"` 或 `android:isGame="true"`）；`package` 为空或读不到这个应用的信息时为 0。客户端据此决定是否为没有方案的游戏显示起点面板（ADR-0072 第 1 节）。服务端靠系统的任务栈变化通知得知变化，不轮询；注册不了这个通知时不置能力位 7。
- **设备状态**（能力位 11，ADR-0098）：服务端能回答订阅就置位 11，拿不到的项报未知。
  - 订阅：客户端发 `on` = 1 的 `SubscribeStatus`，服务端立即采样并发一条 `DeviceStatus`，之后每 `periodMs` 毫秒发一条。已在订阅中又收到 `on` = 1 时，周期不同就改用新周期（下一条在一个新周期之后），相同则不变。`on` = 0 时停止（`periodMs` 照样须在范围内，服务端不用它）。客户端可能在退订之后还收到一条已在路上的 `DeviceStatus`，忽略即可。
  - 生命周期：control 连接断开（含进入保留状态）与会话结束时服务端停止。续连后不自动恢复，客户端在每次进入串流（含续连）后按需要重发 `SubscribeStatus`。
  - 电量、充电与电池温度取系统电池广播（`Intent.ACTION_BATTERY_CHANGED`）的粘性数据：`batteryPercent` = `level × 100 / scale` 四舍五入，最大 100；`charging` 在状态为"正在充电"，或"已充满"且接着电源时为 1；`temperatureDeciC` 即广播里的 `temperature`（本来就以 0.1 °C 计）。拿不到广播、广播说没有电池（`present` 为假）、数值缺失或超出字段范围时，相应的项报未知，`charging` 为 0。
  - CPU 占用是两次采样之间全部 CPU 的占用：取 `/proc/stat` 第一行（`cpu` 合计）的 user、nice、system、idle、iowait、irq、softirq、steal（老内核缺的项不计），总时间为它们之和，空闲为 idle + iowait，`cpuPermille` = (Δ总 − Δ空闲) / Δ总，以千分之一四舍五入。订阅后的第一条没有上一次采样，以及读不到 `/proc/stat`、Δ总不为正或空闲时间倒退时报未知。
  - 不用 `dumpsys`（R5）。
- **接收端反馈与码率控制**（ADR-0051；adb 隧道与 WiFi 直连相同）：视频通道打开期间，客户端每 200 ms 发送一条 `ReceiverReport`，期间没有收到数据帧也照发；每条视频连接在收到第一个会话描述之前不发送（会话号 0 保留）。
  - `videoSession` 为统计所属的编码会话，会话变化时先发出上一会话的报告再重新累计；`intervalUs` 为本报告覆盖的客户端时长；`frames`、`bytes` 为期间收到的数据帧数与视频通道字节数（含包头）。
  - `delayMinUs` / `delayMeanUs` / `delayMaxUs` 为期间每个数据帧"收齐时刻（客户端单调时钟）−（`ptsUs` + `encodeDelayUs`）"的最小 / 平均 / 最大值。两端时钟差未知，只有差值有意义；期间没有数据帧时三者为 0。
  - `roundTripUs` 为最近一次 `Ping` / `Pong` 的往返时间（尚未测得为 0）；`lostPackets` / `recoveredPackets` 为 WiFi 直连期间丢失 / 经 FEC 恢复的包数（第 10 章），adb 隧道上为 0。
  - 服务端据此调整编码码率：`ConfigureVideo` / `SetVideoBitrate` 给出的码率是上限，实际码率可低于它。拥塞时服务端先丢弃可丢弃帧；积压超过上限时丢弃到下一个关键帧为止，所以客户端看到的帧序列可能出现间断，下一帧必为关键帧，不需要特别处理。

---

## 8. 错误码（u16）

| 码 | 名称 | 码 | 名称 |
|---|---|---|---|
| 0 | None | 8 | UnexpectedMessage |
| 1 | VersionMismatch | 9 | ChannelUnavailable |
| 2 | AuthenticationFailed | 10 | ResourceBusy |
| 3 | UnsupportedAndroid | 11 | EncoderFailure |
| 4 | MalformedFrame | 12 | PermissionDenied |
| 5 | FrameTooLarge | 13 | NotFound |
| 6 | UnknownType | 14 | Timeout |
| 7 | WrongDirection | 15 | InternalError |
| 16 | InjectionDenied | 17 | ClipboardDenied |
| 18 | AudioCaptureFailed | 19 | VideoReduced |
| 20 | VideoRestored | | |

按被拒的功能区分错误码，客户端据此结合机型给出可操作的提示（R6、ADR-0045）：16 = 设备拒绝输入注入（`INJECT_EVENTS`，部分厂商需另开开发者选项），17 = 设备拒绝剪贴板访问，18 = 音频采集未能开始或中途停止（例如 Android 11 要求开始采集时手机已解锁）；其他权限问题仍用 12。音频编码器失败用 11。音频的这两种失败以 `Notice` 上报，音频通道保持打开、只是不再有数据；客户端下次发送 `ConfigureAudio`（包括续连后的重发）时服务端重新尝试。

19、20 不是失败（ADR-0054）：链路比编码器能压到的最低输出还窄时，服务端降低视频的分辨率或帧率，每降一级发一条警告级的 `Notice(VideoReduced)`；逐级恢复到用户设定时发一条信息级的 `Notice(VideoRestored)`。`message` 是给日志看的英文说明，客户端按错误码显示本地化的轻提示，当前帧尺寸照常来自会话描述。

**解码错误**（实现内部与测试向量使用，不上线；检查顺序见 3.3 节）：`Truncated`、`TrailingBytes`、`LengthExceeded`、`InvalidEnum`、`InvalidBool`、`MalformedUtf8`、`ReservedBitsSet`、`OutOfRange`、`BadMagic`、`UnknownType`、`WrongDirection`、`UnexpectedType`、`FrameTooLarge`。

---

## 9. 版本

- 当前协议版本：**1**。
- 版本不一致：服务端以 `HandshakeError(VersionMismatch)` 拒绝，消息中包含双方版本号。

## 10. WiFi 直连传输（M5，ADR-0005、0019、0020、0031、0051、0052）

> 状态：10.4 节（Reed-Solomon 纠删码）已定稿并实现，由金标准向量 `vectors/fec/reed-solomon.json` 锁定（WIF-002）；10.2 节（数据报与握手）格式已定，数据报头、证明与控制标签由金标准向量 `vectors/direct/datagrams.json` 锁定，握手两侧的实现随 WIF-006 落地；10.3 节（可靠通道）已定稿，数据报格式由 `vectors/direct/reliable.json` 锁定（WIF-004）；10.1 节的引导与结束随两侧传输实现定稿；10.5～10.7 节（媒体通道的打开与关闭、视频切片、音频、反馈）已定稿，格式由 `vectors/direct/media.json` 锁定（WIF-001；"已关闭"随 ADR-0056 补充）。

### 10.1 概览（定稿）

- 引导：仍经 adb（USB 或无线 adb）推送并启动服务端，令牌照旧经标准输入传入（2.1 节）；启动参数增加 `transport=udp`，可选 `udpPort=<端口>`（默认 0，由系统挑一个空闲端口）。服务端绑定 UDP 端口后，就绪信号写为 `GS_READY <协议版本> udp=<端口>[ ip=<IPv4 地址>[,<IPv4 地址>…]]`：`ip=` 列出手机当前开着的网络接口上的 IPv4 地址（不含回环与链路本地地址，WLAN 接口的排在前面），没有时省略。adb 隧道照常监听，两条传输上来的请求由同一个会话处理（客户端选了"自动"时不必重启服务端即可改走隧道）。
- 结束：第 7 章"发出 `Goodbye` 后读到对端关闭"在直连时改为"读到 `Goodbye` 被确认"：UDP 没有关闭，可靠通道的确认已保证对方收到。会话结束后，服务端的这个连接继续发送尚未确认的数据、并确认客户端最后发来的数据，最多 1 秒，然后丢弃。
- 客户端从无线 adb 的设备地址取得手机 IP；序列号里没有地址时（经 mDNS 名字连接的无线调试，如 `adb-<编号>._adb-tls-connect._tcp`）取就绪信号 `ip=` 的第一个地址。向该 IP 的 UDP 端口发起握手；1.5 秒内未完成即失败并报告原因。传输方式由用户选定，选了直连就只走直连；只有用户选了"自动"，客户端才在直连失败后改走 adb 隧道（ADR-0055）。
- 一个 UDP 套接字承载 2.2 节的四个通道：control 与 bulk 走自研的可靠通道（两条独立的流，10.3 节）；video 切片后加 RS 纠删码；audio 每个数据报附带上一包。
- 上层格式不变：control / bulk 在可靠通道之上传输 3.1 节的消息帧，video / audio 传输 3.2 节的媒体包（切片后由接收端重组）。

### 10.2 数据报、握手与认证（定稿，WIF-006）

接收端接受不超过 1280 字节的 UDP 数据报；发送端把数据报控制在 1200 字节以内（与 QUIC 相同：IPv6 最小链路 MTU 1280 减去 IP 与 UDP 头后仍有余量，局域网常见的 1500 更不必说）。每个数据报以 8 字节的数据报头开始：

| 偏移 | 字段 | 类型 | 说明 |
|---|---|---|---|
| 0 | magic | u16 | `0x4753`（"GS"） |
| 2 | channel | u8 | 2.2 节的通道编号（0 control、1 video、2 audio、3 bulk），15 = 握手 |
| 3 | flags | u8 | 保留为 0 |
| 4 | connection | u32 | 握手完成前为 0；之后为服务端分配的连接号（随机，非 0） |
| 8 | payload | bytes | 按通道解释 |

- 握手（channel 15，负载恰好是 3.1 节的一个完整消息帧，必须装进一个数据报；帧后多出字节按 `TrailingBytes` 处理）：
  1. 客户端发负载为空的"敲门"数据报；服务端回 `ServerHello`。服务端为每个来源地址保留一个 `nonce`，自生成起 5 秒内有效：有效期内同一来源再敲门（客户端重发）得到同一个 `nonce`，过期后的敲门才生成新的。这样即使先前的 `ServerHello` 迟到，客户端回应哪一个，证明都与服务端保存的一致。
  2. 客户端回 `ClientHello`，证明的上下文为 ASCII `"GSCP/1 udp hello"`（与 2.4 节的 TCP 上下文不同，两种传输的证明不能互相重放）。
  3. 服务端校验通过后分配连接号，绑定客户端的来源地址与端口，回一个负载为空、头部带新连接号的握手数据报（"接受"）；`Welcome` 随后作为 control 可靠通道（10.3 节）上的第一条消息发出（续连时 `resumed` = 1，绑定换成新的来源）。`Welcome` 不放进握手数据报，因为它可能超过一个数据报（16 个显示器、32 个编码器时约 4.8 KB），由可靠通道分片与重传。同一来源以同一 `nonce` 重发的 `ClientHello` 得到同一个"接受"（连接号不变）。校验失败回 `HandshakeError`，不绑定。
  4. 客户端每 250 ms 重发尚未得到应答的敲门或 `ClientHello`；1.5 秒内未完成即放弃，握手失败（是否改走 adb 隧道见 10.1 节）。客户端只接受来自服务端地址、头部合法的握手数据报，其余静默忽略（伪造的数据报不应打断握手）；收到 `nonce` 不同的 `ServerHello`（服务端重启或旧 `nonce` 过期）时改用新的 `nonce` 重新证明。收到"接受"后停止重发；control 通道上的第一条消息必须是 `Welcome`（按握手上下文解码），握手到此完成，1.5 秒的时限包括它。
- 绑定之后，来源地址或连接号不符的数据报一律静默丢弃（ADR-0020）。握手数据报在绑定后只接受续连。
- control 数据报（10.3 节）在可靠通道头中带 `counter u64`（每个方向从 1 起严格递增），末尾附 16 字节标签：HMAC-SHA256(token, ASCII `"GSCP/1 udp control"` ‖ 本数据报标签之前的全部字节) 的前 16 字节。接收端用常量时间比较标签，并按宽 64 的滑动窗口防重放：计数器落在窗口之外或已经见过即丢弃，可以容忍乱序。
- 媒体（video、audio）与 bulk 不加密、不附标签，只校验来源与连接号。
- 数据报头的解码检查顺序（两侧一致）：不足 8 字节 → `Truncated`；`magic` 不符 → `BadMagic`；`channel` 不是 0～3 或 15 → `InvalidEnum`；`flags` 非 0 → `ReservedBitsSet`；整个数据报超过 1280 字节 → `FrameTooLarge`。
- 金标准向量 `vectors/direct/datagrams.json`：数据报头的编解码与上述错误、`"GSCP/1 udp hello"` 上下文的证明（并给出同一令牌与随机数下 2.4 节的 TCP 证明，两者必须不同）、敲门 / `ServerHello` / `ClientHello` / "接受"四个握手数据报的完整字节，以及控制数据报标签。

### 10.3 可靠通道（定稿，ADR-0052）

control（通道 0）与 bulk（通道 3）各是一条双向的可靠字节流：发送端把要发的字节切成"分段"，接收端按分段序号重组出原来的字节流，再按 3.1 节的帧头从中取出消息帧（一个分段可以含多帧，一帧也可以跨多个分段，所以不需要另外的分片标记）。两条流、每条流的两个方向各有自己的序号空间，互不阻塞。数据报头之后的格式：

| 字段 | 类型 | 说明 |
|---|---|---|
| counter | u64 | 只在 control：10.2 节的防重放计数器，本方向从 1 起每个数据报加 1 |
| ack | u32 | 累计确认：对方序号小于 `ack` 的分段都已收到（即期望的下一个序号） |
| sack | u32 | 选择确认：第 i 位（最低位为第 0 位）表示序号 `ack + 1 + i` 的分段已收到 |
| count | u8 | 其后的分段数；0 为纯确认 |
| segments | 分段 × count | 每个分段：`seq u32`、`length u16`（≥ 1）、`length` 字节数据 |
| tag | 16 字节 | 只在 control：10.2 节的标签，覆盖它之前的全部字节 |

- 分段序号每个方向从 0 开始，每个新分段加 1，一个连接内不回绕；续连得到新连接号后两条流都从 0 重新开始。同一数据报里的分段可以是任意顺序（发送端按序号升序放）。
- 发送端的数据报不超过 1200 字节（接收端仍按 10.2 节接受到 1280 字节）：只含一个分段时，control 的分段数据最多 1153 字节，bulk 最多 1177 字节。
- 解码检查顺序（两侧一致）：
  - control：总长不足 41 字节（数据报头 8、counter 8、ack / sack / count 9、标签 16）→ `Truncated`；标签不符 → 丢弃（向量里记为 `BadTag`）；`counter` 为 0 → `OutOfRange`；然后按防重放窗口检查 `counter`（有状态，不在向量里），再解析确认与分段。先验标签，未经认证的字节不进入解析。
  - bulk：总长不足 17 字节 → `Truncated`，再解析确认与分段。
  - 分段：剩余字节放不下分段头或其数据 → `Truncated`；`length` 为 0 → `OutOfRange`；`count` 个分段之后还有字节（control 指标签之前）→ `TrailingBytes`。
  - 通过认证的 control 数据报解码失败，说明对端违反协议，结束会话；bulk 解码失败只丢弃该数据报（它没有认证，可能是伪造的）。
- 接收：序号小于 `ack` 的分段是重复的，丢弃；落在 `[ack, ack + 32]` 内的暂存（已有的丢弃）；更远的丢弃（发送端受在途上限约束，不会发出）。`ack` 处的分段一到，连同其后连续暂存的分段按序交给上层。缓冲不下时可以不收新分段（不确认），发送端会超时重发。
- 确认：收到新分段后最迟 5 ms 发出确认，期间若有本流的数据报发出则已捎带；收到的分段使 `ack` 之后出现缺口，或一个数据报里的分段全是重复的（对方在重发，说明确认丢了），立即确认。纯确认的数据报本身不被确认。
- 发送：
  - 在途上限：发送端只发出序号小于"对方累计确认 + 32"的分段，其余在发送端排队；这保证在途的分段都落在对方能确认的范围内（选择确认过的分段也算在内）。
  - control 有数据就立即发出，不等凑包；数据报除新分段外，再顺带最多 3 个最近的、尚未确认的较早分段（放得下才放）：单个数据报丢失时，接收端从下一个数据报直接拿到，不必等重发。bulk 不顺带。
  - 丢失判定：确认表明某个分段已到，而另一个分段仍未确认、且其最后一次发出早于前者的最后一次发出，后者即判为丢失，立即重发，不等超时。
  - 超时重发：往返时间按 RFC 6298 平滑（SRTT、RTTVAR）；每个推进确认的数据报取其中新确认的、序号最大的分段作样本，从它第一次发出算起，因丢失判定或超时而重发过的分段不取样（顺带重发不算）。RTO = max(20 ms, SRTT + 4 × RTTVAR)，还没有样本时为 100 ms。最早的未确认分段自最后一次发出起超过 RTO 仍未确认就重发它；连续超时每次 RTO 加倍，最多 1 秒，确认推进后恢复。对端长时间无响应由会话的心跳超时处理（第 7 章），可靠通道本身不放弃。
- 金标准向量 `vectors/direct/reliable.json`：control 与 bulk 数据报（纯确认、带顺带重发的多分段、最大长度）的完整字节与各字段，以及畸形数据报和它们的错误；control 数据报的标签与 10.2 节的计算一致。

### 10.4 Reed-Solomon 纠删码（定稿）

- 有限域 GF(2^8)，本原多项式 x^8 + x^4 + x^3 + x^2 + 1（0x11D）。加法即异或；乘法与求逆借助以 2 为生成元的对数 / 指数表。
- 一个编码组由 k 个数据片与 m 个校验片组成：1 ≤ k，0 ≤ m，k + m ≤ 256。组内各片长度同为 L：数据片不足 L 时在末尾补 0（真实长度由切片头携带，见 10.5 节）。
- 系统码：数据片原样发送。校验片 i（0 ≤ i < m）的第 b 个字节 = Σ C[i][j] · D_j[b]（j = 0…k−1），其中 C[i][j] = (x_i ⊕ y_j)^−1，y_j = j，x_i = k + i（Cauchy 矩阵；各 x、y 两两不同，所以 x_i ⊕ y_j ≠ 0）。
- 生成矩阵为单位阵 I_k 叠在 C 之上。Cauchy 矩阵的任意方子阵都可逆，所以收到 k + m 片中的任意 k 片，即可恢复全部数据片：取这 k 片对应的生成矩阵行组成方阵，在 GF(2^8) 上求逆，再乘以收到的片。
- 金标准向量 `vectors/fec/reed-solomon.json`：给定 k、m 与数据片，两侧算出的校验片逐字节一致；给定丢失的片号，两侧恢复出的数据片逐字节一致。

### 10.5 媒体通道的打开与视频切片（定稿，WIF-001）

打开（video 与 audio 相同，取代 2.3 节的 `ChannelOpen`）：客户端在该通道上发一个负载为空的数据报（"打开"）；会话接受这个通道时，服务端在同一通道回一个负载为空的数据报（"已打开"），之后才在这个通道上发媒体，重复的"打开"得到同样的回复。客户端每 250 ms 重发尚未得到回复的"打开"，1.5 秒仍无回复即视为通道被拒。来源绑定与连接号已经认证了客户端（10.2 节），所以不再需要证明。

关闭（ADR-0056）：连接存续期间服务端关闭一个媒体通道时（例如视频推流放弃、要客户端重开；会话结束、连接断开或被接替时不发），在该通道上发一个负载为 1 字节 `0` 的数据报（"已关闭"），此后这个通道不再有媒体；每 250 ms 重发，直到客户端在同一通道回同样的数据报（确认），最多 1.5 秒。客户端每收到一个"已关闭"就回一个确认，交付完已经完整的包后结束该通道，之后与 adb 隧道上媒体连接被关闭时相同处理。服务端仍在重发"已关闭"时又收到"打开"，停止重发并当作新的打开。

通道 1、2 上负载 0 字节的数据报是"打开" / "已打开"，1 字节且为 0 的是"已关闭" / 确认，其余 1 字节的丢弃；更长的按下面的视频片或 10.6 节的音频解码。

视频（channel 1）：每个媒体包（3.2 节，含包头，下称"包"）切成等长的数据片，每 64 个数据片为一组，每组加 RS 校验片（10.4 节）；一片一个数据报，片头在数据报头之后：

| 字段 | 类型 | 说明 |
|---|---|---|
| packet | u32 | 包序号：本通道每个包加 1，从 0 开始，一个连接内不回绕 |
| packetSize | u32 | 包的字节数（含 3.2 节的包头），1 ～ 16 MiB + 20 |
| shardSize | u16 | 片长 L，≥ 1 |
| group | u16 | 组号，从 0 开始 |
| index | u8 | 组内片号：0 ～ k−1 为数据片，k ～ k+m−1 为校验片 |
| k | u8 | 本组数据片数 |
| m | u8 | 本组校验片数，可以为 0 |
| reserved | u8 | 保留为 0 |
| bytes | | 片的内容 |

- 切法（两侧一致）：数据片 j 是包的 `[j·L, min((j+1)·L, packetSize))` 字节，共 N = ⌈packetSize / L⌉ 片；组 g 含数据片 64g ～ min(64g+64, N)−1，即 k = min(64, N − 64g)。只有包的最后一个数据片可以短于 L；校验片按 10.4 节计算，组内各数据片先在末尾补 0 到 L，校验片长度总是 L。发送端的数据报不超过 1200 字节，L 最大 1176；推荐 L = ⌈packetSize / ⌈packetSize / 1176⌉⌉，各片等长。m 由发送端按丢包反馈选（10.7 节），k + m ≤ 256。
- 解码检查顺序（两侧一致）：数据报不足 24 字节 → `Truncated`；`reserved` 非 0 → `ReservedBitsSet`；`packetSize` 为 0 或超过 16 MiB + 20、`shardSize` 为 0、`group` ≥ ⌈N / 64⌉、`k` ≠ min(64, N − 64·group)、k + m > 256、`index` ≥ k + m → `OutOfRange`；内容短于应有长度（数据片为 min(L, packetSize − (64·group + index)·L)，校验片为 L）→ `Truncated`，长于 → `TrailingBytes`。不合格的视频数据报一律丢弃（媒体不认证）。
- 接收：某组收到任意 k 片即可恢复它的数据片；一个包的所有组都恢复后，按 `packetSize` 截去末尾的补 0 交给上层。包按序号交付：较后的包已完整而较早的仍不完整时，较早的判为无法恢复并丢弃，下一个交付的包附带"此前丢失的包数"。
- 无法恢复的包：客户端丢弃此后同一编码会话的数据帧，直到关键帧，并发 `RequestKeyFrame`（WIF-003）。

### 10.6 音频（定稿）

- audio（channel 2）数据报在数据报头之后为：`packet u32`（本数据报里第一个包的序号，本通道每个包加 1，从 0 开始）、`count u8`（1 或 2）、`count` 个 `size u16` + 包（3.2 节，含包头）。第二个包（若有）是序号 `packet − 1` 的上一个包的副本：丢一个数据报不丢声音（ADR-0019）。当前包与上一个包放不进 1200 字节时只带当前包。
- 解码检查顺序：数据报不足 13 字节 → `Truncated`；`count` 不是 1 或 2、`count` 为 2 而 `packet` 为 0、某个 `size` 为 0 → `OutOfRange`；放不下 `size` 或包 → `Truncated`；多出字节 → `TrailingBytes`。接收端按序号去重、按序交付，缺失的包同样计入"此前丢失的包数"。

### 10.7 反馈（定稿）

- 沿用第 7 章的 `ReceiverReport`：`lostPackets` 为期间按组结算时缺少的视频片数（数据片与校验片都算，含所在包被判为无法恢复的），`recoveredPackets` 为期间经纠删重建的数据片数；服务端据此与它这期间发出的片数算出丢片率，同时调整码率（ADR-0051）与每组的校验片数（ADR-0019）。

## 11. 伴侣 App 连接（ADR-0086、ADR-0023 修订）

> 状态：2026-11-02 补充（CMP-001～004）。四条消息的格式由金标准向量 `vectors/companion.json` 与 `vectors/malformed.json` 锁定。2026-11-06 补充第二批（ADR-0104，CMP-005～007）：输入法与键位提示的五条消息（0x0603～0x0606、0x8603）与局域网连接（11.7 节，握手、密钥与加密记录由 `vectors/companion/lan.json` 锁定）；仍在发布前，不升版本号。同日补充截图（ADR-0105 第 5 节，HID-008）：三条消息（0x0607、0x8604、0x8605）与 11.8 节，向量在 `vectors/companion.json` 与 `vectors/malformed.json`。2026-11-11 补充版本 2（ADR-0112 第 5.2 节）：版本协商（11.2 节）、两条消息（0x0608、0x8606）与设置同步（11.9 节），向量同样在 `vectors/companion.json` 与 `vectors/malformed.json`。

伴侣 App（包名 `com.gamescrcpy.companion`）是装在手机上的普通应用，与服务端无关：它自己起前台服务、自己监听，电脑经 adb 转发直接连它。第一批只有手机上的光标；第二批加上输入法（识别输入框、接收电脑打的字）、手机上的键位提示，以及没有 adb 时经局域网扫码连接（11.7 节）；没有画面时编辑键位用的手机截图见 11.8 节。

### 11.1 启动与就绪

- 客户端每次启动生成 16 字节随机令牌，以 32 位小写十六进制经 `am start-foreground-service -n com.gamescrcpy.companion/.CompanionService --es token <令牌>` 启动前台服务（Android 14 起类型为 `specialUse`）。令牌不是 32 位十六进制时服务直接停止。再次启动时新令牌取代旧令牌，已通过认证的连接不受影响。
- 就绪：启动命令返回后，客户端执行一次 `am broadcast -n com.gamescrcpy.companion/.ReadyReceiver`。服务已在监听时广播结果码为 1（`am` 输出 `Broadcast completed: result=1`），否则为 0，客户端据此报告启动失败，不轮询。两条命令按顺序到达 App 的主线程，服务在处理启动命令时同步开始监听，所以查一次即可。
- 服务与就绪广播只接受持有 `android.permission.DUMP` 的调用方：adb shell 有这个权限，普通应用拿不到。
- 服务启动后 15 秒内没有通过认证的连接，或通过认证的连接断开后 15 秒内没有新的，服务自行停止。

### 11.2 连接与开局

1. 服务在抽象命名空间 socket `gamescrcpy-companion` 上监听；客户端经 `adb forward tcp:0 localabstract:gamescrcpy-companion` 连接。帧格式同 3.1 节，上下文为"伴侣连接"（负载上限 16 KiB，允许的类型见 3.1 节的表）。没有 `ServerHello`。
2. 客户端连上后先发 `CompanionHello`，`version` 是客户端会说的最高版本（当前为 2）。伴侣 5 秒内没收到它即关闭连接。
3. `version` 不为 0 且令牌与当前令牌相同（常量时间比较）：伴侣回 `CompanionWelcome`，其中 `version` 取客户端的 `version` 与伴侣自己会说的最高版本（当前为 2）中较小的一个，这就是这条连接用的版本；连接进入工作状态。版本 1 的连接上不出现 0x0608、0x8606，收到即按 `UnexpectedMessage` 处理（伴侣照第 4 步回 `Goodbye`，电脑结束连接）。
4. 否则伴侣回 `Goodbye`（0x8402）：令牌不符为 `AuthenticationFailed`；`version` 为 0 时为 `VersionMismatch`，`message` 写明双方版本；开局前收到其他消息为 `UnexpectedMessage`；帧解码失败按第 7 章的对应错误码（`FrameTooLarge`、`UnknownType`、`WrongDirection`、`UnexpectedMessage`，其余为 `MalformedFrame`）。之后照第 7 章"结束"读到对端关闭（最多 5 秒）再关闭。工作状态下的解码失败同样处理。只会版本 1 的伴侣（2.0.0）对 `version` 不是 1 的 `CompanionHello` 也回 `VersionMismatch`。
5. 伴侣同一时间只服务一条通过认证的连接：新连接认证通过后，伴侣直接关闭旧连接（客户端断线重连时旧连接可能还没被察觉）。
6. 客户端以高于 1 的版本开局而伴侣回 `VersionMismatch` 时，伴侣只会版本 1：经 adb 的连接关掉这条，在新连接上用同一令牌以版本 1 重新开局；局域网上（11.7 节）电脑记住这台手机（握手里的 `phone`）只会版本 1，伴侣自己重连时以版本 1 开局，记到程序退出。`CompanionWelcome.version` 为 0 或大于客户端发出的版本时，客户端当作开局失败。

### 11.3 消息（分区 `0x06`）

| 类型 | 名称 | 方向 | 负载 |
|---|---|---|---|
| 0x0601 | CompanionHello | 电脑 → 伴侣 | `version u16`（客户端会说的最高版本，当前为 2，见 11.2 节）· `token blob8（≤ 16）`（客户端总是发 16 字节；长度不对等同令牌不符） |
| 0x0602 | CursorState | 电脑 → 伴侣 | `x u16` · `y u16` · `flags u8`（位 0 = `visible` 显示；位 1 = `pressed` 按下；其余保留） |
| 0x8601 | CompanionWelcome | 伴侣 → 电脑 | `version u16` · `overlay u8`（0 = `granted` 已授权，1 = `noPermission` 没有悬浮窗权限）· `width u16` · `height u16` · `rotation u8（0–3）` |
| 0x8602 | OverlayChanged | 伴侣 → 电脑 | `overlay u8` · `width u16` · `height u16` · `rotation u8（0–3）` |
| 0x0603 | CommitText | 电脑 → 伴侣 | `text str16（≤ 4096）` |
| 0x0604 | EditorAction | 电脑 → 伴侣 | `action u8`（`ImeAction`，见下） |
| 0x0605 | KeyHintLayout | 电脑 → 伴侣 | `count u8（≤ 192）` · `count × KeyHint`；**KeyHint** = `shape u8`（`KeyHintShape`，见下）· `x u16` · `y u16` · `width u16` · `height u16` · `label str8（≤ 32）` |
| 0x0606 | KeyHintState | 电脑 → 伴侣 | `flags u8`（位 0 = `shown` 显示；位 1 = `faded` 淡化；其余保留）· `opacity u8（0–100）` |
| 0x8603 | InputBoxChanged | 伴侣 → 电脑 | `flags u8`（位 0 = `shown` 弹出；位 1 = `multiLine` 多行；其余保留）· `inputType u32` · `action u8`（`ImeAction`） |
| 0x0607 | ScreenshotRequest | 电脑 → 伴侣 | `requestId u32` · `maxSide u16`（截图的长边最多这么多像素，不放大；0 = 伴侣默认的 1920）· `quality u8（1–100）`（JPEG 质量） |
| 0x8604 | ScreenshotChunk | 伴侣 → 电脑 | `requestId u32` · `index u16` · `total u16（1–274）` · `data blob16（≤ 15360）`；`index` 必须小于 `total` |
| 0x8605 | ScreenshotFailed | 伴侣 → 电脑 | `requestId u32` · `reason u8`（`ScreenshotProblem`，见下）· `message str16（≤ 1024）`（给日志看的英文说明，可为空） |
| 0x0608 | CompanionSettings | 电脑 → 伴侣（版本 2） | `computer bytes[16]` · `name str8（≤ 64）` · `link u8`（`ComputerLink`，见下）· `accent u32`（`0x00RRGGBB`，高 8 位保留为 0）· `applied u32` · `cursor bool` · `hints bool` · `hintOpacity u8（10–100）` · `ime bool` · `autoConnect bool`（11.9 节） |
| 0x8606 | CompanionSettingChange | 伴侣 → 电脑（版本 2） | `change u32`（不为 0）· `item u8`（`CompanionItem`，见下）· `value u8`（开关项为 0 或 1，`hintOpacity` 为 10–100）（11.9 节） |

伴侣连接里另外沿用 `Ping`（0x0401）、`Goodbye`（0x0402）、`Pong`（0x8401）与 `Goodbye`（0x8402）。

- **`ImeAction`**（u8，与 Android `EditorInfo.IME_ACTION_*` 同值）：0 `unspecified`、1 `none`、2 `go`、3 `search`、4 `send`、5 `next`、6 `done`、7 `previous`。
- **`ScreenshotProblem`**（u8）：0 `refused` 用户在手机上拒绝了录屏授权；1 `unsupported` 手机做不了截图（系统不让这个应用录屏、或没有可截的画面）；2 `busy` 上一张还没截完；3 `error` 其它错误（`message` 写明）。
- **`KeyHintShape`**（u8，照投屏窗口键位提示的形状，ADR-0077、设计系统 KeyHint）：0 `tap` 圆标、1 `hold` 按住（外圈整圈）、2 `longPress` 长按（外圈进度弧）、3 `turbo` 连发（右上残影）、4 `camera` 视角（准星短线）、5 `aim` 技能施法（外圈虚线）、6 `freeLook` 自由视角（眼形）、7 `swipe` 滑动（向右的虚线箭头）、8 `key` 小方键帽（摇杆环上的方向键、环里的其余键）、9 `ring` 摇杆或方向轮盘的圆环（没有文字）、10 `unbound` 没填键（虚线圈，写组件名）。
- **`ComputerLink`**（u8）：0 `usb` 经 USB 调试的 adb；1 `wireless` 经无线调试的 adb；2 `lan` 局域网直连（11.7 节）。
- **`CompanionItem`**（u8，括号里是电脑上这台手机的设置）：0 `cursor` 手机上的光标（`companion.cursor`）；1 `hints` 手机上的键位提示（`companion.hints`）；2 `hintOpacity` 手机上的键位提示淡化后的不透明度，10–100（`companion.hintOpacity`）；3 `ime` 用电脑键盘打字（`companion.ime`）；4 `autoConnect` 自动连接（`companion.autoConnect`）。

### 11.4 语义

- **坐标**：`x`、`y` 是相对手机主显示器整个画面（当前方向）的归一化定点数，含义同 1.2 节（0 = 左 / 上边缘，65535 = 右 / 下边缘）；客户端由投屏画面上的指针位置换算。投屏画面不是主显示器（独立显示器，第 7 章）时客户端不显示光标。
- **映射**：伴侣把归一化坐标换算到主显示器当前方向的像素：`px = x · W / 65535`（`W` 为显示器当前方向的宽度），夹到 `[0, W − 1]`，再减去覆盖视图在屏幕上的原点并夹到视图之内（`y` 同理）。覆盖层铺满整个屏幕并延伸到刘海区，这时 `W` 就是覆盖视图自身的宽度、原点为 0，所以横竖屏、旋转、刘海机型都不偏移；系统没让覆盖层铺满时（例如 Android 9、10 横屏时刘海所在的长边）减去原点后同样不偏移。
- **最新值**：客户端只发最新的光标状态，最多每秒 120 条，与上次发出的相同则不发；伴侣读线程只保留最新值，在下一个 Choreographer 帧里平移光标，不重新布局。
- **隐藏**：指针离开画面、锁定视角、投屏窗口不在前台时，客户端发 `visible` = 0。连接结束时伴侣隐藏光标。
- **覆盖层信息**：`width`、`height` 为覆盖视图当前的像素尺寸（没有悬浮窗权限或尚未铺好时为 0），`rotation` 为主显示器旋转的 90° 份数。尺寸、方向或授权变化时伴侣发 `OverlayChanged`，与上次发出的完全相同则不发。
- **心跳**：同第 7 章：客户端至少每 2 秒发一次 `Ping`，伴侣回 `Pong`（`serverTimeUs` 为手机单调时钟）；伴侣 6 秒内没收到任何帧即关闭连接；客户端 6 秒内没收到 `Pong` 即判定连接失效。
- **结束**：客户端发 `Goodbye`（0x0402）表示不再需要伴侣：伴侣隐藏光标、关闭连接并停止服务。用户在手机通知栏点"停止"时伴侣发 `Goodbye`（0x8402，`None`）后停止服务，客户端不再重连。发送 `Goodbye` 的一方照第 7 章读到对端关闭再关闭。
- **输入框**（`InputBoxChanged`）：伴侣的输入法开始显示输入视图（`onStartInputView`，输入类型不是 `TYPE_NULL`）时发 `shown` = 1，带输入框的 `inputType`（Android `EditorInfo.inputType` 原值）、`action`（`imeOptions & IME_MASK_ACTION`，不在 0～7 时按 `unspecified`）与 `multiLine`（文字类输入框带 `TYPE_TEXT_FLAG_MULTI_LINE`）；输入视图收起或输入结束（`onFinishInputView`、`onFinishInput`）时发 `shown` = 0，其余字段为 0。连接进入工作状态时已有弹出的输入框，伴侣紧接 `CompanionWelcome` 发一次；之后变化才发，与上次发出的相同不发（客户端在每条新连接开始时都当作没有输入框）。伴侣的输入法不是当前输入法时一直是 `shown` = 0。
- **伴侣的输入法**：输入视图是屏幕底部一条细提示"正在用电脑键盘打字"，点它立即换回上一个输入法（`switchToPreviousInputMethod()`）；不全屏、没有候选栏；手机认为接着硬件键盘（如 AOA 键盘）时照样显示这条提示。与电脑的连接断开（或从未连上）5 秒后，输入法自己换回上一个输入法（没有上一个时换到下一个），手机不会没有键盘可用。
- **文字**（`CommitText`）：伴侣把文字提交到当前输入框的光标处（`commitText(text, 1)`）；没有输入框时丢弃。客户端按 UTF-8 4096 字节分段，不切开字符；同一连接上的 `CommitText` 与 `EditorAction` 按发送顺序执行。
- **回车动作**（`EditorAction`）：`unspecified` = 软键盘的回车键在这个输入框里做的事（输入框的默认动作；多行或标了 `IME_FLAG_NO_ENTER_ACTION` 的输入框换行，即 `sendKeyChar('\n')`）；`none` = 发送回车键（`KEYCODE_ENTER` 按下与抬起）；2～7 = `performEditorAction(action)`。没有输入框时丢弃。
- **键位提示**（`KeyHintLayout`）：每条取代整份列表。`x`、`y` 是提示中心相对当前画面的归一化定点数（同 1.2 节）；`ring` 的 `width`、`height` 是圆环直径占画面宽、高的比例（同样乘 65535），其余形状为 0（键帽大小由伴侣按屏幕密度画）。`label` 是电脑上显示的键名（已按电脑的界面语言与键盘布局写好），超过 32 字节在字符边界截断。客户端最多发 192 个，多出的不发。伴侣照投屏窗口的样子画（设计系统 KeyHint：黑 66% 底、白 22% 边、白字；圆环白 28% 边、黑 14% 底），坐标照上面"映射"换算。
- **键位提示的显示**（`KeyHintState`）：`shown` = 0 时不画；`faded` = 1 时以 `opacity`%（程序设置里的键位提示透明度）显示，0 时完整显示；变化时伴侣用短动画过渡。连接后收到第一条 `KeyHintState` 之前不画。客户端连接后先发 `KeyHintLayout` 再发 `KeyHintState`，之后各自只在变化时发。锁定视角时客户端只隐藏光标（`CursorState.visible` = 0），键位提示照常。

### 11.5 安装、授权与更新（客户端行为）

- 安装：没装时先在投屏窗口里提示，用户点"安装"后推送程序旁的 `gs-companion.apk` 到 `/data/local/tmp/gs-companion.apk`，`pm install -r` 安装（输出含 `Success` 为成功；部分手机会在手机上弹确认，客户端一直等到命令结束），然后删掉推送的文件。
- 授权：`appops set com.gamescrcpy.companion SYSTEM_ALERT_WINDOW allow`，再用 `appops get com.gamescrcpy.companion SYSTEM_ALERT_WINDOW` 确认结果含 `SYSTEM_ALERT_WINDOW: allow`。确认不了、或 `CompanionWelcome` 报告没有悬浮窗权限时，执行 `am start -a android.settings.action.MANAGE_OVERLAY_PERMISSION -d package:com.gamescrcpy.companion` 打开手机上的权限页（每条连接最多一次），并在电脑上说明要打开哪个开关；伴侣照常启动，用户打开开关后以 `OverlayChanged` 报告。Android 13 起另以 `pm grant com.gamescrcpy.companion android.permission.POST_NOTIFICATIONS` 让前台服务的通知可见，失败不影响使用。
- 更新：`pm list packages --show-versioncode com.gamescrcpy.companion` 给出已装的版本号（`package:com.gamescrcpy.companion versionCode:<N>`，只认包名完全相同的一行），低于随程序带的版本号（程序旁 `gs-companion.json`，即 Gradle 在安装包旁写出的 `output-metadata.json`，取 `elements[0].versionCode`）时重新安装，不再询问。读不到随程序带的版本号时只在没装时安装。不用 `dumpsys`（R5）。
- 停止：客户端发 `Goodbye` 后，再执行 `am stop-service -n com.gamescrcpy.companion/.CompanionService` 并拆掉转发。

### 11.6 输入法（客户端行为）

- 伴侣的输入法编号为 `com.gamescrcpy.companion/.CompanionIme`。设置 `companion.ime`（每台手机，默认开）打开、经 adb 的伴侣连接进入工作状态后，客户端：
  1. `settings get secure default_input_method` 记下原来的输入法；结果是伴侣自己或 `null` 时，改记 `ime list -s` 列出的第一个不是伴侣的输入法（没有就不记）；
  2. `ime enable com.gamescrcpy.companion/.CompanionIme`，输出含 `now enabled` 或 `already enabled` 为成功；
  3. `ime set com.gamescrcpy.companion/.CompanionIme`，输出含 `selected` 且不含 `Unknown`、`cannot`、`Error` 为成功。
- 第 2、3 步不成功（手机不允许经 USB 调试启用或切换输入法）时，执行 `am start -a android.settings.INPUT_METHOD_SETTINGS` 打开手机的输入法设置页（每条连接最多一次），电脑上说明要打开哪个开关并给"再试一次"。
- 换回：连接结束（客户端发 `Goodbye`、断开、失效）或设置被关掉时，当前输入法（`settings get secure default_input_method`）仍是伴侣才执行 `ime set <原来的输入法>`；用户中途自己换了输入法就不动。手机已经断开时这一步做不了，由伴侣的输入法自己换回（11.4 节）。
- 只有伴侣报告的输入框是弹出状态（`InputBoxChanged.shown` = 1）时，客户端把打的字经 `CommitText`、不带修饰键的回车经 `EditorAction(unspecified)` 发给伴侣；其余按键照旧经服务端注入（5.1 节 `Key`），没有伴侣输入框时文字照旧经 `Text`。不用 `dumpsys`（R5）。

### 11.7 局域网连接（没有 adb 时，ADR-0104 第 6 项）

没有 adb（如只有 AOA 的手机）时，伴侣经局域网连电脑：电脑在无画面的控制窗口里显示二维码，伴侣扫码配对；之后双方记住对方，同一 Wi-Fi 下伴侣自己重连。连接上的消息与 11.2～11.4 节相同，外面加一层 AES-256-GCM。

**二维码**：`http://<电脑的局域网 IPv4>:<端口>/c/<令牌>#k=<密钥>`。
- 令牌：16 个随机字节的 base64url（不补 `=`，22 个字符），每次显示二维码时新生成，配对成功一次即作废。
- 密钥：32 个随机字节（AES-256），base64url 不补 `=`（43 个字符），放在 `#` 后面，浏览器打开网址时不会随请求发出。
- 端口：电脑优先用 27185，被占用时用系统给的空闲端口；同一个端口同时收 HTTP 与伴侣连接。电脑只在有控制窗口显示二维码或在等配对过的手机重连时监听。

**HTTP**（手机自带的扫码打开网址时，用来下载伴侣）：电脑只回这几种请求，都带 `Connection: close`、回完即关：
- `GET` / `HEAD /c/<当前的令牌>`：一个 HTML 页面（UTF-8），说明这是什么、给出下载链接 `/c/<令牌>/gs-companion.apk`；请求头 `Accept-Language` 的第一个语言以 `zh` 开头时用中文，否则英文。
- `GET` / `HEAD /c/<当前的令牌>/gs-companion.apk`：程序旁的 `gs-companion.apk`（`application/vnd.android.package-archive`）；没有这个文件时 404。
- 其余（令牌不对、别的路径、别的方法、请求头超过 8 KiB 或 5 秒内没收完）：404 或直接关闭。

**握手**（明文，连接建立后伴侣先发；伴侣连接以 4 字节 `GSCL` 开头，HTTP 请求以方法名开头，电脑据前 4 个字节区分）：

| 方向 | 内容 |
|---|---|
| 伴侣 → 电脑 | `magic bytes[4]`（ASCII `GSCL`）· `version u8`（当前为 1）· `token blob8（≤ 64）` · `phone bytes[16]` · `nonce bytes[16]` |
| 电脑 → 伴侣 | `magic bytes[4]`（`GSCL`）· `version u8` · `status u8`（0 接受；1 不认识：令牌不是当前的、或这台手机没配对过；2 版本不符）· `pc bytes[16]` · `nonce bytes[16]` |

- `token`：扫码配对时是二维码里的令牌（ASCII 原样）；重连时为空，电脑按 `phone` 找这台手机记下的配对。`phone` 是伴侣第一次运行时随机生成的 16 字节手机编号；`nonce` 是双方每条连接各自随机的 16 字节。
- `status` 不是 0 时 `pc`、`nonce` 为全 0，电脑回完即关闭；伴侣收到 1 时不删配对（可能只是这台手机的控制窗口没开），照下面的"重连"稍后再试。`pc` 是电脑编号：每次配对时电脑随机生成 16 字节，和密钥一起记在这台手机的设置里；重连时伴侣核对回复里的 `pc` 与记下的相同，不同就当作没连上（上次的地址可能换了一台电脑）。
- 会话密钥 `S = HMAC-SHA256(K, "GSCP/1 companion lan" ‖ hello ‖ reply)`：`K` 是配对时二维码里的密钥、重连时双方记下的密钥；`hello`、`reply` 是上面两段的完整字节。握手 5 秒内没完成即关闭。

**加密记录**（握手之后两个方向都只发记录）：`length u32`（其后的字节数，22 ≤ `length` ≤ 6 + 16 KiB + 16）· `ciphertext bytes[length]`。
- `ciphertext` = AES-256-GCM（密钥 `S`；随机数 12 字节 = `direction u32`（0 = 电脑 → 伴侣，1 = 伴侣 → 电脑）‖ `counter u64`，每个方向从 0 起每条记录加 1；附加数据 = 前面 4 个 `length` 字节），末尾 16 字节是标签。
- 明文恰好是一个 3.1 节的消息帧（伴侣连接上下文）。`length` 越界、标签不对、明文不是一个合法的帧：关闭连接。

**开局与消息**：握手后照 11.2 节第 2 步起：电脑先发 `CompanionHello`，局域网上令牌为空，伴侣不校验（连接已由密钥认证）；其余消息与心跳同 11.3、11.4 节。局域网上收到电脑的 `Goodbye` 时伴侣只关闭这条连接、不停止服务，照下面的"重连"继续等电脑，直到用户在通知栏点"停止"。
- 配对成功（带令牌的握手通过且收到 `CompanionWelcome`）后，电脑把 `phone`、`K`、`pc` 记在这台手机的设置里（同一台手机再配对时取代旧的）；伴侣记下 `pc`、`K` 与这次的地址（主机与端口），最近的在前，最多 4 份。
- 重连：伴侣先试上次的地址，再用 mDNS 找 `_gs-companion._tcp`（实例名 `gs-<pc 的 32 位小写十六进制>`，电脑监听期间为每台在等的、配对过的手机通告）；都不通时间隔 2 秒起、每次翻倍、最多 30 秒再试，网络变化时立即再试。
- Windows 防火墙第一次会问是否允许 GameScrcpy 接受局域网的连接，允许后才连得上（帮助"AOA 触控与 WinUSB 驱动"写明）。

金标准向量 `vectors/companion/lan.json`：二维码网址、配对与重连两种握手的完整字节、会话密钥、两个方向的加密记录（含其中的消息帧），以及 AES-256-GCM 的已知答案。

### 11.8 截图（没有画面时编辑键位的背景，ADR-0105 第 5 节）

没有画面的控制窗口（AOA、ESP32）里编辑键位时，电脑可以向伴侣要一张手机当前画面的截图当编辑器的背景。adb 连接与局域网连接都能用。不做实时画面。

- **请求**：电脑发 `ScreenshotRequest`。同一条连接上同一时间最多一个请求在等回复；伴侣在上一个请求还没回完时收到新的，回 `ScreenshotFailed(busy)`。`requestId` 由电脑选，伴侣原样带回。
- **录屏授权**：这条连接上第一次收到请求时，伴侣在手机上弹出系统的录屏确认（`MediaProjectionManager.createScreenCaptureIntent()`）。用户拒绝 → `ScreenshotFailed(refused)`。同意后伴侣开始录屏：前台服务加上类型 `mediaProjection`（权限 `FOREGROUND_SERVICE_MEDIA_PROJECTION`），状态栏显示录屏图标；画面经 `VirtualDisplay` 进 `ImageReader`。授权一直保持到这条连接结束（电脑发 `Goodbye`、连接断开或失效），之后的请求直接截图、不再确认；连接结束时伴侣停止录屏。电脑在无画面的控制窗口关闭时结束伴侣连接，录屏随之停止。系统不让录屏（如没有录屏服务）→ `ScreenshotFailed(unsupported)`。
- **截图**：取最近的一帧，方向与手机当前方向一致（宽高与主显示器当前方向的宽高同向），长边缩到 `maxSide`（0 时 1920）以内、不放大，编码成 JPEG（质量 `quality`）。超过 4 MiB（4194304 字节）时伴侣逐步降低质量重编，仍超过 → `ScreenshotFailed(error)`。
- **分块**：伴侣按 `index` 从 0 起依次发 `ScreenshotChunk`；`total` 是块数，每块都相同；除最后一块外每块正好 15360 字节，最后一块 1～15360 字节，按 `index` 拼起来就是 JPEG 文件。总长不超过 4 MiB，所以 `total` ≤ 274（伴侣连接每条消息最多 16 KiB，见 3.1 节）。
- **电脑收块**：只收正在等的 `requestId` 的块与失败，其余丢弃；`total` 与先到的块不同、同一 `index` 重复、拼起来超过 4 MiB：这次截图失败。还没收到任何块时等 30 秒（用户要在手机上确认），收到块之后两块之间最多等 5 秒，超时即失败；连接结束时还在等的请求失败。

### 11.9 设置同步（版本 2，ADR-0112 第 5.2 节）

手机上的光标、手机上的键位提示（显示与淡化后的不透明度）、用电脑键盘打字与自动连接在电脑上和伴侣里都能开关，两边保持一致。它们是电脑上这台手机自己的设置（`CompanionItem` 括号里的设置，按设备保存）：电脑上的值是存档，伴侣记着每台电脑最后发来的一份。只在版本 2 的连接上（11.2 节）。

- **快照**（`CompanionSettings`）：连接进入工作状态后电脑先发一份；之后这些项、电脑的名字、强调色或 `applied` 变了再发完整的一份，与上次发出的完全相同则不发。
  - `computer`：电脑的编号，同一台电脑经 USB、无线调试与局域网都相同，伴侣按它分开记每台电脑。电脑把 ASCII `GameScrcpy companion computer`、一个 0 字节与 Windows 的机器编号（读不到时用计算机名）连起来做 SHA-256，取前 16 字节，不直接发出机器编号。它与局域网配对的 `pc`（11.7 节）无关。
  - `name`：电脑的计算机名，超过 64 字节时在字符边界截断，可为空。`link`：这条连接是怎么连的。`accent`：电脑当前的强调色（如 Ember 为 `0xFF6B3D`），伴侣连着这台电脑时用它画强调色。
  - `applied`：这条连接上电脑已经处理过的最后一条 `CompanionSettingChange` 的 `change`，还没有时为 0。
- **手机上改**（`CompanionSettingChange`）：用户在伴侣里改一项时，伴侣把新值发给每条已收到快照的版本 2 连接；`change` 在每条连接上从 1 起每条加 1。电脑按到达的顺序处理：把值存为这台手机的设置，再发一份 `applied` 等于这条 `change` 的快照（值没变也发，当作回执）；存不下时（例如还不知道这台手机的设备编号）同样回执，快照里是电脑上原来的值。
- **两边都改**：以后到电脑的为准。伴侣收到的快照 `applied` 小于它某一项最后发出的 `change` 时，这一项仍显示手机上改的值（电脑还没处理到），其余项照快照；`applied` 达到后整份照快照，所以电脑在那之后改的值会盖过手机上的。
- **没连着时改**：伴侣把改动记下来（重启手机也在）并显示在开关上；连上一台说版本 2 的电脑、收到它的第一份快照后，把与快照不同的项逐项发出，相同的直接作废。发出后连接断了、没等到回执的，下次连上再发。只说版本 1 的电脑收不到这些改动，伴侣继续记着。
- **伴侣记的**：每台电脑（按 `computer`）的名字、连接方式、强调色、最后一份快照与最后连上的时间。没连着时开关显示最近连过的那台电脑的值，再加上还没发出的改动。
- **自动连接**（`autoConnect`）：关掉时伴侣不在后台重连局域网上配对过的电脑（11.7 节"重连"：断开后、网络变化时都不再自己去连），只在用户点"现在连接"或扫码时连一次；经 adb 的连接由电脑发起，不受影响。电脑只保存这一项。
