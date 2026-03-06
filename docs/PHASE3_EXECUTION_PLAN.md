# Phase 3: 深度去 Qt 化执行计划

> **目标**: 将所有非 UI-Widget 层的 Qt 依赖替换为 C++17 标准库 + Win32 API
> **约束**: 保留 Qt Widgets / QOpenGLWidget 作为 UI 框架（直到 Step 17 D3D11 替换完成）
> **起始状态**: Phase 2 (Steps 21-29) 已完成，网络层已全部 Winsock2 化

---

## 当前 Qt 依赖全景

| 类别 | 非UI文件数 | 总出现次数 | 替换难度 |
|------|-----------|-----------|---------|
| `QString` | ~79 | 1000+ | ⭐⭐⭐⭐⭐ |
| `emit` / 信号发射 | ~30 类 | 200+ | ⭐⭐⭐⭐ |
| `connect()` / 信号连接 | ~30 类 | 200+ | ⭐⭐⭐⭐ |
| `QTimer` / `timerEvent` | 10 文件 | 30+ | ⭐⭐⭐ |
| `QPointer` | 11 文件 | 15+ | ⭐⭐ |
| `QSettings` | 4 文件 | 集中 | ⭐⭐ |
| `QImage` (非UI) | 10 文件 | 20+ | ⭐⭐⭐ |
| `QJSEngine` / `QJSValue` | 4 文件 | 集中 | ⭐⭐⭐⭐⭐ |
| `QOpenGL*` | 3 文件 | 核心渲染 | ⭐⭐⭐⭐⭐ |
| `QMetaObject::invokeMethod` | 9 文件 | 15+ | ⭐⭐⭐ |
| `Q_OBJECT` 类 (非UI) | 30 个 | — | ⭐⭐⭐⭐ |
| `QProcess` (AdbProcess) | 已底层替换 | 壳层残留 | ⭐⭐ |
| `Q_ENUM` | 2 文件 | 4 处 | ⭐ |
| `Q_INVOKABLE` | 1 文件 | 28 处 | 随 QJSEngine |

---

## 执行顺序总览

```
阶段 A: 基础设施建设 (Step 30-32)
  ├── Step 30: 自研信号槽系统 Signal<Args...>
  ├── Step 31: 线程安全消息队列 ThreadDispatcher
  └── Step 32: 原生 INI 配置系统 (替换 QSettings)

阶段 B: 低风险逐步替换 (Step 33-36)
  ├── Step 33: QPointer → SafePtr (弱引用包装)
  ├── Step 34: Q_ENUM → DECLARE_ENUM_STRINGS 宏
  ├── Step 35: QImage (非UI) → FrameBuffer / cv::Mat
  └── Step 36: QTimer (非UI) → NativeTimer

阶段 C: 核心管线信号脱耦 (Step 37-42)
  ├── Step 37: transport 层信号替换 (Server/KcpServer/TcpServerHandler)
  ├── Step 38: decoder 层信号替换 (Demuxer/AudioStreamManager)
  ├── Step 39: control 层信号替换 (Controller/ControlSender/InputDispatcher)
  ├── Step 40: core/service 层信号替换 (StreamManager/ConnectionManager)
  ├── Step 41: 脚本层信号替换 (ScriptWatchdog/ScriptEngine/ScriptBridge)
  └── Step 42: AdbProcess 完全脱 QObject

阶段 D: 脚本引擎替换 (Step 43-45) [原 Step 16]
  ├── Step 43: QuickJS 集成 + 基础绑定框架
  ├── Step 44: 全部 Q_INVOKABLE → QuickJS C 绑定
  └── Step 45: ScriptSandbox 脱 QObject + QThread

阶段 E: 渲染引擎替换 (Step 46-49) [原 Step 17]
  ├── Step 46: D3D11 YUV 渲染器核心
  ├── Step 47: D3D11 渲染器集成到窗口系统
  ├── Step 48: 帧抓取 D3D11 实现 (替换 QImage)
  └── Step 49: 移除 QOpenGL 依赖

阶段 F: QString 全局迁移 (Step 50-53)
  ├── Step 50: 字符串桥接层 GameString + 转换工具
  ├── Step 51: transport/common 层 QString → std::string
  ├── Step 52: control/core/decoder 层 QString → std::string
  └── Step 53: UI 边界适配层 + CMake 最终清理

阶段 G: 收尾验证 (Step 54)
  └── Step 54: 全量编译 + 功能测试 + 依赖审计
```

---

## 阶段 A: 基础设施建设 (Step 30-32)

> 后续所有替换都依赖这些基础组件，必须先建设。

### Step 30: 自研信号槽系统 `Signal<Args...>`

> **风险**: ⭐⭐⭐ | **预计耗时**: 8h | **前置**: 无

**目标**: 实现一个轻量级、类型安全的 C++ 信号/槽系统，替代 Qt 的 `signals/slots/emit/connect`。

**设计要求**:
- 头文件 only（`common/Signal.h`）
- 模板类 `Signal<Args...>`，支持 `connect(callable)` 返回 `ConnectionId`
- `disconnect(ConnectionId)` 断开
- `emit(args...)` 或 `operator()(args...)` 触发
- 线程安全：内部 `std::mutex` 保护连接列表
- 支持自动断连：连接到 `std::weak_ptr` 持有的对象时自动检测失效
- 支持 `BlockingConnection` 模式（调用方等待槽执行完毕）
- **不需要** MOC、不需要字符串查找、不需要 QObject 继承

**API 示例**:
```cpp
class Server {
public:
    Signal<bool, QString, QSize> serverStarted;  // 替代 signals: void serverStarted(...)
};

// 替代 connect(server, &Server::serverStarted, this, &Ctrl::onStart)
auto id = server->serverStarted.connect([this](bool ok, auto& name, auto& sz) {
    onStart(ok, name, sz);
});

// 替代 emit serverStarted(true, name, size)
server->serverStarted(true, name, size);
```

**涉及文件**:
- 新建: `client/src/common/Signal.h`
- 新建: `client/src/common/Signal_test.cpp` (单元测试，可选)

**验收标准**:
- 编译通过
- 能在多线程环境下安全 connect/disconnect/emit
- emit 的吞吐量 ≥ Qt 信号/槽（无跨线程时应为纳秒级直接调用）

---

### Step 31: 线程安全消息队列 `ThreadDispatcher`

> **风险**: ⭐⭐⭐ | **预计耗时**: 6h | **前置**: 无

**目标**: 替代 `QMetaObject::invokeMethod(obj, lambda, Qt::QueuedConnection)` 的跨线程调用机制。

**设计要求**:
- 头文件 + 实现: `common/ThreadDispatcher.h/.cpp`
- 每个线程可注册一个 `Dispatcher` 实例
- `postToThread(threadId, std::function<void()>)` — 异步投递
- `invokeOnThread(threadId, std::function<void()>)` — 阻塞等待执行
- 主线程 Dispatcher 集成到 Qt 事件循环（过渡期）：用 `QCoreApplication::postEvent` 或 Win32 `PostMessage`
- 后续非 Qt 线程使用独立消息泵（`condition_variable` 通知）

**核心接口**:
```cpp
namespace dispatch {
    void postToMain(std::function<void()> fn);  // 投递到主线程
    void postTo(std::thread::id tid, std::function<void()> fn);
    void invokeOnMain(std::function<void()> fn); // 阻塞调用主线程
    void processQueue();  // 非 Qt 线程手动 pump
}
```

**涉及文件**:
- 新建: `client/src/common/ThreadDispatcher.h`
- 新建: `client/src/common/ThreadDispatcher.cpp`

**替代目标** (15 处 `QMetaObject::invokeMethod`):
| 文件 | 当前用法 | 替代后 |
|------|---------|--------|
| kcpcontrolsocket.cpp | invokeMethod → emit 信号 | dispatch::postToMain → Signal.emit |
| kcpvideosocket.cpp | invokeMethod → emit 信号 | dispatch::postToMain → Signal.emit |
| qyuvopenglwidget.cpp | invokeMethod → repaint/stop | dispatch::postToMain → 直接调用 |
| adbprocess.cpp | invokeMethod → 回调 | dispatch::postToMain → 回调 |
| demuxer.cpp | invokeMethod → deleteLater | dispatch::postToMain → delete |
| ScriptSandbox.cpp | invokeMethod → watchdog/toast | dispatch::postToMain → 回调 |
| ScriptWatchdog.cpp | invokeMethod → 启动定时器 | dispatch::postToMain → NativeTimer |

**验收标准**:
- 跨线程投递延迟 < 1ms
- 主线程分发与 Qt 事件循环兼容（过渡期）

---

### Step 32: 原生 INI 配置系统 (替换 QSettings)

> **风险**: ⭐⭐ | **预计耗时**: 4h | **前置**: 无

**目标**: 用纯 C++ INI 解析器替代 `QSettings`。

**设计要求**:
- 头文件 + 实现: `common/IniConfig.h/.cpp`
- 读写 INI 格式（section/key/value），UTF-8 编码
- 支持 `getString/setString`, `getInt/setInt`, `getBool/setBool` 等类型化 API
- 自动创建目录、原子写入（先写 .tmp 再 rename）
- 可使用 `inih` 库（单头文件，MIT 许可）或自写

**涉及文件**:
- 新建: `client/src/common/IniConfig.h`
- 新建: `client/src/common/IniConfig.cpp`
- 修改: `app/config.h/.cpp` — `QSettings*` → `IniConfig*`
- 修改: `common/ConfigCenter.h/.cpp` — `QSettings*` → `IniConfig*`

**验收标准**:
- 读写现有 config.ini / userdata.ini 结果与 QSettings 完全一致
- 删除 `#include <QSettings>` 后编译通过

---

## 阶段 B: 低风险逐步替换 (Step 33-36)

> 这些替换相互独立，每步改动集中，风险低。

### Step 33: `QPointer` → `SafePtr` 弱引用包装

> **风险**: ⭐⭐ | **预计耗时**: 3h | **前置**: Step 30 (Signal)

**目标**: 消除对 `<QPointer>` 的依赖。`QPointer` 核心功能是自动置空（当被指向的 QObject 被销毁时）。

**替代方案**:
- 对于 `std::shared_ptr` 管理的对象 → `std::weak_ptr`
- 对于裸指针 + 明确生命周期的对象 → 裸指针 + RAII guard（析构时置空）
- 新建 `common/SafePtr.h`: 轻量弱引用，被引用对象析构时自动通知

**涉及文件** (11 个非 UI 文件):
| 文件 | 当前 QPointer 指向 | 替换策略 |
|------|-------------------|---------|
| server.h | `QPointer<KcpServer>`, `QPointer<TcpServerHandler>` | 裸指针 (Server 拥有它们的生命周期) |
| devicemanage.h | `QPointer<Server>` | 裸指针 (DeviceController 拥有) |
| kcpserver.h | `QPointer<KcpVideoSocket>`, `QPointer<KcpControlSocket>` | 裸指针 (KcpServer 拥有) |
| demuxer.h | `QPointer<KcpVideoSocket>` | 裸指针 (外部传入，调用方保证生命周期) |
| controlsender.h | `QPointer<KcpControlSocket>` | 裸指针 |
| controller.h | `QPointer<ControlSender>`, `QPointer<Receiver>` | `std::unique_ptr` (Controller 拥有) |
| InputDispatcher.h | `QPointer<Controller>` | 裸指针 (SessionContext 保证) |
| ScriptBridge.h | `QPointer<Controller>` | 裸指针 |
| SessionContext.h | `QPointer<Controller>` | 裸指针 |

**验收标准**:
- 所有 `#include <QPointer>` 从非 UI 代码中移除
- 编译 0 errors 0 warnings

---

## 阶段 C: 核心管线信号脱耦 (Step 37-42)

> **核心任务**: 将 30 个非 UI `Q_OBJECT` 类中的 `signals/slots/emit/connect` 替换为 Step 30 的 `Signal<>` 系统。
> 按层级从底向上替换，每步完成后构建验证。

### Step 37: transport 层信号替换

> **风险**: ⭐⭐⭐ | **预计耗时**: 10h | **前置**: Step 30, Step 36

**目标**: transport 模块的 8 个 Q_OBJECT 类信号→自研 Signal<>。

**涉及类及其信号清单**:

| 类 | 信号 | 连接方 |
|----|------|--------|
| `Server` | `serverStarted(bool,QString,QSize)`, `serverStoped()` | DeviceController |
| `TcpServerHandler` | `serverStarted(bool,QString,QSize)`, `serverStoped()` | Server |
| `KcpServer` | `serverStarted(bool,QString,QSize)`, `serverStoped()` | Server |
| `KcpVideoSocket` | `readyRead()`, `connected()`, `disconnected()`, `errorOccurred(QString)` | Demuxer, ConnectionManager |
| `KcpControlSocket` | `readyRead()`, `connected()`, `disconnected()`, `errorOccurred(QString)` | ConnectionManager |
| `DeviceController` | `connected(...)`, `disconnected(QString)` | DeviceManage → UI |
| `DeviceManage` | (无自身信号，使用回调) | — |
| `AdbProcess` | `adbProcessResult(quint16,...)` | TcpServerHandler, KcpServer, DeviceController |

**改动模式** (以 Server 为例):
```cpp
// 之前 (Qt)
class Server : public QObject {
    Q_OBJECT
signals:
    void serverStarted(bool, const QString&, const QSize&);
};
// emit serverStarted(true, name, size);
// connect(m_server, &Server::serverStarted, this, &Ctrl::onStart);

// 之后 (Signal<>)
class Server {  // 不再继承 QObject
public:
    Signal<bool, const std::string&, int, int> serverStarted;
};
// serverStarted(true, name, w, h);
// m_server->serverStarted.connect([this](auto&&... args) { onStart(args...); });
```

**注意**: `KcpVideoSocket`/`KcpControlSocket` 当前用 `QMetaObject::invokeMethod` 跨线程 emit，改为 `dispatch::postToMain` + `Signal<>::emit`。

**Step 37 内部分步**:
1. Server 类去 Q_OBJECT — `serverStarted/serverStoped` → `Signal<>`
2. TcpServerHandler 去 Q_OBJECT — 信号 → `Signal<>`，`timerEvent` → `NativeTimer` (Step 36 前置)
3. KcpServer 去 Q_OBJECT — 同上
4. KcpVideoSocket/KcpControlSocket — 信号 → `Signal<>`，`QMetaObject::invokeMethod` → `dispatch::postToMain`
5. AdbProcess — 信号 → `Signal<>`（保留内部 Win32 进程管理）
6. DeviceController — 信号 → `Signal<>`，`connect` → `Signal::connect`
7. 更新所有 `connect()` 调用方
8. 编译验证

**验收标准**:
- transport 模块 0 个 `Q_OBJECT`（除 DeviceManage 如果难以脱耦）
- `#include <QObject>` 从 transport 层全部移除
- 编译 0 errors 0 warnings
- USB/WiFi 连接流程正常

---

### Step 38: decoder 层信号替换

> **风险**: ⭐⭐⭐ | **预计耗时**: 6h | **前置**: Step 30, Step 37

**目标**: decoder 模块的 3 个 Q_OBJECT 类信号→Signal<>。

**涉及类**:

| 类 | 信号 | 连接方 |
|----|------|--------|
| `Demuxer` | `onStreamStop()`, `getFrame(AVPacket*)`, `getConfigFrame(AVPacket*)` | StreamManager, ZeroCopyStreamManager |
| `AudioStreamManager` | `audioStreamStarted()`, `audioStreamStopped()`, `audioStreamError(QString)` | DeviceSession |
| `IDecoder` (接口) | `stateChanged(State)`, `fpsUpdated(int)`, `hardwareDecoderFallback()`, `decoderError(QString)` | StreamManager |

**特别注意**:
- `Demuxer::getFrame(AVPacket*)` 是帧管线热路径，已用 `std::thread` 解复用，通过 `DirectConnection` 在解复用线程同步调用解码器。替换为 `Signal<AVPacket*>` 后保持同步调用语义（非跨线程）即可，零开销。
- `AudioStreamManager` 已用 `std::thread`，信号仅用于通知 UI 状态，适合 `dispatch::postToMain` + `Signal<>`。

**验收标准**:
- decoder 模块 0 个 `Q_OBJECT`
- 视频解码、音频流功能正常
- 编译 0 errors 0 warnings

---

### Step 39: control 层信号替换

> **风险**: ⭐⭐⭐⭐ | **预计耗时**: 12h | **前置**: Step 30, Step 36

**目标**: control 模块的 8 个 Q_OBJECT 类信号→Signal<>。

**涉及类**:

| 类 | 信号 | 复杂度 |
|----|------|--------|
| `Controller` | `grabCursor(bool)` | 低 |
| `ControlSender` | `sendError(QString)` + QTimer slot `flushCoalesced()` | 中 (需 NativeTimer) |
| `InputDispatcher` | `grabCursor(bool)` + `timerEvent` | 中 (需 NativeTimer) |
| `SessionContext` | `grabCursor(bool)`, `scriptTipRequested(...)`, `keyMapOverlayUpdateRequested(...)` | 中 |
| `ScriptBridge` | `grabCursor(bool)` + slots | 中 |
| `ViewportHandler` | 无信号，3 个 QTimer slots | 中 (需 NativeTimer) |
| `SteerWheelHandler` | 无信号，3 个 QTimer slots | 中 (需 NativeTimer) |
| `ScriptWatchdog` | `softTimeout()`, `hardTimeout()` + 2 个 QTimer | 中 |

**改动要点**:
- `ControlSender`: `QTimer* m_coalesceTimer` → `NativeTimer`，`flushCoalesced` slot → 回调
- `InputDispatcher`: `startTimer/killTimer/timerEvent` → `NativeTimer`
- `ViewportHandler/SteerWheelHandler`: 多个 QTimer → 多个 NativeTimer，slots → lambda 回调
- 全部去掉 `Q_OBJECT` 宏和 QObject 继承

**验收标准**:
- control 模块 0 个 `Q_OBJECT`
- 鼠标/键盘控制正常，方向盘拟人化正常，视角控制正常
- 编译 0 errors 0 warnings

---

### Step 40: core/service 层信号替换

> **风险**: ⭐⭐⭐⭐ | **预计耗时**: 10h | **前置**: Step 37, Step 38, Step 39

**目标**: core 模块的 6 个 Q_OBJECT 类信号→Signal<>。

**涉及类**:

| 类 | 信号数量 | 复杂度 |
|----|---------|--------|
| `ConnectionManager` | 7 个信号 + 2 个 slots | 高 (枢纽类) |
| `StreamManager` | 4 个信号 + 3 个 slots + QTimer | 高 |
| `ZeroCopyStreamManager` | 6 个信号 + 3 个 slots | 高 |
| `ZeroCopyDecoder` | 2 个信号 | 中 |
| `ZeroCopyRenderer` | 1 个信号 | 低 (但涉及 OpenGL 上下文) |
| `DeviceSession` | 12+ 个信号 (门面类) | 高 (最终对外接口) |

**关键挑战**:
- `DeviceSession` 是 Core↔UI 边界，其信号直接被 `VideoForm`、`Dialog` 等 UI 类连接。
  - **策略**: DeviceSession 的 Signal<> 在 UI 端用适配器包装回 Qt connect（过渡期），或 UI 直接使用 Signal::connect。
- `ConnectionManager` 串联 Server/KcpVideoSocket/KcpControlSocket，是连接生命周期管理核心。
- `StreamManager` 串联 Demuxer→Decoder→Renderer 的帧管线。

**验收标准**:
- core 模块 0 个 `Q_OBJECT`（ZeroCopyRenderer 保留 QOpenGLWidget 继承，但不再是 Q_OBJECT）
- 视频播放、FPS 统计、连接管理正常
- 编译 0 errors 0 warnings

---

### Step 41: 脚本层信号替换

> **风险**: ⭐⭐⭐ | **预计耗时**: 6h | **前置**: Step 30, Step 36

**目标**: ScriptWatchdog 和 ScriptEngine 的信号→Signal<>。
> 注意: ScriptSandbox 和 SandboxScriptApi 依赖 QJSEngine，留到阶段 D。

**涉及类**:
| 类 | 操作 |
|----|------|
| `ScriptWatchdog` | `softTimeout/hardTimeout` → Signal<>; 2 个 QTimer → NativeTimer; 去 Q_OBJECT |
| `ScriptEngine` | 10 个信号 → Signal<>; 管理层纯转发; 去 Q_OBJECT |

**注意**: ScriptEngine 的信号是从 ScriptSandbox 转发而来。暂时 ScriptSandbox 保留 Q_OBJECT（直到阶段 D QuickJS 替换），ScriptEngine 可以先用 Signal<> 订阅 ScriptSandbox 的 Qt 信号（混合模式过渡）。

**验收标准**:
- ScriptWatchdog 和 ScriptEngine 去掉 Q_OBJECT
- 脚本执行、超时检测功能正常

---

### Step 42: AdbProcess 完全脱 QObject

> **风险**: ⭐⭐ | **预计耗时**: 4h | **前置**: Step 30

**目标**: AdbProcess 去掉 QObject 继承，`adbProcessResult` 信号→Signal<> 或回调。

**当前状态**: 底层进程执行已用 Win32 `CreateProcessW`（`AdbProcessImpl`），但外层 `AdbProcess` 仍是 QObject 子类，使用信号 `adbProcessResult` 通知调用方。

**改动**:
1. `AdbProcess` 去掉 `Q_OBJECT` 和 `QObject` 继承
2. `adbProcessResult` signal → `Signal<quint16, ...>` 或 `std::function` 回调
3. `IAdbExecutor` 接口 — 去掉 Q_OBJECT，6 个信号 → 6 个 Signal<>
4. 更新 TcpServerHandler/KcpServer/DeviceController 中所有 `connect(m_workProcess, ...)` → `Signal::connect`

**验收标准**:
- AdbProcess/IAdbExecutor 去掉 Q_OBJECT
- adb push/reverse/forward/execute 流程正常
- 编译 0 errors 0 warnings

---

## 阶段 D: 脚本引擎替换 — QJSEngine → QuickJS (Step 43-45)

> 对应原 Step 16。QJSEngine 是 Qt::Qml 模块的核心依赖，替换后可移除 `Qt::Qml`。

### Step 43: QuickJS 集成 + 基础绑定框架

> **风险**: ⭐⭐⭐⭐ | **预计耗时**: 16h | **前置**: Step 30, Step 31

**目标**: 将 QuickJS 嵌入项目，建立 C++ ↔ JS 绑定框架。

**QuickJS 选择理由**:
- 单文件嵌入（quickjs.c + quickjs.h）约 600KB 编译产物
- 完整 ES2020 支持（async/await, Promise, modules）
- 比 QJSEngine 更轻量（无 MOC、无 Qt 依赖）
- MIT 许可，可商用
- 可中断（`JS_SetInterruptHandler` — 替代 `QJSEngine::setInterrupted`）

**改动**:
1. 引入 QuickJS 源码到 `client/src/common/quickjs/`
2. 新建 `common/JsEngine.h/.cpp` — QuickJS 包装类:
   ```cpp
   class JsEngine {
   public:
       JsEngine();
       ~JsEngine();
       bool evaluate(const std::string& code, std::string& error);
       void registerFunction(const char* name, JSCFunction* func, int argc);
       void setInterrupted(bool interrupted);
       JSValue callGlobal(const char* funcName, ...);
   };
   ```
3. 新建 `common/JsBindings.h` — 绑定宏

**涉及文件**:
- 新建: `common/quickjs/quickjs.c`, `common/quickjs/quickjs.h`, `common/quickjs/quickjs-libc.c`
- 新建: `common/JsEngine.h`, `common/JsEngine.cpp`, `common/JsBindings.h`
- 修改: CMakeLists.txt 添加 quickjs 编译

**验收标准**:
- QuickJS 编译通过，能执行 JS 并调用 C++ 函数

---

### Step 44: 全部 Q_INVOKABLE → QuickJS C 绑定

> **风险**: ⭐⭐⭐⭐ | **预计耗时**: 16h | **前置**: Step 43

**目标**: `SandboxScriptApi` 的 28 个 `Q_INVOKABLE` 方法逐一迁移到 QuickJS 绑定。

**28 个方法**: `click`, `holdpress`, `release`, `slide`, `pinch`, `isPress`, `key`, `releaseAll`, `sleep`, `isInterrupted`, `stop`, `toast`, `setGlobal`, `getGlobal`, `loadModule`, `log`, `shotmode`, `setRadialParam`, `resetview`, `resetwheel`, `getmousepos`, `getkeypos`, `getbuttonpos`, `getKeyState`, `setKeyUIPos`, `findImage`, `findImageByRegion`, `swipeById`

**验收标准**:
- 所有 28 个方法在 QuickJS 中可调用
- 现有用户脚本无需修改即可运行
- `SandboxScriptApi` 去掉 `Q_INVOKABLE` 和 `Q_OBJECT`

---

### Step 45: ScriptSandbox 脱 QObject + QThread

> **风险**: ⭐⭐⭐⭐ | **预计耗时**: 12h | **前置**: Step 44, Step 30

**目标**: ScriptSandbox 去掉 QObject/QThread，用 `std::thread` + `Signal<>` + `ThreadDispatcher`。

**替换后架构**:
- `std::thread` 运行脚本（替代 QThread）
- `JsEngine::setInterrupted()` (QuickJS `JS_SetInterruptHandler`)
- 11 个信号 → 11 个 `Signal<>`
- 跨线程通信 → `dispatch::postToMain()`

**涉及文件**:
- 重写: `ScriptSandbox.h/.cpp`
- 修改: `ScriptEngine.h/.cpp`
- 修改: CMakeLists.txt — 移除 `Qt::Qml`

**验收标准**:
- `Qt::Qml` 从 CMake 移除，脚本功能正常，编译 0 errors

---

## 阶段 E: 渲染引擎替换 — QOpenGL → D3D11 (Step 46-49)

> 对应原 Step 17。完成后移除 `Qt::OpenGL` + `Qt::OpenGLWidgets`。

### Step 46: D3D11 YUV 渲染器核心

> **风险**: ⭐⭐⭐⭐⭐ | **预计耗时**: 24h | **前置**: 无（可与阶段 C 并行）

**目标**: 纯 D3D11 的 YUV/NV12 → RGB 渲染管线。

**核心接口**:
```cpp
class D3D11Renderer {
public:
    bool initialize(HWND hwnd, int width, int height);
    void resize(int width, int height);
    void renderFrame(const AVFrame* frame);  // YUV420P or NV12
    void present();
    void shutdown();
    bool renderHWFrame(ID3D11Texture2D* texture, int index); // 零拷贝硬解
};
```

**涉及文件**:
- 新建: `render/d3d11/D3D11Renderer.h/.cpp`
- 新建: `render/d3d11/shaders/yuv_ps.hlsl`, `fullscreen_vs.hlsl`

**验收标准**: 60fps 1080p 渲染无掉帧，GPU < 5%

---

### Step 47: D3D11 渲染器集成到窗口系统

> **风险**: ⭐⭐⭐⭐ | **预计耗时**: 16h | **前置**: Step 46

**目标**: D3D11Renderer 嵌入 Qt Widget 窗口（过渡方案：`QWidget::winId()` → HWND）。

**涉及文件**:
- 新建: `render/d3d11/D3D11VideoWidget.h/.cpp`
- 修改: `IVideoRenderer.h`, `videoform.cpp`

**验收标准**: 视频能正常显示，窗口缩放/全屏正常

---

### Step 48: 帧抓取 D3D11 实现

> **风险**: ⭐⭐⭐ | **预计耗时**: 6h | **前置**: Step 46, Step 35

**实现**: `CopyResource` → staging texture → `Map` → `cv::Mat`

**验收标准**: 帧抓取、截图、模板匹配正常

---

### Step 49: 移除 QOpenGL 依赖

> **风险**: ⭐⭐⭐ | **预计耗时**: 4h | **前置**: Step 47, Step 48

**改动**:
1. 删除/归档 `qyuvopenglwidget.h/.cpp`、`D3D11GLInterop.h/.cpp`、`OpenGLRenderer.cpp`
2. CMakeLists.txt: 移除 `Qt::OpenGL`, `Qt::OpenGLWidgets`, `opengl32.lib`

**验收标准**: `Qt::OpenGL` 移除，视频全走 D3D11，编译 0 errors

---

## 阶段 F: QString 全局迁移 (Step 50-53)

> **最大工程量的阶段**。QString 渗透到 79 个非 UI 文件、1000+ 处使用。
> 策略：先建桥接层，再分模块逐步替换，最后适配 UI 边界。

### Step 50: 字符串桥接层 + 转换工具

> **风险**: ⭐⭐⭐ | **预计耗时**: 6h | **前置**: 无

**目标**: 建立 `std::string` ↔ `QString` 无缝转换基础设施，使后续逐步替换成为可能。

**设计**:
```cpp
// common/StringUtils.h
namespace strutil {
    // UTF-8 std::string ↔ QString
    inline QString toQ(const std::string& s) { return QString::fromUtf8(s.c_str(), (int)s.size()); }
    inline std::string fromQ(const QString& s) { auto u8 = s.toUtf8(); return std::string(u8.data(), u8.size()); }

    // UTF-8 std::string ↔ std::wstring (Win32 API 交互)
    std::wstring toWide(const std::string& utf8);
    std::string fromWide(const std::wstring& wide);

    // 格式化
    std::string format(const char* fmt, ...);  // printf-style
}
```

**涉及文件**:
- 新建: `common/StringUtils.h`
- 新建: `common/StringUtils.cpp`

**验收标准**:
- 转换函数正确处理中文/日文等多字节 UTF-8
- 编译通过

---

### Step 51: transport/common 层 QString → std::string

> **风险**: ⭐⭐⭐⭐ | **预计耗时**: 16h | **前置**: Step 50, Step 37

**目标**: transport 和 common 模块中所有 QString 参数/成员/返回值改为 `std::string`。

**涉及重点文件**:
| 文件 | QString 用途 | 改动量 |
|------|-------------|--------|
| `server.h/cpp` | serial, deviceName, serverPath 等 | 大 |
| `devicemanage.h/cpp` | DeviceParams 结构体全是 QString | 大 |
| `tcpserverhandler.h/cpp` | serial, serverPath 等 | 大 |
| `kcpserver.h/cpp` | host, serial 等 | 中 |
| `adbprocess.h/cpp` | 命令行参数、输出 | 大 |
| `NativeTcpSocket.h/cpp` | connectToHost 已用 std::string ✅ | 无 |
| `ConfigCenter.h/cpp` | get/set 用 QString key | 大 |
| `config.h/cpp` | 全部 getter/setter 返回 QString | 大 |
| `Logger.h/cpp` | 日志格式化 | 中 |

**改动模式**:
```cpp
// 之前
QString getSerial() const { return m_serial; }
void setSerial(const QString& serial) { m_serial = serial; }

// 之后
const std::string& getSerial() const { return m_serial; }
void setSerial(const std::string& serial) { m_serial = serial; }
```

**UI 边界处理**: UI 代码调用处加 `strutil::toQ()` / `strutil::fromQ()` 转换。

**验收标准**:
- transport + common 模块中 `#include <QString>` 减少到仅 UI 边界转换处
- 编译 0 errors 0 warnings

---

### Step 52: control/core/decoder 层 QString → std::string

> **风险**: ⭐⭐⭐⭐ | **预计耗时**: 16h | **前置**: Step 51, Step 38-41

**目标**: control、core、decoder 模块的 QString → std::string。

**涉及重点文件**:
| 模块 | 文件 | 改动量 |
|------|------|--------|
| control | controller.h/cpp, keymap.h/cpp, SessionContext, ScriptBridge | 大 |
| core | DeviceSession.h/cpp, ConnectionManager, StreamManager | 大 |
| decoder | demuxer.h/cpp, AudioStreamManager | 中 |
| render | IVideoRenderer.h | 小 |

**Signal<> 中的 QString**: Step 37-42 中 Signal<> 模板参数包含 QString 的要改为 `const std::string&`。

**验收标准**:
- control/core/decoder 模块中 QString 仅残留在直接与 UI 交互的边界
- 编译 0 errors 0 warnings

---

### Step 53: UI 边界适配层 + CMake 最终清理

> **风险**: ⭐⭐⭐ | **预计耗时**: 8h | **前置**: Step 51, Step 52

**目标**: 确保 UI 层与纯 C++ 内核之间的字符串转换无缝衔接。

**改动**:
1. `DeviceSession` 对外接口（UI 调用）中的信号参数保持 `QString`，内部转换
   - 或者 UI 直接使用 `Signal<std::string>` + `strutil::toQ()` 显示
2. `Config` / `ConfigCenter` 对外 API 返回 `std::string`，UI 调用处转换
3. CMakeLists.txt 最终清理:
   - 确认最终 Qt 模块: 仅 `Qt::Widgets`, `Qt::Gui`, `Qt::Svg`, `Qt::SvgWidgets`
   - 验证 `Qt::Qml`、`Qt::OpenGL`、`Qt::OpenGLWidgets` 已移除
4. 移除所有残留的 `Q_OBJECT` 在非 UI 类中

**验收标准**:
- CMake 直接依赖仅 `Qt::Widgets Qt::Gui Qt::Svg Qt::SvgWidgets`
- 非 UI 代码中 0 个 `Q_OBJECT`
- 全量编译 0 errors 0 warnings

---

## 阶段 G: 收尾验证 (Step 54)

### Step 54: 全量编译 + 功能测试 + 最终依赖审计

> **风险**: ⭐⭐ | **预计耗时**: 8h | **前置**: 全部前序步骤

**功能测试清单**:
| 测试项 | 验证内容 |
|--------|---------|
| USB 有线连接 | adb push → reverse → 视频/控制/音频/辅助 4 通道 |
| WiFi 无线连接 | KCP 视频 + KCP 控制 + TCP 音频/辅助 |
| 视频渲染 | D3D11 渲染 60fps 1080p，窗口缩放/全屏 |
| 音频播放 | Opus/AAC 解码 + WASAPI 播放 |
| 鼠标键盘控制 | 普通模式 + 游戏映射模式 |
| 方向盘/视角 | SteerWheel 拟人化 + Viewport 视角控制 |
| 脚本执行 | QuickJS 执行用户脚本，所有 28 个 API |
| 脚本超时 | watchdog 软/硬超时中断 |
| 模板匹配 | findImage cv::Mat 路径正常 |
| 帧抓取/截图 | D3D11 readback → cv::Mat → save |
| 配置读写 | IniConfig 读写 config.ini / userdata.ini |
| 多设备 | 同时连接 2+ 设备 |
| 热插拔 | 断开重连不崩溃 |

**最终依赖审计**:
```
期望的 CMake 链接库:
  Qt::Widgets, Qt::Gui, Qt::Svg, Qt::SvgWidgets   (UI 框架)
  d3d11, dxgi, d3dcompiler                          (D3D11 渲染)
  ws2_32, iphlpapi                                   (Winsock2 网络)
  winmm, avrt                                        (音频定时器)
  avformat, avcodec, avutil, swscale, swresample    (FFmpeg)
  opencv_world                                       (OpenCV, 可选)

已移除:
  × Qt::Network    (Phase 2 Step 29 已移除)
  × Qt::Qml        (Phase 3 Step 45 移除)
  × Qt::OpenGL     (Phase 3 Step 49 移除)
  × Qt::OpenGLWidgets (Phase 3 Step 49 移除)
  × opengl32.lib   (Phase 3 Step 49 移除)
```

**非 UI 代码剩余 Qt 依赖**:
```
期望: 0 个 Q_OBJECT, 0 个 emit, 0 个 connect, 0 个 QTimer
允许: QString 仅在 UI 边界转换处, QImage 仅在 UI 代码中
```

---

## 总工时估算

| 阶段 | 步骤 | 预计工时 |
|------|------|---------|
| A: 基础设施 | Step 30-32 | 18h |
| B: 低风险替换 | Step 33-36 | 19h |
| C: 信号脱耦 | Step 37-42 | 48h |
| D: 脚本引擎 | Step 43-45 | 44h |
| E: 渲染引擎 | Step 46-49 | 50h |
| F: QString 迁移 | Step 50-53 | 46h |
| G: 收尾验证 | Step 54 | 8h |
| **总计** | **25 步** | **~233h** |

---

## 依赖关系图 (关键路径)

```
Step 30 (Signal) ──┬── Step 33 (QPointer)
                   ├── Step 36 (NativeTimer) ──┬── Step 37 (transport 信号)
                   │                           ├── Step 38 (decoder 信号)
                   │                           ├── Step 39 (control 信号)
                   │                           └── Step 41 (脚本信号)
                   ├── Step 37 ─── Step 40 (core 信号) ─── Step 52 (QString core)
                   ├── Step 42 (AdbProcess)
                   └── Step 43 (QuickJS) ─── Step 44 ─── Step 45

Step 31 (Dispatcher) ──── Step 37/38/39/43

Step 32 (IniConfig) ──── Step 51 (QString transport)

Step 34 (Q_ENUM) ───── 独立

Step 35 (QImage) ──── Step 48 (D3D11 帧抓取)

Step 46 (D3D11 核心) ─── Step 47 ─── Step 49
                         Step 48 ───┘

Step 50 (StringUtils) ─── Step 51 ─── Step 52 ─── Step 53 (最终清理)

Step 53 ─── Step 54 (收尾验证)
```

**关键路径**: Step 30 → Step 36 → Step 37 → Step 40 → Step 52 → Step 53 → Step 54

---

## 执行进度追踪

| Step | 内容 | 状态 | 完成日期 |
|------|------|------|----------|
| 30 | Signal<> 信号系统 (GameSignal.h) | ✅ 完成 | 2026-03-03 |
| 31 | ThreadDispatcher 消息队列 | ✅ 完成 | 2026-03-03 |
| 32 | IniConfig 替换 QSettings | ✅ 完成 | 2026-03-03 |
| 33 | QPointer → 裸指针 (11个非UI文件) | ✅ 完成 | 2026-03-03 |
| 34 | Q_ENUM → keyMapTypeFromString() | ✅ 完成 | 2026-03-03 |
| 35 | QImage (非UI) → cv::Mat | ✅ 完成 | 2026-03-03 |
| 36 | QTimer (非UI) → NativeTimer (Win32) | ✅ 完成 | 2026-03-03 |
| 37 | transport 层信号替换 (8个类) | ✅ 完成 | 2026-03-03 |
| 38 | decoder 层信号替换 (3个类) | ✅ 完成 | 2026-03-03 |
| 39 | control 层信号替换 (8个类) | ✅ 完成 | 2026-03-03 |
| 40 | core/service 层信号替换 (5个类) | ✅ 完成 | 2026-03-03 |
| 41 | 脚本层信号替换 | ✅ 完成 | 2026-03-03 |
| 42 | AdbProcess 脱 QObject (Step 37已完成) | ✅ 完成 | 2026-03-03 |
| 43 | QuickJS 集成 + JsEngine 包装器 | ✅ 完成 | 2026-03-04 |
| 44 | Q_INVOKABLE → QuickJS C 绑定 (28个API) | ✅ 完成 | 2026-03-04 |
| 45 | ScriptSandbox 脱 QObject/QThread, Qt::Qml移除 | ✅ 完成 | 2026-03-04 |
| 46 | D3D11 YUV 渲染器核心 | ✅ 完成 | 2026-03-04 |
| 47 | D3D11 集成窗口系统 (D3D11VideoWidget) | ✅ 完成 | 2026-03-04 |
| 48 | D3D11 帧抓取 (C++ staging texture) | ✅ 完成 | 2026-03-04 |
| 49 | 移除 QOpenGL (Qt::OpenGL/OpenGLWidgets) | ✅ 完成 | 2026-03-04 |
| 50 | StringUtils 桥接层 (strutil::toQ/fromQ) | ✅ 完成 | 2026-03-04 |
| 51 | transport/common QString→std::string | ✅ 完成 | 2026-03-04 |
| 52 | control/core/decoder QString→std::string | ✅ 完成 | 2026-03-04 |
| 53 | UI 边界适配 + CMake 清理 + 全面去Qt | ✅ 完成 | 2026-03-04 |
| 54 | 全量编译 + 最终依赖审计 | ✅ 完成 | 2026-03-04 |

### Step 53 扩展内容 (超出原计划)
- QSize→Size 全局替换 (transport/decoder/core/control ~50个文件)
- QPointF→PointF 全局替换 (~25个文件)
- QRectF→RectF 全局替换 (~6个文件)
- QVariant/QVariantMap→ConfigValue/ScriptValue/类型安全struct (10个文件)
- QDir/QFile/QFileInfo→std::filesystem (5个文件)
- QScopeGuard→ScopeGuard (2个文件)
- QList→std::vector (1个文件)
- QGuiApplication/QCursor→Win32 API (1个文件)
- QtGlobal→<cstdint> (1个文件)
- qFatal/qWarning→Logger宏 (3个文件)
- QMetaEnum→静态查找表 (2个文件)
- Qt::Key_*/MouseButton/Modifier→GameKey::/GameMouse::/GameMod:: (468处替换, 8个文件)
- keymap QString→std::string (1个文件)
- QImage→前向声明 (IVideoRenderer.h)
