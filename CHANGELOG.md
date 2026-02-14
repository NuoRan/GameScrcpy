# 更新日志 / Changelog

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
