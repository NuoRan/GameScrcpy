# 更新日志 / Changelog

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
