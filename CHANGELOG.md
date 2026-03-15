# 更新日志 / Changelog

## v1.3.3 (2026-03-15)

> 新增 Companion App、多触控后端路由、视角自适应平滑、帮助中心等

### 📱 Companion App（全新 Android 辅助应用）

- **CursorService 浮动光标服务**：TCP 端口 26758，PC 端发送归一化坐标 → 手机端绘制浮动光标覆盖层
  - 协议：`0x01 [float x][float y]` 更新位置、`0x03` 隐藏光标
  - 实时 `Display.getRealSize()` 适配横屏/竖屏旋转，不再缓存尺寸
  - `FLAG_LAYOUT_IN_SCREEN` 精准定位，修复 ~50px 偏移问题
  - 5 秒无更新自动隐藏、40dp 密度自适应光标尺寸
- **ScreenshotService 远程截屏服务**：TCP 端口 26759，基于 MediaProjection + ImageReader
  - 协议：`0x02` 请求截屏、`0x82 [int32 len][JPEG]` 响应
  - Android 14+ 兼容（`registerCallback` 替代 `registerDisplayListener`）
  - 截屏完成自动停止服务，释放 MediaProjection 资源
- **MainActivity**：两个独立开关按钮分别启停光标/截屏服务，显示设备 IP、端口、分辨率
- **PC 端 CompanionClient**：大端序 float 序列化、JPEG 响应解析、临时连接自动清理
- **自定义应用图标**：使用 GameScrcpy.png 生成全密度 mipmap 图标
- **Android 14+ 前台服务**：`foregroundServiceType="specialUse"` / `"mediaProjection"`，符合新版权限要求

### 🎮 多触控后端 & TouchRouter

- **ITouchBackend 统一接口**：`sendTouch(action, touchId, x, y)` + `resetAllTouch()` + `supportsKeys()`，坐标范围 0-65535
- **TouchRouter 路由器**：4 种 TouchMethod（Adb / Uhid / Esp32 / Aoa），自动解析 FastMsg 分发到对应后端
- **UhidTouchBackend**：通过控制通道发送 UHID_CREATE/INPUT/DESTROY 消息，服务端写 `/dev/uhid` 创建虚拟触摸屏，支持 WiFi 无需 USB 驱动
- **AoaHidBackend + AoaHidDevice**：libusb AOA 协议注册 HID 触摸屏+键盘设备，`contactId` 池管理（0-15），多点触控活跃点补发，显示旋转坐标变换
- **Esp32HidBackend**：QineTouch v3.2 协议（`0xF4` header + 12 字节包），串口通信（默认 921600 波特率），多触点管理
- **HidReportDescriptor**：USB HID 触摸屏报告描述符（7 字节格式，最大 16 触点）+ 键盘描述符
- **WinUsbDriverHelper**：Windows WinUSB 驱动自动安装/卸载，枚举 Android VID USB 设备，动态生成 INF

### 🔍 视角控制自适应 EMA 平滑

- **自适应双阈值 EMA**：替代固定系数 0.85，速度低时强平滑（`FACTOR_LOW=0.5`）消除锯齿，速度高时弱平滑（`FACTOR_HIGH=0.95`）保持响应
- **速度阈值**：`SPEED_LOW=0.001`（归一化）→ `SPEED_HIGH=0.008`，线性插值计算因子
- **Overshoot 回灌管线**：边缘/中心回正不再一次性 MOVE 跳步，改为回灌 `m_pendingMoveDelta` 由下帧 EMA 平滑消化，消除视角瞬移
- **EMA 历史清空**：`onCenterRepressTimer()` 中 `m_smoothedDelta = {0,0}`，全新触摸序列不受旧残差影响
- **移除 JITTER_THRESHOLD**：消除低速锯齿的根本原因（累积→刷新阶梯效应）
- **Timer 间隔 15ms → 20ms**：减少过于频繁的回正触发

### 🎮 方向轮盘重置可靠性

- **resetWheel() 延迟重按**：场景切换时（如跑步按 F 进车）UP → 20ms 延迟 → 重新 DOWN，确保服务端正确识别触摸释放
- **resetRepressTimer (20ms)**：`onResetRepressTimer()` 延迟后重新计算方向键状态并 re-trigger `executeMove`
- **waitingForResetRepress 状态**：防止重入，所有键松开时自动取消定时器

### 📖 帮助中心 & 引导系统

- **HelpDialog 12 节帮助中心**：快速入门 / 连接设备 / 投屏窗口 / 键鼠映射 / 脚本编辑器 / mapi API / 图像识别 / 自定义选区 / 设置参数 / 终端 ADB / 快捷键 / 常见问题
  - 统一 Fluent Design CSS、暗色模式支持、左侧导航 + 右侧内容区
- **OnboardingOverlay 分步引导**：聚光灯裁剪 + 提示卡片、淡入动画、自动定位（下/上/右/左）
  - 主窗口 9 步引导（欢迎→USB→WiFi→设备列表→导航栏→设置→终端→工作流→完成）
  - 投屏窗口 4 步引导（画面区域→操作工具栏→键位面板）
  - 编辑模式引导（首次进入键位编辑时触发）
  - 设置页可重新启动全部引导

### 🔌 连接进度指示

- **ConnectionProgressWidget**：11 阶段可视化连接进度（Idle→Checking→Pushing→Starting→Connecting→Negotiating→Streaming→Connected/Failed/Timeout/Cancelled）
- 脉冲动画、超时处理、状态文本实时更新

### 📊 性能监控 & 图像截取

- **PerformanceDialog**：实时视频指标（FPS/解码延迟/渲染延迟/帧数/丢帧）、网络指标（延迟/收发字节/pending）、输入指标
- **ImageCaptureDialog**：3 种模式（模板截取/搜索区域选择/位置点选择）、`ZoomableImageWidget` 可缩放显示、OpenCV 集成

### 🐛 Bug 修复

- **ESC 宏热键无法执行**：键位映射解析器 `getItemKey()` 的 `qtKeyFromName` 查找表仅包含 `Key_Escape` 前缀形式，JSON 中写 `"Esc"` 无法匹配；新增 `displayNameToKeyWithModifiers()` 作为 fallback，支持所有短名称（Esc、Space、Tab、Enter、F1-F12、箭头键等）
- **脚本 `simulateKey("ESC")` 无限递归**：`script_simulateKey()` 调用 `keyEvent()` 重新进入键映射分发器，遇到同一 KMT_SCRIPT 节点再次触发脚本；改为通过 `InputDispatcher::convertKeyCode()` + `sendKeyEvent()` 直接发送 Android 按键，绕过整个键映射链路
- **视频窗口等比例锁定**：新增 `nativeEvent` 处理 WM_NCHITTEST（仅四角返回 HTTOPLEFT/HTTOPRIGHT/HTBOTTOMLEFT/HTBOTTOMRIGHT，边缘返回 HTCLIENT 禁止拖拽）+ WM_SIZING（动态计算窗口开销、以宽度为基准等比约束高度）+ WM_NCCALCSIZE（保持无边框外观）
- **横竖屏切换窗口不更新**：`updateShowSize()` 检测 `m_widthHeightRatio` 跨越 1.0 的方向变化，即使用户手动调整过窗口也强制 resize 并重新计算尺寸
- **移除设置页"恢复 ADB 驱动"按钮**：清理 SettingsPage 中 `#ifdef HAVE_AOA_HID` 相关的四处代码（include、UI 创建、click handler、翻译文本）
- **光标覆盖层偏移**：CursorService 从 `DisplayMetrics`（内容区域）改为 `Display.getRealSize()`（含状态栏/导航栏），修复 ~50px 右偏
- **横屏光标错位**：`getRealSize()` 从 onCreate 缓存改为 `updateCursor()` 每次实时查询，适配屏幕旋转
- **视角瞬移**：边缘/中心回正 overshoot 从即时 MOVE 改为管线回灌 EMA 平滑
- **低速锯齿**：移除 JITTER_THRESHOLD 阶梯效应，自适应 EMA 低速强平滑
- **轮盘重置失败**：resetWheel 即时 UP+DOWN 改为 20ms 延迟重按

---

## v1.3.2 (2026-03-14)

### 🔧 帧缓存与输入优化

- **YUV 帧缓存抓图**：`grabCurrentFrame()` / `grabGrayFrame()` 改为从 CPU 侧缓存的 YUV 平面数据转换，不再依赖 GPU 回读，消除 D3D11 设备锁竞争
- **cacheFramePlanes()**：每帧渲染时同步缓存 Y/U/V 三平面到 `std::vector`，加 `std::mutex` 保护
- **Click 事件 DOWN/UP 分离**：脚本 `click()` 在 DOWN 与 UP 之间插入 20ms 延迟（`kClickReleaseDelay`），确保服务端正确识别点击
- **鼠标按键状态追踪**：`InputDispatcher::mouseEvent()` 新增 `m_keyStates` 按键状态记录

### 📝 文档与构建修正

- **移除 Qt Multimedia 虚假依赖**：BUILD.md 发布包结构移除 `Qt6Multimedia.dll` 和 `multimedia/` 目录
- **新增 QuickJS 内置依赖说明**：构建文档已内置依赖表新增 QuickJS (env/quickjs/)
- **修正构建脚本输出路径**：`build_all.bat` 修正为 `output/x64/<BuildType>/` 匹配 CMake RUNTIME_OUTPUT_DIRECTORY
- **修正 config.ini 过时注释**：移除 OpenGL 硬解选项说明，更新编码器支持 H.265，修正 ServerPath 后缀
- **版本号统一更新**：README / SCRIPT_API / RC 文件版本同步至 1.3.2

## v1.3.1 (2026-03-12)

### 🐛 编辑模式崩溃与稳定性修复

- **NaN 比例崩溃修复**：`updateShowSize()` 新增 `size.width() <= 0 || size.height() <= 0` 早期返回守卫，防止 0÷0 产生 NaN 导致无限缩放振荡
- **sizeHint 冲突修复**：`D3D11VideoWidget::sizeHint()` 改为返回固定 `QSize(256,256)`，避免帧尺寸变化与 KeepRatioWidget 手动布局冲突
- **覆盖层同步防抖**：新增 `m_pendingOverlaySync` 标志位，合并高频 resize 事件的覆盖层同步
- **删除 use-after-free 修复**：键位映射项的关闭按钮点击改用 `QTimer::singleShot(0)` 延迟删除，避免 `mousePressEvent` 中同步移除导致的野指针访问
- **项目位置跳变修复**：移除 `updateShowSize` 定时器中的重复重定位逻辑、收紧边界钳位范围、dialog `exec()` 前后保存恢复位置

## v1.3 (2026-03-06)

> 183 files changed, +12,615 / -7,877 lines

### 🏗️ 脚本引擎迁移：QJSEngine → QuickJS

- **QuickJS 嵌入式 C 引擎**：完全移除 Qt QJSEngine 依赖，改用 QuickJS（ES2023 标准），编译为独立静态库
- **JsEngine C++ 包装层**：新增 `JsEngine.h` 封装 QuickJS C API，提供类型安全的 JS↔C++ 交互
- **JsBindings 绑定层**：新增 `JsBindings.h/.cpp`，28 个 `mapi` API 全部适配 QuickJS
- **零 Qt 脚本运行时**：ScriptSandbox 使用 `std::mt19937` 替代 QRandomGenerator，`std::chrono` 替代 QDateTime

### 🖥️ D3D11 原生渲染引擎

- **D3D11Renderer**：纯 D3D11 视频渲染器，支持 YUV420P / NV12 / D3D11VA 硬解纹理直通
- **D3D11VideoWidget**：QWidget 封装的 D3D11 渲染窗口，无锁原子帧邮箱（`submitFrameDirect` 零拷贝帧提交）
- **替代 OpenGL**：主渲染路径从 QOpenGLWidget (PBO + D3D11-GL interop) 切换为 D3D11 原生，减少一层互操作开销
- **链接 d3d11 / dxgi / d3dcompiler**：CMakeLists 新增 DirectX 依赖

### ⚡ 零拷贝视频管线

- **ZeroCopyDecoder**：FFmpeg 硬件加速解码（D3D11VA），GPU 帧直接传递渲染器，跳过 `av_hwframe_transfer_data`
- **ZeroCopyStreamManager**：端到端零拷贝管线：VideoSocket → Demuxer → Decoder → FrameQueue → Renderer，无运行时 malloc
- **FramePool 预分配帧池**：无锁 atomic CAS 扫描，运行时零内存分配
- **FrameQueue 自适应抖动管理**：SPSC 队列 + RFC 3550 自适应抖动算法
- **SPSCQueue 无锁队列**：自研单生产者单消费者队列，cache line padding 避免 false sharing

### 🔊 音频系统重构

- **WasapiPlayer**：纯 WASAPI 共享模式播放器替代 `QAudioSink`，独立 feed 线程，零 Qt 依赖
- **AudioStreamManager**：scrcpy 官方 `audio_regulator` 架构，独立接收线程 + SPSC 环形缓冲
- **音频通道**：TCP 4 通道架构新增专用音频端口 (27186)，OPUS 编解码实时转发
- **WASAPI 缓冲 10ms**：播放缓冲从 50ms 降至 10ms，环形缓冲目标 30ms
- **动态重采样补偿**：`swr_set_compensation()` 微调重采样率维持缓冲水位

### 🎨 Fluent Design UI 全面重构

- **MainWindow 导航式主窗口**：替代旧 Dialog，NavigationView + QStackedWidget 分页架构
- **NavigationView**：Windows 11 风格可折叠侧边导航栏，活动指示条动画
- **ThemeManager 主题管理器**：深色/浅色/跟随系统 + 6 种强调色 + QSS 模板编译
- **设计 Token 系统**：DesignTokens + MotionTokens，统一的颜色/间距/动效规范
- **分页架构**：HomePage、SettingsPage、TerminalPage、DeviceDetailPage
- **20+ Fluent 组件**：FluentCard、FluentButton、FluentToggle、FluentDialog、FluentInfoBar、FluentSlider、FluentProgressRing、FluentBadge、FluentToolWindow、FluentComboBox、FluentInput、SettingRow、DeviceCard、ActivityLog 等
- **VideoBottomBar**：视频窗口底部工具栏重构
- **VideoSettingsPopup**：视频参数设置弹窗
- **KeyMapSidePanel**：键位配置侧边栏
- **OnboardingOverlay**：首次使用分步引导覆盖层（4 步引导流程）
- **HelpDialog**：帮助弹窗

### 🏗️ 核心层架构重构 (core/)

- **接口抽象层 (interfaces/)**：`IDecoder`、`IVideoChannel`、`IControlChannel`、`IRenderer`、`IInputProcessor`，依赖反转设计
- **基础设施层 (infra/)**：`FramePool`、`FrameQueue`、`FrameData`、`SessionParams`
- **实现层 (impl/)**：`TcpVideoChannel`、`KcpVideoChannel`、`TcpControlChannel`、`KcpControlChannel`、`FFmpegDecoderImpl`、`ZeroCopyDecoder`、`GameInputProcessor`
- **服务层 (service/)**：`DeviceSession`（门面）、`StreamManager`、`ZeroCopyStreamManager`、`InputManager`、`ConnectionManager`、`SessionFactory`
- **DeviceSession 门面模式**：UI 层与核心层唯一接口，VideoForm 通过 `bindSession()` 绑定

### 📡 自研 Signal\<\> 信号系统

- **GameSignal.h**：轻量级类型安全信号槽系统，header-only C++17，支持 `connect`/`disconnect`/`ScopedConnection`/`weak_ptr` guard
- **全面替代 Qt signals/slots/emit**：非 UI 层完全脱离 Qt 元对象系统，无需 MOC 编译
- ScriptSandbox 11 个信号、StreamManager/ConnectionManager/DeviceSession 等全部使用自研 Signal

### 🔌 网络传输层去 Qt 化

- **NativeTcpSocket**：原生 Winsock2 TCP 封装替代 `QTcpSocket`，阻塞式 send/recv
- **NativeTcpServer**：原生 Winsock2 TCP 服务器替代 `QTcpServer`
- **KcpClient 无 Qt 依赖**：`CircularBuffer` O(1) 环形缓冲替代 `QByteArray` append/remove
- **NativeTimer**：Win32 Timer 替代 `QTimer`
- **ThreadDispatcher**：原生线程调度替代 Qt 线程间通信

### 📡 辅助通道 (Auxiliary Channel)

- **AuxChannelClient**：独立于控制通道的第三条 TCP 通道 (端口 27185)
- 支持运行时动态调整：视频码率/帧率/分辨率、视频流暂停/恢复
- **TCP 4 通道架构**：video (27183) + ctrl (27184) + aux (27185) + audio (27186)

### ⚙️ 控制层架构升级

- **HandlerChain 处理器链**：责任链模式输入分发
- **独立处理器**：SteerWheelHandler、ViewportHandler、FreeLookHandler、CursorHandler、KeyboardHandler
- **SessionContext + SessionVars + ScriptBridge + InputDispatcher**：会话状态三层管理
- **ScriptWatchdog**：脚本看门狗超时保护
- **KeyMapPropertyPanel**：键位属性编辑面板
- **KeyConflictIndicator**：键位冲突可视化指示

### ⚙️ 配置系统重构

- **ConfigCenter 单例**：分层配置（默认 → 全局 → 用户 → 运行时覆盖），`std::variant` ConfigValue
- **IniConfig**：C++17 原生 INI 解析替代 Qt QSettings
- 配置变更监听、依赖注入

### 🔄 scrcpy 服务端升级

- **服务端升级至 v3.3.4**：跟进上游 scrcpy 最新版本
- **H.265 编码支持**：新增 H.265 (HEVC) 视频编码选项
- **自定义服务端构建**：`build_without_gradle.bat` 无 Gradle 编译脚本

### 🧹 精简瘦身

- **移除系统托盘**：点击标题栏 X 直接退出程序，不再最小化到托盘
- **移除崩溃转储 (.dmp)**：禁用 MiniDump 生成，移除 DbgHelp 依赖，保留文本崩溃日志
- **全面日志清理**：移除/节流 8+ 文件中的逐帧、逐包、启动诊断日志，减少 I/O 开销

### 🐛 Bug 修复

- **WiFi 视口漂移**：添加 DPR 补偿，解决高 DPI 下视角持续偏移
- **编辑模式删除按钮**：修复双层 Bug（scene 缓存 + fallback 逻辑）导致 Camera/FreeLook 项无法删除
- **\~ 键误触发**：添加 `isValidMouseMoveMap` 守卫，防止非视角模式下误触发
- **编辑覆盖层卡顿**：改用 `SetParent` 原生子窗口，消除跨窗口坐标转换延迟
- **引导黑屏**：修复 VideoForm 引导遮罩在无视频帧时显示黑屏
- **按键格式错误**：修复键位映射按键名称格式化 Bug
- **引导 z-order**：修复编辑模式引导层级遮挡问题

---

## v1.2 (2026-02-16)

### ⚡ 控制延迟优化

- **视频IO线程隔离**：`KcpVideoClient` 的 `KcpTransport`（UDP收发 + KCP更新）移至独立 `QThread`（`VideoKCP-IO`），视频高码率/高包量场景不再阻塞主线程事件循环，控制通道响应延迟显著降低
- **根因**：视频和控制 KCP 共享主线程事件循环，`onSocketReadyRead()` 的 `while(hasPendingDatagrams)` 循环在复杂场景下处理数百个 UDP 包，导致控制通道 ACK/发送被饿死

### ⚡ 协议极简化（v2 wire format）

- **Touch 6B**（原 10B）：action 编码进 type 字节，seqId 压缩为 1 字节
- **Touch RESET 1B**（原 10B）
- **Key 3B**（原 4B）：action 编码进 type 字节
- **Batch 6B/event**（原 9B）
- type 值 10-16 + 0xFF，避开 scrcpy 原生类型 0/2/4 的冲突

### 🚀 裸 UDP 视频传输

- **UdpVideoSender（服务端）**：无 KCP 协议栈开销的帧级 UDP 发送器，每包 `[seq(4B)+flags(1B)+payload(≤1395B)]`，SOF/EOF 标志标记帧边界
- **UdpVideoClient（客户端）**：帧级重组 + 帧级丢包保护，SOF→EOF 之间 seq 不连续则整帧丢弃（避免字节流错位脏画面），IO 线程帧重组→CircularBuffer→解码线程阻塞读取
- **对比 KCP**：零 ACK 流量、零重传延迟、协议头 5B（KCP 24B）、无用户态线程
- **码率自适应缓冲区**：环形缓冲区 = max(bitrate/8×3s, 4MB)，OS recv = max(bitrate/8/fps×10帧, 2MB)

### 🛡️ FEC 前向纠错

- **XOR 冗余编码**：10:1 分组（每 10 数据包生成 1 校验包），6B FEC 头 `[type+groupId+index+groupSize+originalLen(2B)]`
- **客户端解码器**：环形缓冲区（MAX_GROUPS=4），组内丢 1 包可恢复（`recovered = parity XOR all_other_packets`）
- **透明集成**：KcpTransport UDP 输出回调中自动编解码，对上层协议透明

### 📊 服务端自适应码率（ABR）

- **编码器级 ABR**：每 500ms 统计实际码率，>目标×1.2 按比例降低，<目标×0.7 逐步恢复（+10%），变化>5% 才调整 `PARAMETER_KEY_VIDEO_BITRATE`，Clamp 到 [目标×0.25, 目标]
- **网络层建议码率**：KcpVideoSender 根据 pending/baseWindow 比例建议 33%/50%/75% 码率，与编码器级取较小值
- **KcpVideoSender 动态丢帧**：滞回控制（pending > dropThreshold 开始丢帧，< resumeThreshold 停止），RTT 自适应阈值（congestionRatio > 0.8 激进丢帧），config 和 keyFrame 永不丢弃

### 🎬 服务端 OpenGL 滤镜管线

- **AffineOpenGLFilter**：服务端 GPU 仿射变换着色器（旋转/裁剪/翻转），`GL_OES_EGL_image_external` 外部纹理，超出 [0,1] 范围输出黑色
- **VideoFilter 变换链**：crop → orientation → angle（自由角度） → resize，合成为单个 `AffineMatrix` 一次 GPU 渲染完成
- **OpenGLRunner**：EGL/GLES 环境管理（单例 HandlerThread），`eglPresentationTimeANDROID` 精确时间戳传递
- **自动降分辨率**：首帧编码失败时按序列 2560→1920→1600→1280→1024→800 回退

### ⚡ KCP 协议栈优化

- **服务端 KCP 纯 Java 实现**：Segment 对象池（ArrayDeque, 默认256）、预分配 ACK 数组（512 slots）、手动 byte[] 解析替代 ByteBuffer、索引循环替代 for-each（避免 Iterator GC）
- **批量操作优化**：`parseUna()` 批量 `subList(0,count).clear()` 替代逐个 `remove(0)` 的 O(k×n)
- **客户端 KcpCore 读写锁**：`std::shared_mutex` 优化只读方法，`processInputBatch()` 将 N 次 input+update+peekSize 从 N+2 次加锁→1 次
- **KcpTransport 批量 UDP 处理**：MAX_BATCH=64 包/批，栈分配 UdpPacket 数组
- **KcpConfig 统一常量**：CONV_VIDEO=0x11223344, CONV_CONTROL=0x22334455, interval=1ms, minRTO=1ms

### ⚡ 服务端低延迟编码

- **H.264 Baseline Profile**：无 B 帧重排序延迟，`KEY_MAX_B_FRAMES=0`
- **CBR + 实时优先级**：`KEY_PRIORITY=0`（API 23+），`KEY_LATENCY=0`（API 30+），`KEY_OPERATING_RATE=Short.MAX_VALUE`（禁止降频节能）
- **厂商私有低延迟**：vendor.low-latency.enable=1（高通/三星/联发科）
- **GOP 缩短**：`KEY_I_FRAME_INTERVAL=1s`，加速错误恢复

### 🏗️ 服务端会话架构重构

- **ScrcpySession 模板方法模式**：抽象基类统一 TCP/KCP 会话生命周期（beforeRun→createChannels→onInitialized→startProcessors→cleanup）
- **TcpSession**（USB 模式）：LocalSocket 视频/控制通道
- **KcpSession**（WiFi 模式）：UdpVideoSender + KcpControlChannel
- **Completion 计数器**：多处理器协调，全部完成或致命错误时 `Looper.quitSafely()`

### ⚡ 服务端 FastTouch 优化

- **O(1) 数据结构**：`seqIdToIndex[256]` O(1) 查找，`usedPointerIdBitmap` O(1) 分配/释放，交换删除法 O(1) 移除
- **预计算缩放因子**：16 位归一化坐标 (0~65535) → `scaleX = displayWidth / 65535f`，避免每事件浮点除法
- **单触点跳过排序**：最常见场景零额外开销
- **ControlMessageReader 优化**：64B `BufferedInputStream`，批量消息预分配 `byte[255×6]` 一次 `readFully`

### 🆕 脚本工具系统

- **虚拟按钮管理器**：`ScriptButtonManager` 单例（线程安全 `QReadWriteLock`），通过选区编辑器创建/拖拽/重命名虚拟按钮，保存到 `keymap/buttons.json`
- **滑动路径管理器**：`ScriptSwipeManager` 单例（线程安全 `QReadWriteLock`），两次点击设置起点→终点，保存到 `keymap/swipes.json`
- **新增 API `mapi.getbuttonpos(buttonId)`**：按编号获取虚拟按钮位置，返回 `{x, y, valid, name}`
- **新增 API `mapi.swipeById(swipeId, durationMs, steps)`**：按编号执行预定义滑动路径，内部委托 `slide()` 带拟人曲线
- **选区编辑器增强**：新增「新建按钮」「新建滑动」创建模式，所有元素支持拖拽编辑、右键菜单重命名/删除/生成代码片段
- **脚本编辑器快捷面板**：新增 `getbuttonpos`、`swipeById` 快捷指令入口

### 🐛 Bug 修复

- **FreeLook 挡位单击**：单击检测逻辑修复
- **编辑模式 use-after-free 崩溃**：`clearEditingState()` 在 `scene()->clear()` 之前调用

### 🧹 协议代码精简

- 删除未使用的 `BufferUtil`（bufferutil.h/.cpp）
- 合并 `ControlSender::doWriteKcp/doWriteTcp` 为单一 `doWrite()`
- 移除 `Controller::sendControl()` 死代码
- 移除 9 个未使用的 FastMsg 便捷方法

---

## v1.1 (2026-02-15)

> 67 files changed, +1894 / -355 lines

### 🎮 视角控制

- **ViewportHandler 速度自适应倍增器**：新增基于移动速度的灵敏度缩放，正常移动保持 1:1，快速甩枪时自动加速（最高 1.6x），二次方平滑过渡
- 亚像素精度累积，不丢弃微小位移
- 传感器级抖动过滤（阈值 0.00008）
- 轻量 EMA 平滑（系数 0.85）

### ⚡ 客户端性能优化

- **零分配序列化**：SteerWheelHandler / FreeLookHandler / CursorHandler / KeyboardHandler 全部改用栈缓冲区 `char buf[]` + `serializeTouchInto()` / `serializeKeyInto()`，消除每次操作的 QByteArray 堆分配
- **FastMsg 新增 `serializeKeyInto()`**：与 `serializeTouchInto()` 对齐的零拷贝键盘序列化接口
- **postFastMsg() 零开销**：移除 `std::chrono` 计时和 `PerformanceMonitor` 统计调用
- **TCP 控制通道**：启用 `TCP_NODELAY`（禁用 Nagle），发送缓冲区缩小至 16KB
- **TCP flush 移除**：`controlsender.cpp` 不再每次写入后同步 flush
- **VSync 关闭**：渲染器默认禁用垂直同步
- **PBO 异步 DMA**：`glMapBufferRange` 实现纹理异步上传
- **渲染/解码线程优先级提升**：Windows 平台 MMCSS + `SetThreadPriority`
- **PerformanceMonitor 无锁化**：原子操作替代 QMutex + std::deque

### ⚡ 服务端性能优化

- **FastTouch / FastKey ASYNC 注入**：`InputManager.injectInputEvent` 改为异步模式
- **ControlMessage 对象池**：FastTouch / FastKey 消息复用，减少 GC 压力
- **KCP 控制通道轮询**：`POLL_INTERVAL_MS` 500ms → 50ms

### 🏗️ 零拷贝视频管线

- **ZeroCopyDecoder**：SIMD 加速内存拷贝（SSE2/AVX2）、NV12 UV 去交织 SIMD 加速
- **D3D11VA GPU 直通**：硬件解码帧直接传递给 OpenGL 渲染，跳过 `av_hwframe_transfer_data`
- **D3D11GLInterop**：`WGL_NV_DX_interop` 实现 D3D11 纹理到 GL 纹理的零拷贝共享
- **FramePool 无锁化**：atomic CAS 扫描 + 无锁 acquire/release
- **FrameQueue 自适应抖动管理**：SPSC 队列 + 帧池一体化
- **ZeroCopyRenderer**：跳帧到最新策略，丢弃积压帧
- **submitFrameDirect**：原子指针交换无锁帧提交

### 🔌 KCP 传输层

- **KcpVideoClient 环形缓冲区**：替代 QByteArray append/remove，O(1) 读写
- **KcpTransport 按需调度**：`ikcp_check` 计算下次更新时间，减少无效 timer 唤醒
- **收到数据立即 update**：ACK 最快发出，避免对端触发不必要重传

### 🐛 稳定性修复

- **KCP Dead Link 断连修复**：服务端 `KcpCore.setFastMode()` 中 `deadLink` 阈值从默认 20 提升至 100，容忍 WiFi 环境下的短暂丢包
- **killTimer 跨线程修复**：`Demuxer` 清理 `KcpVideoSocket` 时改用 `moveToThread(mainThread)` + `deleteLater()`，避免从工作线程操作主线程创建的 QTimer
- **KcpVideoSocket 析构函数**：添加 `close()` 调用，确保 `deleteLater()` 触发时正确清理资源
- **QThread 优先级警告修复**：`ScriptSandbox` / `Demuxer` 的 `start()` 显式指定 `QThread::NormalPriority`

### 🆕 新功能

- **首次运行用户协议弹窗**：Apache 2.0 许可声明 + 免责条款，主题配色匹配 psblack.css
- **Config 持久化**：`agreementAccepted` 字段存储于 `userdata.ini`
- **SettingsDialog**：新增设置对话框 UI

### 🧹 代码整理

- 清理全部 `[低延迟优化 StepXX]`、`【xxx优化】`、`（与原版完全一致）` 等冗余注释标签（20 个文件，70+ 处）

---

## v1.0

- 初始版本发布
- 基于 QtScrcpy 的游戏级 Android 投屏控制工具
- FastTouch 协议、键位映射脚本系统、KCP WiFi 传输
