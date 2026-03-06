# GameScrcpy UI 全面重构方案 — Fluent Focus 设计系统

> **版本**: v2.0 | **日期**: 2026-03-03 | **目标框架**: Qt 6.x + QSS + 自绘
> **设计语言**: Fluent Focus — 融合 Microsoft Fluent Design 2 + 游戏级操控体验

---

## 目录

1. [现状分析与痛点总结](#1-现状分析与痛点总结)
2. [设计系统: Fluent Focus Design Tokens](#2-设计系统-fluent-focus-design-tokens)
3. [方案 A: 导航式主界面 (NavigationView)](#3-方案-a-导航式主界面-navigationview)
4. [通用组件重构清单](#4-通用组件重构清单)
5. [VideoForm 投屏窗口重构](#5-videoform-投屏窗口重构)
6. [ToolForm 悬浮工具栏重构](#6-toolform-悬浮工具栏重构)
7. [键位映射编辑器重构](#7-键位映射编辑器重构)
8. [设置系统重构](#8-设置系统重构)
9. [对话框系统统一重构](#9-对话框系统统一重构)
10. [动效与过渡系统](#10-动效与过渡系统)
11. [主题系统 (深色/浅色/自定义)](#11-主题系统-深色浅色自定义)
12. [无障碍与国际化](#12-无障碍与国际化)
13. [脚本编辑器 Fluent Focus 重构](#13-脚本编辑器-fluent-focus-重构)
14. [选区管理工具与截图工具 Fluent Focus 重构](#14-选区管理工具与截图工具-fluent-focus-重构)
15. [键位配置侧边栏 (KeyMapSidePanel)](#15-键位配置侧边栏-keymapsidepanel)
16. [实施路线图](#16-实施路线图)

---

## 1. 现状分析与痛点总结

### 1.1 当前 UI 架构一览

| 组件 | 文件 | 当前状态 |
|------|------|----------|
| **主界面 Dialog** | `dialog.h/cpp/ui` | 400×520 固定窗口，单页卡片布局，功能入口分散 |
| **设置对话框 SettingsDialog** | `settingsdialog.h/cpp` | 模态 QDialog，代码式布局，参数项水平堆砌 |
| **终端对话框 TerminalDialog** | `terminaldialog.h/cpp` | 独立模态窗口，功能简单 |
| **投屏窗口 VideoForm** | `videoform.h/cpp/ui` | 自定义圆角窗口 + KeepRatioWidget + 皮肤系统 |
| **悬浮工具栏 ToolForm** | `toolform.h/cpp/ui` | 64px 固定宽窄条，StackedWidget 切换普通/键位模式 |
| **键位编辑器 KeyMapEditView** | `KeyMapEditView.h/cpp` | QGraphicsView 架构，支持拖拽/撤销 |
| **键位覆盖层 KeyMapOverlay** | `KeyMapOverlay.h/cpp` | 自绘半透明覆盖层 |
| **脚本编辑器 ScriptEditorDialog** | `scripteditordialog.h` | 内嵌 JS 高亮编辑器 + 自动补全 |
| **图像截取对话框 ImageCaptureDialog** | `imagecapturedialog.h` | 可缩放截图 + 模板匹配工具 |
| **选区编辑器 SelectionEditorDialog** | `selectioneditordialog.h` | 多图层选区管理 + 按钮/滑动管理 |
| **性能对话框 PerformanceDialog** | `PerformanceDialog.h/cpp` | GroupBox 网格布局，功能性展示 |
| **连接进度 ConnectionProgressWidget** | `ConnectionProgressWidget.h/cpp` | 阶段指示器 + 脉冲动画 |
| **脚本提示 ScriptTipWidget** | `ScriptTipWidget.h/cpp` | 全局单例浮窗，支持拖拽 |
| **磁性吸附 MagneticWidget** | `magneticwidget.h/cpp` | 窗口吸附基类 |
| **图标字体 IconHelper** | `iconhelper.h/cpp` | FontAwesome 单例 |
| **QSS 主题** | `modern_dark.qss` / `light.qss` | 两套完整主题，Zinc+Indigo 色系 |

### 1.2 核心痛点

| # | 痛点 | 具体表现 |
|---|------|----------|
| P1 | **主界面功能碎片化** | 设置、终端、高级选项分散在多个模态对话框，用户需要频繁开关窗口 |
| P2 | **信息层级不清** | 首页同时展示连接按钮 + 设备列表 + 工具栏，缺乏导航逻辑 |
| P3 | **设置体验差** | SettingsDialog 一行堆放码率/帧率/分辨率/编码，空间局促，无分组卡片 |
| P4 | **ToolForm 过窄** | 64px 宽度极限挤压，键位编辑模式展开到 90px 仍然局促 |
| P5 | **样式碎片化** | Dialog 用 `applyModernStyle()` 内联 QSS，SettingsDialog/TerminalDialog 各自独立 QSS，ToolForm 在代码中硬编码样式 |
| P6 | **缺乏统一动效** | 仅 ConnectionProgressWidget 有脉冲动画，其余窗口跳变切换 |
| P7 | **对话框体验陈旧** | 使用 QMessageBox 系统对话框和 QInputDialog，风格与暗色主题割裂 |
| P8 | **无响应式布局** | 主界面 400×520 固定比例，不适配大屏/高 DPI |
| P9 | **多设备管理弱** | 设备列表仅显示序列号/昵称文本，无状态图标/电量/缩略图预览 |
| P10 | **键位编辑与投屏割裂** | 键位编辑(KeyMapEditView)覆盖在视频上但没有专门的编辑面板，设置分散在 ToolForm 小弹窗中 |

---

## 2. 设计系统: Fluent Focus Design Tokens

### 2.1 设计哲学

**Fluent Focus** = Microsoft Fluent Design 2 的层级体系 + 游戏控制器级的操控精度

核心原则：
- **Layer (层级)**: 通过背景层、卡片层、悬浮层建立空间纵深
- **Reveal (揭示)**: 鼠标移入时边框光效揭示可交互区域
- **Motion (动效)**: 连贯的 Ease 曲线过渡，消除突兀跳变
- **Focus (聚焦)**: 所有操作围绕「当前设备」聚焦，减少上下文切换
- **Density (密度)**: 工具面板紧凑但不拥挤，投屏区域最大化

### 2.2 色彩系统 (Design Tokens)

```
┌─────────────────────────────────────────────────────────────┐
│                    Fluent Focus 色彩层级                      │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  Base Layer (最底层)                                         │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Dark: #09090b (zinc-950)   Light: #f4f4f5 (zinc-100) │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
│  Card Layer (卡片层)                                         │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Dark: #18181b (zinc-900)   Light: #ffffff           │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
│  Surface Layer (浮层)                                        │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Dark: #27272a (zinc-800)   Light: #fafafa           │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
│  Accent (强调色)                                            │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Primary: #6366f1 (indigo-500)                       │   │
│  │  Hover:   #818cf8 (indigo-400)                       │   │
│  │  Active:  #4f46e5 (indigo-600)                       │   │
│  │  Subtle:  rgba(99,102,241,0.12)                      │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
│  Semantic (语义色)                                           │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Success: #22c55e  Warning: #f59e0b  Error: #ef4444  │   │
│  │  Info:    #3b82f6  Online:  #22d3ee                  │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
│  Border                                                     │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Dark: #27272a  Hover: #3f3f46  Focus: #6366f1      │   │
│  │  Light: #e4e4e7  Hover: #d4d4d8  Focus: #6366f1     │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
│  Text                                                       │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Dark:  Primary #fafafa  Secondary #a1a1aa           │   │
│  │         Tertiary #71717a  Disabled #52525b           │   │
│  │  Light: Primary #09090b  Secondary #52525b           │   │
│  │         Tertiary #71717a  Disabled #a1a1aa           │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 2.3 排版系统

| Token | 字号 | 字重 | 行高 | 用途 |
|-------|------|------|------|------|
| `display` | 28px | 700 | 36px | 主标题 (App名称) |
| `title` | 20px | 600 | 28px | 页面标题 |
| `subtitle` | 16px | 600 | 24px | 卡片标题 |
| `body` | 14px | 400 | 22px | 正文/说明文字 |
| `bodyStrong` | 14px | 600 | 22px | 强调正文 |
| `caption` | 12px | 400 | 18px | 辅助说明、状态 |
| `captionStrong` | 12px | 600 | 18px | 辅助标签 |
| `mono` | 13px | 400 | 20px | 代码/终端 (JetBrains Mono) |

### 2.4 间距系统

| Token | 值 | 用途 |
|-------|------|------|
| `space-xs` | 4px | 紧凑元素间距 |
| `space-sm` | 8px | 组内间距 |
| `space-md` | 12px | 元素间距 |
| `space-lg` | 16px | 区块间距 |
| `space-xl` | 24px | 区域间距 |
| `space-2xl` | 32px | 页面边距 |

### 2.5 圆角系统

| Token | 值 | 用途 |
|-------|------|------|
| `radius-sm` | 4px | 小按钮/标签 |
| `radius-md` | 8px | 输入框/常规按钮 |
| `radius-lg` | 12px | 卡片/面板 |
| `radius-xl` | 16px | 大卡片/弹窗 |
| `radius-full` | 9999px | 圆形按钮/头像 |

### 2.6 阴影系统

| 层级 | Dark 模式 | Light 模式 | 用途 |
|------|-----------|-----------|------|
| `elevation-0` | 无 | 无 | 底层 |
| `elevation-1` | `0 1px 3px rgba(0,0,0,0.4)` | `0 1px 3px rgba(0,0,0,0.08)` | 卡片 |
| `elevation-2` | `0 4px 12px rgba(0,0,0,0.5)` | `0 4px 12px rgba(0,0,0,0.12)` | 弹出层 |
| `elevation-3` | `0 8px 24px rgba(0,0,0,0.6)` | `0 8px 24px rgba(0,0,0,0.16)` | 模态对话框 |

---

## 3. 方案 A: 导航式主界面 (NavigationView)

> **推荐度: ★★★★★** — 最全面的现代化方案，适合功能持续扩展

### 3.1 核心理念

采用 **左侧导航栏 + 右侧内容区** 的经典 NavigationView 布局（类似 Windows 11 设置），将所有功能整合到单窗口中，彻底消除模态对话框碎片化问题。

### 3.2 布局结构

```
┌──────────────────────────────────────────────────────────────────┐
│  ●  ●  ●     GameScrcpy                          ─  □  ✕      │  ← 自定义标题栏
├────────┬─────────────────────────────────────────────────────────┤
│        │                                                         │
│  🏠    │   [页面标题]                                             │
│  首页   │   ─────────────────────────────────────────────         │
│        │                                                         │
│  📱    │       ┌─────────────────────────────────────┐           │
│  设备   │       │                                     │           │
│        │       │        (内容区域)                     │           │
│  ⚙️    │       │                                     │           │
│  设置   │       │                                     │           │
│        │       │                                     │           │
│  ⌨️    │       └─────────────────────────────────────┘           │
│  终端   │                                                         │
│        │                                                         │
│  📊    │                                                         │
│  性能   │                                                         │
│        │                                                         │
├────────┤                                                         │
│        │                                                         │
│  🌐    │                                                         │
│  EN    │                                                         │
│        │                                                         │
└────────┴─────────────────────────────────────────────────────────┘
```

### 3.3 各页面设计

#### 3.3.1 首页 (Home)

```
┌─────────────────────────────────────────────────────────────┐
│                                                             │
│  首页                                                        │
│                                                             │
│  ┌──── 快速连接 ──────────────────────────────────────────┐ │
│  │                                                         │ │
│  │   ┌──────────────┐    ┌──────────────┐                 │ │
│  │   │   🔌 USB     │    │   📶 WiFi    │                 │ │
│  │   │   自动检测     │    │   输入地址     │                 │ │
│  │   │   连接        │    │   连接        │                 │ │
│  │   └──────────────┘    └──────────────┘                 │ │
│  │                                                         │ │
│  │   WiFi 地址: [  192.168.1.xxx   ] : [ 5555 ]  [连接]   │ │
│  │                                                         │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                                             │
│  ┌──── 已连接设备 ─────────────────────────────────────────┐ │
│  │                                                         │ │
│  │  ┌─────────┐  ┌─────────┐  ┌─────────┐               │ │
│  │  │ 📱      │  │ 📱      │  │   ＋    │               │ │
│  │  │ Pixel 8 │  │ Mi 14   │  │  添加   │               │ │
│  │  │ USB     │  │ WiFi    │  │  设备   │               │ │
│  │  │ ● 在线  │  │ ● 在线  │  │         │               │ │
│  │  └─────────┘  └─────────┘  └─────────┘               │ │
│  │                                                         │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                                             │
│  ┌──── 最近活动 ──────────────────────────────────────────┐ │
│  │  14:30  Pixel 8 已连接 (USB)                           │ │
│  │  14:28  正在推送 scrcpy-server...                       │ │
│  │  14:25  应用启动                                        │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**关键改进**:
- 快速连接区域整合 WiFi 地址输入（原来在 SettingsDialog 中分离），一处搞定
- 设备列表升级为**卡片网格**，展示设备名、连接方式、在线状态
- 设备卡片支持右键菜单（断开/昵称/删除）
- 底部活动日志替代原来隐藏的终端输出

#### 3.3.2 设备详情页 (Device Detail)

双击设备卡片进入设备详情页：

```
┌─────────────────────────────────────────────────────────────┐
│  ← 返回     Pixel 8 Pro                      ⚙ 设备设置     │
│                                                             │
│  ┌─ 设备信息 ──────────────────────────────────────────────┐ │
│  │  序列号: ABCDEF123456     型号: Pixel 8 Pro              │ │
│  │  连接方式: USB             分辨率: 1080×2400             │ │
│  │  Android: 14              IP: 192.168.1.105              │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                                             │
│  ┌─ 投屏控制 ──────────────────────────────────────────────┐ │
│  │                                                         │ │
│  │  码率   [ 8 ] [Mbps ▼]    帧率  [ 60 ]                 │ │
│  │  分辨率 [ 1080 ▼]         编码  [ H.264 ▼]             │ │
│  │                                                         │ │
│  │  ☑ 反向连接   ☑ 工具栏   ☐ 无边框   ☑ 显示FPS          │ │
│  │                                                         │ │
│  │  [ ▶ 开始投屏 ]    [ ■ 停止 ]    [ 🔑 键位映射 ]        │ │
│  │                                                         │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                                             │
│  ┌─ 键位配置 ──────────────────────────────────────────────┐ │
│  │  当前配置: [ default.json  ▼]  [新建] [刷新] [📁]       │ │
│  │                                                         │ │
│  │  随机偏移: ─────●───── 35                               │ │
│  │  轮盘平滑: ──────●──── 50                               │ │
│  │  拟人曲线: ────●────── 30                               │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**关键改进**:
- 原 SettingsDialog 中的视频参数和 ToolForm 中的键位设置合并到**同一页面**
- 参数使用 Fluent 分组卡片布局，每个卡片有清晰标题
- "开始投屏"成为设备专属操作而非全局操作

#### 3.3.3 设置页 (Settings)

```
┌─────────────────────────────────────────────────────────────┐
│  设置                                                        │
│                                                             │
│  ┌─ 外观 ─────────────────────────────────────────────────┐ │
│  │                                                         │ │
│  │  主题           [ 深色 ▼]                               │ │
│  │  强调色         ● ● ● ● ● ● (色板选择)                  │ │
│  │  窗口透明度     ─────●───── 95%                          │ │
│  │  字体大小       [ 标准 ▼]                                │ │
│  │                                                         │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                                             │
│  ┌─ 连接 ─────────────────────────────────────────────────┐ │
│  │                                                         │ │
│  │  自动检测设备     [  开  ]                               │ │
│  │  USB 自动连接     [  开  ]                               │ │
│  │  WiFi 自动回连    [  开  ]                               │ │
│  │  检测间隔         [ 1.5s ▼]                             │ │
│  │                                                         │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                                             │
│  ┌─ 高级 ─────────────────────────────────────────────────┐ │
│  │                                                         │ │
│  │  Server 路径     [  /env/scrcpy-server  ] [浏览]        │ │
│  │  ADB 路径        [  /env/adb/win/adb   ] [浏览]        │ │
│  │  日志级别         [ Info ▼]                              │ │
│  │  编码选项         [                    ]                 │ │
│  │                                                         │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                                             │
│  ┌─ 关于 ─────────────────────────────────────────────────┐ │
│  │                                                         │ │
│  │  GameScrcpy v2.x.x                                     │ │
│  │  © 2019-2026 Rankun                                    │ │
│  │  Apache License 2.0                                     │ │
│  │  [ 检查更新 ]  [  GitHub  ]                             │ │
│  │                                                         │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

#### 3.3.4 终端页 (Terminal)

```
┌─────────────────────────────────────────────────────────────┐
│  ADB 终端                                                    │
│                                                             │
│  设备: [ Pixel 8 - ABCDEF  ▼]                               │
│                                                             │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │  $ adb devices                                          │ │
│  │  List of devices attached                               │ │
│  │  ABCDEF123456    device                                 │ │
│  │                                                         │ │
│  │  $ adb shell ls /sdcard                                 │ │
│  │  DCIM                                                   │ │
│  │  Download                                               │ │
│  │  ...                                                    │ │
│  │                                                         │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                                             │
│  $ [  输入命令...                    ] [执行] [终止] [清空]   │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

#### 3.3.5 性能页 (Performance)

将 PerformanceDialog 从独立弹窗变成内嵌页面，增加实时图表：

```
┌─────────────────────────────────────────────────────────────┐
│  性能监控                     设备: [ Pixel 8 ▼]             │
│                                                             │
│  ┌─ FPS 实时曲线 ─────────────────────────────────────────┐ │
│  │  60 ┤ ╭─╮  ╭──╮   ╭──────╮                            │ │
│  │  30 ┤╯   ╰──╯  ╰───╯      ╰───                        │ │
│  │   0 ┼────────────────────────────                       │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                                             │
│  ┌──── 视频 ─────┐  ┌──── 网络 ─────┐  ┌──── 输入 ─────┐  │
│  │ FPS     60    │  │ 延迟   2.1ms │  │ 延迟   0.5ms │  │
│  │ 解码  0.8ms   │  │ 发送  12 KB  │  │ 已处理  234  │  │
│  │ 渲染  0.3ms   │  │ 接收  1.2 MB │  │ 丢弃    0    │  │
│  │ 总帧  15234   │  │ 待发  0 B    │  │              │  │
│  │ 丢帧  3       │  │              │  │              │  │
│  └───────────────┘  └──────────────┘  └──────────────┘  │
│                                                             │
│  帧池使用率: ████████░░  80%                                │
│                                                             │
│  [ 重置计数器 ]                                              │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 3.4 NavigationView 实现方案

#### 代码架构

```
ui/
├── MainWindow.h/cpp              # 新主窗口 (替代 Dialog)
├── NavigationView.h/cpp          # 左侧导航组件
├── pages/
│   ├── HomePage.h/cpp            # 首页
│   ├── DeviceDetailPage.h/cpp    # 设备详情
│   ├── SettingsPage.h/cpp        # 设置页 (替代 SettingsDialog)
│   ├── TerminalPage.h/cpp        # 终端页 (替代 TerminalDialog)
│   └── PerformancePage.h/cpp     # 性能页 (替代 PerformanceDialog)
├── components/                    # 通用组件库
│   ├── FluentCard.h/cpp          # Fluent 卡片容器
│   ├── FluentButton.h/cpp        # Fluent 按钮 (Primary/Secondary/Subtle/Danger)
│   ├── FluentToggle.h/cpp        # Fluent 拨动开关
│   ├── FluentSlider.h/cpp        # Fluent 滑块
│   ├── FluentComboBox.h/cpp      # Fluent 下拉框
│   ├── FluentInput.h/cpp         # Fluent 输入框
│   ├── FluentDialog.h/cpp        # Fluent 模态对话框
│   ├── FluentInfoBar.h/cpp       # Fluent 通知条 (替代 QMessageBox)
│   ├── FluentBadge.h/cpp         # 状态徽标
│   ├── FluentProgressRing.h/cpp  # 环形进度
│   ├── DeviceCard.h/cpp          # 设备信息卡片
│   └── ActivityLog.h/cpp         # 活动日志组件
├── theme/
│   ├── ThemeManager.h/cpp        # 主题管理器 (单例)
│   ├── DesignTokens.h            # 设计 Token 常量
│   ├── fluent_dark.qss           # 深色主题 QSS
│   └── fluent_light.qss          # 浅色主题 QSS
└── ...existing files...
```

#### NavigationView 类设计

```cpp
class NavigationView : public QWidget {
    Q_OBJECT
public:
    enum NavItem { Home, Devices, Settings, Terminal, Performance };

    explicit NavigationView(QWidget* parent = nullptr);
    void setCurrentItem(NavItem item);
    bool isCollapsed() const;

signals:
    void itemSelected(NavItem item);
    void collapseToggled(bool collapsed);

private:
    // 可折叠：展开时显示图标+文字(200px)，折叠时仅图标(48px)
    bool m_collapsed = false;
    QList<NavButton*> m_items;
    QPropertyAnimation* m_collapseAnim;
};
```

---

## 4. 通用组件重构清单

> 无论选择哪个方案，以下 Fluent Focus 组件库都是基础设施，需优先建设。

### 4.1 FluentCard — 统一卡片容器

**替代**: 当前 `QFrame[frameShape="StyledPanel"]` 的各处 ad hoc 用法

```cpp
class FluentCard : public QFrame {
    Q_OBJECT
    Q_PROPERTY(bool hovered READ isHovered NOTIFY hoveredChanged)
    Q_PROPERTY(bool elevated READ isElevated WRITE setElevated)
public:
    explicit FluentCard(QWidget* parent = nullptr);

    void setTitle(const QString& title);
    void setSubtitle(const QString& subtitle);
    void setHeaderWidget(QWidget* widget);  // 自定义右上角控件
    void setContentWidget(QWidget* widget);
    void setCollapsible(bool collapsible);  // 支持折叠
    void setCollapsed(bool collapsed);

signals:
    void hoveredChanged(bool hovered);
    void clicked();
    void collapsed(bool isCollapsed);

protected:
    void paintEvent(QPaintEvent*) override;  // Reveal 光效边框
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
};
```

**视觉规范**:
- 背景: Card Layer (`#18181b` dark / `#ffffff` light)
- 边框: 1px solid Border (`#27272a` dark / `#e4e4e7` light)
- 圆角: `radius-lg` (12px)
- 内边距: `space-xl` (24px)
- 标题: `subtitle` token (16px 600)
- Hover 时: 边框渐变为 Accent Subtle 光效

### 4.2 FluentButton — 按钮体系

**替代**: 全局 QPushButton 的各处硬编码样式

```cpp
class FluentButton : public QPushButton {
    Q_OBJECT
public:
    enum Style { Primary, Secondary, Subtle, Accent, Danger, Ghost };
    enum Size { Small, Medium, Large };

    explicit FluentButton(const QString& text = "", QWidget* parent = nullptr);
    void setButtonStyle(Style style);
    void setButtonSize(Size size);
    void setIcon(const QIcon& icon);
    void setLoading(bool loading);  // 加载状态（旋转图标）

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
};
```

| Style | 背景 | 边框 | 文字 | 用途 |
|-------|------|------|------|------|
| Primary | Accent渐变 | none | white | USB连接/WiFi连接/开始投屏 |
| Secondary | Surface | Border | Primary text | 刷新/保存/编辑 |
| Subtle | transparent | none | Secondary text | 工具栏按钮 |
| Accent | Accent solid | none | white | 确认/保存 |
| Danger | transparent | Error border | Error text | 停止/断开/删除 |
| Ghost | transparent | none | Tertiary text | 底栏/辅助操作 |

| Size | 高度 | 内边距 | 字号 |
|------|------|--------|------|
| Small | 32px | 8px 12px | 12px |
| Medium | 40px | 10px 20px | 14px |
| Large | 52px | 14px 28px | 16px |

### 4.3 FluentToggle — 拨动开关

**替代**: `QCheckBox` (对于布尔设置项)

```cpp
class FluentToggle : public QWidget {
    Q_OBJECT
    Q_PROPERTY(bool checked READ isChecked WRITE setChecked NOTIFY toggled)
    Q_PROPERTY(qreal handlePos READ handlePos WRITE setHandlePos)
public:
    explicit FluentToggle(QWidget* parent = nullptr);
    bool isChecked() const;
    void setChecked(bool checked);
    void setLabel(const QString& label);

signals:
    void toggled(bool checked);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
};
```

**视觉**: 44×24px 药丸形，关 (#3f3f46) → 开 (#6366f1)，手柄 200ms QPropertyAnimation

### 4.4 FluentSlider — 滑块

**替代**: 当前 `showAntiDetectSettings()` 中硬编码样式的 QSlider

```cpp
class FluentSlider : public QWidget {
    Q_OBJECT
public:
    explicit FluentSlider(QWidget* parent = nullptr);
    void setRange(int min, int max);
    void setValue(int value);
    void setLabel(const QString& label);
    void setShowValue(bool show);
    void setSuffix(const QString& suffix);  // 如 "%"

signals:
    void valueChanged(int value);

protected:
    void paintEvent(QPaintEvent*) override;
};
```

**视觉**:
- 轨道高度: 4px，填充区 Accent 色
- 手柄: 16px 圆形，白色填充 + Accent 边框
- Hover 时手柄放大到 20px (QPropertyAnimation)
- 右侧数值标签: `captionStrong` token

### 4.5 FluentComboBox — 下拉框

**替代**: 全局 QComboBox

在保持 QComboBox 功能的基础上增强视觉：
- 下拉列表使用 elevation-2 阴影
- 选中项左侧竖线高亮 (3px Accent)
- 展开/收起平滑动画
- 搜索过滤功能（可选）

### 4.6 FluentInput — 输入框

**替代**: 全局 QLineEdit

增强特性:
- 底部下划线焦点指示器 (Material 风格) 或边框渐变 (Fluent 风格)
- 前缀/后缀图标支持
- 清除按钮 (✕)
- 错误状态红色边框 + 错误提示文字
- 支持内联验证

### 4.7 FluentDialog — 模态对话框

**替代**: `QMessageBox`, `QInputDialog`, `showStatusDialog()`

```cpp
class FluentDialog : public QDialog {
    Q_OBJECT
public:
    enum DialogType { Info, Success, Warning, Error, Confirm, Input };

    explicit FluentDialog(QWidget* parent = nullptr);

    // Builder 模式
    FluentDialog& setType(DialogType type);
    FluentDialog& setTitle(const QString& title);
    FluentDialog& setMessage(const QString& message);
    FluentDialog& setIcon(const QIcon& icon);
    FluentDialog& addButton(const QString& text, FluentButton::Style style,
                            std::function<void()> callback);
    FluentDialog& setInputField(const QString& placeholder = "");
    QString inputText() const;

    // 便捷静态方法
    static void info(QWidget* parent, const QString& title, const QString& msg);
    static void error(QWidget* parent, const QString& title, const QString& msg);
    static bool confirm(QWidget* parent, const QString& title, const QString& msg);
    static QString input(QWidget* parent, const QString& title, const QString& prompt,
                        const QString& defaultText = "");

protected:
    void showEvent(QShowEvent*) override;   // 进场动画
    void paintEvent(QPaintEvent*) override; // 半透明遮罩
};
```

**视觉**:
- 全屏半透明遮罩 `rgba(0,0,0,0.5)`
- 白卡片居中 (Card Layer)，elevation-3 阴影
- 进场: 从下方 20px 滑入 + 透明度 0→1 (200ms OutCubic)
- 退场: 向下淡出 (150ms InCubic)

### 4.8 FluentInfoBar — 通知条

**替代**: 各处的状态提示 (`updateStatusBar`, `outLog` 提示)

```cpp
class FluentInfoBar : public QWidget {
    Q_OBJECT
public:
    enum Severity { Info, Success, Warning, Error };
    enum Position { Top, Bottom, TopRight };

    static void show(QWidget* parent, Severity severity,
                     const QString& message, int durationMs = 3000,
                     Position pos = TopRight);
};
```

**视觉**: 右上角 Toast 通知，左侧竖条颜色表示类型，自动淡出

### 4.9 DeviceCard — 设备信息卡片

```cpp
class DeviceCard : public FluentCard {
    Q_OBJECT
public:
    struct DeviceInfo {
        QString serial;
        QString displayName;
        QString model;
        QString connectionType;  // "USB" / "WiFi"
        bool isOnline;
        bool isStreaming;
    };

    void setDeviceInfo(const DeviceInfo& info);

signals:
    void connectClicked(const QString& serial);
    void disconnectClicked(const QString& serial);
    void detailClicked(const QString& serial);
};
```

**视觉**:
- 左侧设备图标 (手机轮廓 SVG)
- 中间设备名 + 序列号 + 连接方式
- 右上角状态徽标: ● 在线(绿) / ● 离线(灰) / ● 投屏中(蓝动画)
- 右侧操作按钮: ▶ 开始投屏 / ■ 停止
- Hover 时显示更多操作 (设置/断开/重命名)

---

## 5. VideoForm 投屏窗口重构

### 5.1 当前问题
- `videoform.ui` 样式硬编码在 XML 中
- 皮肤系统 (`updateStyleSheet`) 使用边框图片 + 深色固色背景切换
- 圆角窗口通过 QSS 实现，与自定义标题栏逻辑耦合
- 加载状态用简单的 QWidget 覆盖，无动画

### 5.2 重构方案

#### 5.2.1 窗口框架

```
┌─────────────────────────────────────────────────────────┐
│  📱 Pixel 8 Pro                   FPS: 60  ─ □ ✕       │  ← 自定义标题栏 (可拖拽)
├─────────────────────────────────────────────────────────┤
│                                                         │
│                                                         │
│                                                         │
│                 (视频渲染区域)                             │
│                 QYUVOpenGLWidget                         │
│                                                         │
│                                                         │
│                                                         │
│                                                         │
├─────────────────────────────────────────────────────────┤
│   ⏪  🏠  📱  🖥   ───────────────────────  🎮  ⚙      │  ← 底部操作栏
└─────────────────────────────────────────────────────────┘
```

**改进点**:
1. **自定义标题栏**: 显示设备名称 + 实时 FPS + 窗口控制按钮
2. **底部操作栏**: 将 ToolForm 的常用按钮（返回/主页/多任务/全屏）嵌入底部栏
3. **键位编辑按钮**: 底部栏右侧，点击后展开侧边面板 (替代 ToolForm 弹出逻辑)
4. **取消独立 ToolForm 窗口**: ToolForm 功能全部融入 VideoForm 自身

#### 5.2.2 侧边面板 (替代 ToolForm)

当点击 🎮 键位按钮时，从右侧滑出面板：

```
┌─────────────────────────────────────────┬─────────────────────┐
│                                         │ 键位映射             │
│                                         │ ─────────────────── │
│                                         │ 配置: [xxx.json ▼]  │
│          (视频渲染区域)                   │ [新建][刷新][📁][存] │
│           稍微缩小                        │ ─────────────────── │
│                                         │ 拖拽组件:            │
│                                         │ [点击] [长按] [脚本] │
│                                         │ [轮盘] [视角] [眼睛] │
│                                         │ ─────────────────── │
│                                         │ ▸ 拟人参数           │
│                                         │ ▸ 显示设置           │
│                                         │ ─────────────────── │
│                                         │ [显示键位][性能]     │
├─────────────────────────────────────────┴─────────────────────┤
│   ⏪  🏠  📱  🖥   ───────────────────────  🎮✓  ⚙           │
└───────────────────────────────────────────────────────────────┘
```

**动画**: 侧边面板 `QPropertyAnimation` 从右侧滑入 (250ms OutCubic)

#### 5.2.3 加载状态重新设计

替代当前简单的 `m_loadingWidget`：

```
┌─────────────────────────────────────────────────────────┐
│                                                         │
│                                                         │
│                    ◉                                    │  ← 环形进度动画
│              正在连接设备...                               │
│                                                         │
│        [检查设备] → [推送服务] → [启动连接] → [开始]       │  ← 阶段指示
│                  ●     ●        ○         ○             │
│                                                         │
│              已用时: 3.2s          [取消]                 │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

将 ConnectionProgressWidget 的逻辑直接整合到 VideoForm 的加载状态中。

#### 5.2.4 全屏模式

```
┌──────────────────────────────────────────────────────────────────┐
│                                                                  │
│                                                                  │
│                                                                  │
│                      (全屏视频渲染)                                │
│                                                                  │
│                                                                  │
│                                                                  │
│                                             FPS: 60  ← 淡入淡出  │
│                                                                  │
│   ┌──────────────────────────────────────────────────────┐       │
│   │  ⏪  🏠  📱  🖥           🎮  ⚙  ✕退出全屏         │ ← 自动隐藏 │
│   └──────────────────────────────────────────────────────┘       │
└──────────────────────────────────────────────────────────────────┘
```

- 底部工具栏默认隐藏，鼠标移到底部时自动淡入
- FPS 显示右下角半透明叠加
- ESC 或点击"退出全屏"返回窗口模式

---

## 6. ToolForm 悬浮工具栏重构

### 6.1 当前问题分析

| 问题 | 详情 |
|------|------|
| 宽度极限 | 64px 常规模式，90px 键位模式，空间严重不足 |
| 浮动窗口管理复杂 | MagneticWidget 吸附逻辑 + 独立窗口位置同步 |
| 样式硬编码 | 每个按钮在 `.ui` 和 `.cpp` 中独立设置 styleSheet |
| 设置弹窗嵌套 | `showAntiDetectSettings()` 300×580 弹窗，6个滑块松散堆叠 |
| 键位面板局促 | ComboBox 固定 100px，拖拽标签竖直排列挤压空间 |

### 6.2 重构方向：融合到 VideoForm

**核心变更**: 取消 ToolForm 作为独立悬浮窗口，功能全部整合到 VideoForm 中。

#### 6.2.1 底部操作栏 (常驻)

```cpp
class VideoBottomBar : public QWidget {
    Q_OBJECT
public:
    explicit VideoBottomBar(QWidget* parent = nullptr);

    void setKeyMapMode(bool active);
    void setFullScreenMode(bool fullScreen);

signals:
    void goBack();
    void goHome();
    void appSwitch();
    void fullScreen();
    void keyMapToggled(bool active);
    void settingsClicked();

private:
    FluentButton* m_backBtn;      // ⏪ 返回
    FluentButton* m_homeBtn;      // 🏠 主页
    FluentButton* m_appSwitchBtn; // 📱 多任务
    FluentButton* m_fullScreenBtn;// 🖥 全屏
    FluentButton* m_keyMapBtn;    // 🎮 键位 (toggle)
    FluentButton* m_settingsBtn;  // ⚙ 设置
};
```

按钮使用 Subtle 风格，44×36px，Hover 时 Surface 背景。

#### 6.2.2 侧边键位面板 (KeyMapSidePanel)

```cpp
class KeyMapSidePanel : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int panelWidth READ panelWidth WRITE setPanelWidth)
public:
    explicit KeyMapSidePanel(QWidget* parent = nullptr);

    void setExpanded(bool expanded);  // 展开/收起动画
    bool isExpanded() const;

    // 键位配置管理
    void refreshConfigList();
    QString currentConfig() const;
    void setCurrentConfig(const QString& filename);

signals:
    void configChanged(const QString& filename);
    void saveRequested();
    void overlayToggled(bool visible);
    void overlayOpacityChanged(int value);
    void editModeToggled(bool active);

private:
    // 配置管理区
    FluentComboBox* m_configCombo;
    FluentButton* m_newBtn, *m_refreshBtn, *m_folderBtn, *m_saveBtn;

    // 拖拽组件区
    QList<DraggableLabel*> m_dragLabels;

    // 折叠设置组
    FluentCard* m_humanParamsCard;   // 拟人参数
    FluentCard* m_displayCard;       // 显示设置

    // 底部操作
    FluentToggle* m_overlayToggle;
    FluentButton* m_perfBtn;

    // 动画
    QPropertyAnimation* m_expandAnim;
    int m_expandedWidth = 260;
    int m_collapsedWidth = 0;
};
```

**拖拽组件网格化**:
```
当前 (竖直堆叠):          重构后 (2列网格):
  [点击]                    [点击] [长按]
  [长按]                    [脚本] [轮盘]
  [脚本]                    [视角] [眼睛]
  [轮盘]
  [视角]
  [小眼睛]
```

---

## 7. 键位映射编辑器重构

### 7.1 当前架构

- **KeyMapEditView** (QGraphicsView): 作为透明覆盖层 attach 到 VideoForm
- **KeyMapItems.h**: 包含所有键位元素类（~1037行单文件）
- **KeyMapOverlay** (QWidget): 半透明的只读键位提示层
- **KeyMapBase** (QGraphicsObject): 键位元素基类

### 7.2 问题

| 问题 | 详情 |
|------|------|
| 巨型头文件 | `KeyMapItems.h` 1037行，包含4种键位类型 + 辅助工具 + 对话框 |
| 编辑器属性面板缺失 | 双击键位会弹出简陋的内联编辑器，无统一属性面板 |
| 冲突提示不直观 | 冲突红色高亮仅在元素边框，易忽略 |
| 缺少操作提示 | 用户不知道可以右键删除、双击编辑、滚轮调整大小 |

### 7.3 重构方案

#### 7.3.1 文件拆分

```
ui/
├── keymap/
│   ├── KeyMapEditView.h/cpp        # 编辑视图 (保持)
│   ├── KeyMapOverlay.h/cpp         # 只读覆盖层 (保持)
│   ├── KeyMapBase.h/cpp            # 基类 (保持)
│   ├── items/
│   │   ├── ClickItem.h/cpp         # 点击/长按
│   │   ├── SteerWheelItem.h/cpp    # 轮盘
│   │   ├── CameraItem.h/cpp        # 视角
│   │   ├── FreeLookItem.h/cpp      # 小眼睛
│   │   └── ScriptItem.h/cpp        # 脚本
│   ├── KeyMapHelper.h/cpp          # 辅助工具类
│   ├── KeyMapPropertyPanel.h/cpp   # 属性编辑面板 (新增)
│   └── KeyConflictIndicator.h/cpp  # 冲突提示组件 (新增)
```

#### 7.3.2 属性面板 (KeyMapPropertyPanel)

在侧边面板中，选中键位元素后显示其属性编辑面板：

```
┌──── 属性编辑 ──────────────────┐
│                                │
│  类型: 点击 (Script)           │
│  ─────────────────────         │
│  热键: [ W ]  [修改]           │
│  位置: X [0.45]  Y [0.62]     │
│  ─────────────────────         │
│  脚本:                         │
│  ┌──────────────────────────┐ │
│  │ mapi.click(0.45, 0.62)  │ │
│  │                          │ │
│  └──────────────────────────┘ │
│  [编辑脚本]  [选择区域]        │
│  ─────────────────────         │
│  ☑ 自动启动   触发: 按下       │
│  ─────────────────────         │
│  [删除键位]                    │
│                                │
└────────────────────────────────┘
```

#### 7.3.3 键位编辑入口提示

首次进入编辑模式时显示操作引导覆盖层：

```
┌─────────────────────────────────────────────────────────┐
│                                                         │
│           🎮 键位编辑模式                                │
│                                                         │
│   • 从右侧面板拖拽组件到画面上                             │
│   • 拖拽已有键位调整位置                                   │
│   • 双击键位编辑属性                                      │
│   • 右键删除键位                                          │
│   • Ctrl+Z 撤销 / Ctrl+Y 重做                             │
│   • 滚轮调整轮盘大小                                      │
│                                                         │
│                   [知道了]                                │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

#### 7.3.4 冲突可视化增强

- 冲突键位: 红色脉冲边框 + 底部 InfoBar 提示 "按键 W 与 XX 冲突"
- 选中键位: Accent 蓝色边框 + 放大 1.05x + 阴影
- Hover 键位: Surface 背景 + 工具提示
- 拖拽中: 半透明 0.7 + 虚线定位辅助线
---

## 8. 设置系统重构

### 8.1 当前问题

- `SettingsDialog` 是模态弹窗，一行堆放多个参数控件
- 视频参数 (码率/帧率/分辨率) 和显示选项 (工具栏/无边框) 混在一起
- WiFi 连接功能同时出现在主界面和设置对话框中，入口重复
- `showAntiDetectSettings()` 在 ToolForm 中又创建一个 300×580 弹窗
- 配置持久化分散在 `Config` 和 `ConfigCenter` 两个单例

### 8.2 重构方案：分层设置架构

```
┌─────────────────────────────────────────────────────────────┐
│                                                             │
│                    设置系统分层架构                            │
│                                                             │
│  ┌─ 全局设置 (Settings Page / App级) ─────────────────────┐ │
│  │  • 外观主题 (深色/浅色/跟随系统)                         │ │
│  │  • 强调色选择                                           │ │
│  │  • 语言切换                                             │ │
│  │  • 自动检测设备 开/关                                    │ │
│  │  • USB 即插即连 开/关                                    │ │
│  │  • WiFi 自动回连 开/关                                   │ │
│  │  • ADB/Server 路径                                      │ │
│  │  • 日志级别                                             │ │
│  │  存储: ConfigCenter (userdata.ini General组)              │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                                             │
│  ┌─ 设备设置 (Device Detail Page / 设备级) ─────────────────┐ │
│  │  • 视频参数 (码率/帧率/分辨率/编码)                       │ │
│  │  • 显示选项 (工具栏/无边框/FPS/窗口置顶)                  │ │
│  │  • 设备昵称                                             │ │
│  │  存储: Config (userdata.ini 设备serial组)                 │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                                             │
│  ┌─ 键位设置 (KeyMap Side Panel / 键位级) ──────────────────┐ │
│  │  • 键位配置文件选择                                      │ │
│  │  • 拟人参数 (随机偏移/平滑/曲线)                          │ │
│  │  • 覆盖层显示 (键位透明度/弹窗透明度)                     │ │
│  │  存储: ConfigCenter (keymap.ini 或 JSON)                  │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 8.3 设置项组件化

每个设置项统一使用 `SettingRow` 组件：

```cpp
class SettingRow : public QWidget {
    Q_OBJECT
public:
    explicit SettingRow(QWidget* parent = nullptr);

    void setTitle(const QString& title);
    void setDescription(const QString& desc);  // 可选，灰色小字
    void setIcon(const QIcon& icon);           // 可选，左侧图标
    void setWidget(QWidget* controlWidget);     // 右侧控件 (Toggle/ComboBox/Input...)
};
```

渲染效果：
```
┌─────────────────────────────────────────────────────────┐
│  🎨  主题                                  [ 深色 ▼]    │
│      根据您的偏好选择界面主题                              │
├─────────────────────────────────────────────────────────┤
│  🎯  强调色                          ● ● ● ● ● ●       │
│      自定义界面强调色彩                                   │
├─────────────────────────────────────────────────────────┤
│  📱  自动检测设备                          [  ●开  ]     │
│      自动扫描并连接新的 USB 设备                          │
└─────────────────────────────────────────────────────────┘
```

### 8.4 删除 SettingsDialog

在方案 A/B 中，`SettingsDialog` 完全由以下替代：
- **全局设置** → Settings Page（导航页面）
- **视频参数** → Device Detail Page 的投屏控制卡片
- **WiFi 连接** → Home Page 的快速连接卡片
- **工具按钮 (获取IP/开启ADBD)** → 设备详情页的操作菜单

在方案 C 中，`SettingsDialog` 简化为全局设置弹窗（删除视频参数和 WiFi 部分）。

---

## 9. 对话框系统统一重构

### 9.1 当前问题

| 原对话框 | 问题 |
|----------|------|
| `QMessageBox` 系统对话框 | 白色系统主题，与暗色 UI 割裂 |
| `QInputDialog` | 同上 |
| `showStatusDialog()` | 封装了 QMessageBox，但仍是系统对话框 |
| `ScriptEditorDialog` | 独立大窗口，1066行头文件，功能过度集中 |
| `ImageCaptureDialog` | 1319行头文件，截图+模板匹配+区域选择全在一个类 |
| `SelectionEditorDialog` | 2792行头文件！最复杂的对话框 |
| `showAntiDetectSettings()` | ToolForm中临时创建的QDialog |

### 9.2 统一替代方案

#### 9.2.1 简单对话框 → FluentDialog

```cpp
// 取代 QMessageBox
FluentDialog::info(this, tr("连接成功"), tr("设备 %1 已连接").arg(deviceName));
FluentDialog::error(this, tr("连接失败"), tr("未找到可用设备"));

// 取代 QInputDialog
QString name = FluentDialog::input(this, tr("设备昵称"), tr("输入昵称:"), currentName);

// 取代 confirm 场景
if (FluentDialog::confirm(this, tr("确认断开"), tr("是否断开设备 %1?").arg(serial))) {
    // ...
}
```

#### 9.2.2 脚本编辑器 → ScriptEditorPanel

将 ScriptEditorDialog 拆分：
- **ScriptEditorPanel**: 纯代码编辑面板（可嵌入侧边面板或独立窗口）
- **JsSyntaxHighlighter**: 保持独立（已拆分）
- **ScriptAutoComplete**: 自动补全逻辑独立

#### 9.2.3 图像/选区工具 → 独立工具窗口

`ImageCaptureDialog` 和 `SelectionEditorDialog` 保持为独立窗口（它们的功能确实需要大空间），但统一应用 Fluent Focus 样式：

```cpp
class FluentToolWindow : public QWidget {
    // 统一的工具窗口基类
    // - 自定义标题栏（深色）
    // - 统一样式
    // - 自动保存/恢复窗口位置
};

class ImageCaptureWindow : public FluentToolWindow { ... };
class SelectionEditorWindow : public FluentToolWindow { ... };
```

#### 9.2.4 设置弹窗 → FluentSettingsSheet

将 `showAntiDetectSettings()` 的弹窗改为底部抽屉式表单：

```
┌────────────────────────────────────────────────────┐
│  (视频画面)                                         │
│                                                    │
│                                                    │
├────────────────────────────────────────────────────┤  ← 可向上拖拽
│  ▬  设置                                    ✕     │  ← 把手
│  ─────────────────────────────────────────────     │
│  随机偏移      ─────●───── 35                      │
│  轮盘平滑      ──────●──── 50                      │
│  拟人曲线      ────●────── 30                      │
│  滑动曲线      ──────●──── 50                      │
│  键位透明度    ───●──────── 60                      │
│  弹窗透明度    ──●───────── 70                      │
│  ─────────────────────────────────────────────     │
│  [确定]                                            │
└────────────────────────────────────────────────────┘
```

---

## 10. 动效与过渡系统

### 10.1 动效原则

| 原则 | 说明 |
|------|------|
| **目的性** | 动效服务于功能，不为装饰。进出场表示位置关系，过渡表示状态变化 |
| **一致性** | 全局统一曲线和时长，不同组件使用相同的 Motion Token |
| **流畅性** | 60fps 硬性目标，所有动画使用 QPropertyAnimation 硬件加速 |
| **可选性** | 提供"减少动效"开关，动效持续时间降为 0 |

### 10.2 Motion Tokens

| Token | 时长 | 曲线 | 用途 |
|-------|------|------|------|
| `motion-fast` | 100ms | `QEasingCurve::OutCubic` | 微交互 (hover 反馈) |
| `motion-normal` | 200ms | `QEasingCurve::OutCubic` | 状态切换 (toggle/tab) |
| `motion-slow` | 300ms | `QEasingCurve::OutCubic` | 面板展开/收起 |
| `motion-enter` | 250ms | `QEasingCurve::OutQuart` | 元素进场 (弹窗/通知) |
| `motion-exit` | 150ms | `QEasingCurve::InCubic` | 元素退场 |
| `motion-spring` | 400ms | `QEasingCurve::OutBack` | 弹性效果 (拖拽回弹) |

### 10.3 具体动效清单

| 场景 | 当前表现 | 重构后 |
|------|----------|--------|
| **页面切换** (方案A) | 无 (新建) | 新页面从右侧 20px 滑入 + 淡入，旧页面淡出 |
| **设备卡片展开** (方案B) | 无 (新建) | 高度 64→200px 动画 + 内容交错淡入 |
| **侧边面板展开** | 无 (ToolForm 固定宽 64/90) | 从右侧滑入 0→260px，同时视频区缩小 |
| **对话框弹出** | 无过渡，直接 exec() | 遮罩淡入 + 卡片从下方滑入 |
| **通知条** | 无 (QMessageBox) | 右上角滑入 + 自动淡出 + 堆叠错位 |
| **设备上线** | 列表刷新 (全部重建) | 新设备卡片淡入 + 上移 |
| **设备断开** | 列表刷新 (全部重建) | 断开卡片变灰 + 淡出 |
| **连接进度** | ConnectionProgressWidget 脉冲 | 保持，增加阶段切换的过渡色 |
| **按钮 Hover** | QSS :hover (瞬变) | 50ms 背景色过渡 |
| **Toggle 切换** | 无 (QCheckBox 勾选) | 手柄滑动 200ms + 颜色渐变 |
| **输入框焦点** | QSS :focus (瞬变) | 底部线条从中心展开 200ms |
| **键位拖拽** | 瞬间移动 | 拖拽时 0.7 透明度 + 放下时 OutBack 弹回 |

### 10.4 实现方案

```cpp
// theme/MotionTokens.h
namespace Motion {
    constexpr int Fast   = 100;  // ms
    constexpr int Normal = 200;
    constexpr int Slow   = 300;
    constexpr int Enter  = 250;
    constexpr int Exit   = 150;
    constexpr int Spring = 400;

    inline QEasingCurve defaultCurve() { return QEasingCurve(QEasingCurve::OutCubic); }
    inline QEasingCurve enterCurve()   { return QEasingCurve(QEasingCurve::OutQuart); }
    inline QEasingCurve exitCurve()    { return QEasingCurve(QEasingCurve::InCubic); }
    inline QEasingCurve springCurve()  { return QEasingCurve(QEasingCurve::OutBack); }

    // 便捷函数
    inline QPropertyAnimation* fadeIn(QWidget* w, int duration = Enter) {
        auto* anim = new QPropertyAnimation(w, "windowOpacity");
        anim->setDuration(duration);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->setEasingCurve(enterCurve());
        return anim;
    }

    inline QPropertyAnimation* slideIn(QWidget* w, int offset = 20, int duration = Enter) {
        auto* effect = new QGraphicsOpacityEffect(w);
        w->setGraphicsEffect(effect);
        auto* group = new QParallelAnimationGroup(w);
        // 透明度
        auto* fade = new QPropertyAnimation(effect, "opacity");
        fade->setDuration(duration);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        // 位移
        auto* slide = new QPropertyAnimation(w, "pos");
        slide->setDuration(duration);
        slide->setStartValue(w->pos() + QPoint(offset, 0));
        slide->setEndValue(w->pos());
        slide->setEasingCurve(enterCurve());
        group->addAnimation(fade);
        group->addAnimation(slide);
        return nullptr; // 示意，实际返回 group
    }
}
```

---

## 11. 主题系统 (深色/浅色/自定义)

### 11.1 当前问题

- `modern_dark.qss` 和 `light.qss` 是两套完全独立的 QSS 文件
- Dialog、SettingsDialog、TerminalDialog 各自内联 styleSheet，主题切换时无法同步
- ToolForm 组件在 C++ 代码中硬编码颜色值 (`#27272a`, `#6366f1` 等)
- 无运行时主题切换能力

### 11.2 ThemeManager 架构

```cpp
class ThemeManager : public QObject {
    Q_OBJECT
public:
    enum Theme { Dark, Light, System };
    enum AccentColor { Indigo, Blue, Violet, Rose, Emerald, Amber };

    static ThemeManager& instance();

    // 主题控制
    void setTheme(Theme theme);
    Theme currentTheme() const;
    bool isDarkMode() const;

    // 强调色
    void setAccentColor(AccentColor color);
    AccentColor currentAccentColor() const;

    // Token 访问 (运行时根据当前主题返回对应颜色)
    QColor baseColor() const;
    QColor cardColor() const;
    QColor surfaceColor() const;
    QColor accentColor() const;
    QColor accentHoverColor() const;
    QColor borderColor() const;
    QColor textPrimary() const;
    QColor textSecondary() const;
    QColor textTertiary() const;
    QColor successColor() const;
    QColor warningColor() const;
    QColor errorColor() const;

    // 获取编译后的 QSS
    QString compiledStyleSheet() const;

    // 应用主题到应用
    void applyTheme();

signals:
    void themeChanged(Theme newTheme);
    void accentColorChanged(AccentColor newColor);

private:
    // QSS 模板编译：将 {{accent}}, {{base}} 等占位符替换为实际颜色值
    QString compileQss(const QString& templateQss) const;
};
```

### 11.3 QSS 模板化

```css
/* fluent_template.qss — 使用占位符 */

QWidget {
    background-color: {{base}};
    color: {{text-primary}};
    font-family: "Microsoft YaHei UI", "SF Pro Display", sans-serif;
}

QPushButton#primary {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 {{accent}}, stop:1 {{accent-active}});
    border: none;
    color: #ffffff;
}

QPushButton#primary:hover {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
        stop:0 {{accent-hover}}, stop:1 {{accent}});
}

FluentCard {
    background-color: {{card}};
    border: 1px solid {{border}};
    border-radius: 12px;
}
```

### 11.4 强调色预设

| 名称 | Primary | Hover | Active | Subtle |
|------|---------|-------|--------|--------|
| **Indigo** (默认) | `#6366f1` | `#818cf8` | `#4f46e5` | `rgba(99,102,241,0.12)` |
| **Blue** | `#3b82f6` | `#60a5fa` | `#2563eb` | `rgba(59,130,246,0.12)` |
| **Violet** | `#8b5cf6` | `#a78bfa` | `#7c3aed` | `rgba(139,92,246,0.12)` |
| **Rose** | `#f43f5e` | `#fb7185` | `#e11d48` | `rgba(244,63,94,0.12)` |
| **Emerald** | `#10b981` | `#34d399` | `#059669` | `rgba(16,185,129,0.12)` |
| **Amber** | `#f59e0b` | `#fbbf24` | `#d97706` | `rgba(245,158,11,0.12)` |

### 11.5 消除硬编码颜色

**分三步执行**:

1. **Phase 1**: 建立 `ThemeManager` 和 `DesignTokens.h`，所有颜色常量集中定义
2. **Phase 2**: 将各文件中硬编码的 `setStyleSheet()` 调用替换为 ThemeManager Token 引用
3. **Phase 3**: 将 ToolForm、DraggableLabel 等组件中的内联字符串样式替换为 ThemeManager 编译后的 QSS 类

**当前硬编码热点**:

| 文件 | 硬编码行数 | 内容 |
|------|-----------|------|
| `dialog.cpp` `applyModernStyle()` | ~80行 | 完整 QSS 字符串 |
| `settingsdialog.cpp` `applyStyle()` | ~100行 | 完整 QSS 字符串 |
| `terminaldialog.cpp` `applyStyle()` | ~70行 | 完整 QSS 字符串 |
| `toolform.cpp` DraggableLabel 构造 | ~10行/个 | 内联样式 |
| `toolform.cpp` `initKeyMapPalette()` | ~50行 | ComboBox/Button 样式 |
| `toolform.cpp` `showAntiDetectSettings()` | ~20行 | Dialog 样式 |
| `videoform.cpp` `applyDarkStyle()` | ~10行 | 窗口样式 |
| `toolform.ui` | 各按钮 | <styleSheet> 属性 |
| `videoform.ui` | 根窗口 | <styleSheet> 属性 |

---

## 12. 无障碍与国际化

### 12.1 无障碍 (Accessibility)

| 措施 | 说明 |
|------|------|
| **键盘导航** | 所有交互元素支持 Tab 焦点 + Enter 激活 |
| **焦点指示器** | 2px Accent 描边，与 Hover 样式区分 |
| **对比度** | 文字/背景对比度 ≥ 4.5:1 (WCAG AA)，当前 `#a1a1aa on #18181b` = 5.3:1 ✓ |
| **屏幕阅读器** | 所有按钮设置 `setAccessibleName()` / `setAccessibleDescription()` |
| **减少动效** | 设置项 "减少动效"，启用后所有动画时长降为 0 |
| **字体缩放** | 支持 90%/100%/110%/120% 四档字体大小 |
| **高对比度** | 预留高对比度主题接口 |

### 12.2 国际化 (i18n)

当前架构已支持 `tr()` + `changeEvent(QEvent::LanguageChange)` + `retranslateUi()`。重构中需保持：

| 措施 | 说明 |
|------|------|
| **所有新组件** 使用 `tr()` | FluentButton、FluentCard 的文字内容 |
| **NavigationView** | 导航项文字通过 `retranslateUi()` 动态更新 |
| **Settings Page** | 设置项标题/描述全部 `tr()` |
| **FluentDialog** | 按钮文字 (确定/取消) 使用 `tr()` |
| **FluentInfoBar** | 通知文字使用 `tr()` |
| **操作引导覆盖层** | 帮助文字 `tr()` |
| **RTL 布局** | 预留 `setLayoutDirection(Qt::RightToLeft)` 支持 |

### 12.3 日期/数字本地化

- 性能页的数值显示使用 `QLocale::toString()`
- 时间显示使用 `QLocale::toString(QTime, QLocale::ShortFormat)`

---

## 13. 脚本编辑器 Fluent Focus 重构

> **文件**: `scripteditordialog.h` (~1011 行 header-only)
> **状态**: ✅ 已实现

### 13.1 设计概述

脚本编辑器是 GameScrcpy 的核心工具窗口，提供 JavaScript 脚本编写、自动补全、代码片段库和调试能力。重构后全面应用 Fluent Focus 设计语言。

### 13.2 整体布局

```
┌─────────────────────────────────────────────────────────────┐
│  ⬛ 脚本编辑器                         ─  □  ✕             │  ← DWM 深色标题栏
├────────────┬────────────────────────────────────────────────┤
│            │                                                │
│  快捷指令   │  ┌─ 行号 ─┬───────────────────────────────┐   │
│ (滚动面板)  │  │   1    │ // 示例脚本                     │   │
│            │  │   2    │ mapi.click(0.5, 0.5);          │   │
│ ┌────────┐ │  │   3    │ mapi.holdpress(0.3, 0.7);      │   │
│ │触摸操作 │ │  │ > 4    │ mapi.slide(0.2, 0.8, 0.5...   │   │  ← 当前行高亮
│ │ · 点击  │ │  │   5    │                                │   │
│ │ · 长按  │ │  └────────┴───────────────────────────────┘   │
│ │ · 滑动  │ │                                                │
│ │ · 缩放  │ │  ┌────────────────────┐                       │
│ │ · 释放  │ │  │  mapi.click        │ ← 自动补全弹窗        │
│ └────────┘ │  │  mapi.holdpress     │   (ThemeManager 主题) │
│            │  │  mapi.slide         │                       │
│ ┌────────┐ │  └────────────────────┘                       │
│ │按键操作 │ │                                                │
│ │ · 按键  │ │  ┌────────────────────────────────────────┐   │
│ │ · 组合  │ │  │ 获取工具 │ 脚本目录 │ 清空 │  取消 │ 保存 │   │
│ └────────┘ │  └────────────────────────────────────────┘   │
│  ...       │                                                │
├────────────┴────────────────────────────────────────────────┤
```

### 13.3 主题化方案

对话框级 QSS 在构造函数中通过 `ThemeManager::instance()` 动态生成：

| 元素 | Token 映射 | 效果 |
|------|-----------|------|
| 对话框背景 | `tm.base()` | 最深层背景 |
| QGroupBox 面板 | `tm.surface()` + `tm.border()` | 1px 边框 + 8px 圆角 |
| QGroupBox 标题 | `tm.textPrimary()` + `font-weight:600` | 分组标题强调 |
| 滚动条轨道 | 透明 | 隐藏轨道 |
| 滚动条滑块 | `tm.scrollThumb()` | 4px 宽 + 2px 圆角 |
| QLabel 通用 | `background: transparent` | 防止继承背景色 |

### 13.4 代码编辑器子组件

| 子组件 | 旧实现 | Fluent Focus 实现 |
|--------|--------|-------------------|
| **行号区** | `#1e1e1e` 固定背景 | `tm.surface()` 背景 + `tm.textTertiary()` 文字 |
| **当前行高亮** | `#2d2d30` 固定 | `tm.surface()` 背景 (半透明叠加) |
| **括号匹配** | `#3f3f46` 固定 | `tm.navHover()` 高亮色 |
| **自动补全弹窗** | `#1e1e1e` + `#094771` 选中 | `tm.surface()` 背景 + `tm.navActive()` 选中行 |
| **语法高亮** | VS Code 色系 (保留) | 不变 — 语法色是编辑器标准，不随主题切换 |

### 13.5 快捷指令面板

左侧 250px 宽滚动面板，包含按 QGroupBox 分组的代码片段按钮：

- **面板背景**: `tm.surface()` + `tm.border()` 边框 + 8px 圆角
- **分组标题**: `tm.textPrimary()` 600 字重
- **片段按钮**: Ghost 风格 — 透明底 + `tm.textPrimary()` 文字 + hover 时 `tm.navHover()` 背景
- **布局**: 每组 12px 内边距，按钮间距 4px

---

## 14. 选区管理工具与截图工具 Fluent Focus 重构

> **文件**: `selectioneditordialog.h` (~2742 行) + `imagecapturedialog.h` (~1319 行)
> **状态**: ✅ 已实现

### 14.1 设计概述

选区管理工具（SelectionEditorDialog）和截图工具（ImageCaptureDialog）是 GameScrcpy 的两大全屏覆盖式工具窗口。由于需要大面积操作空间，它们保持独立窗口形式，但全面应用 Fluent Focus 主题。

### 14.2 选区管理工具布局

```
┌─────────────────────────────────────────────────────────────┐
│  ⬛ 选区管理工具                        ─  □  ✕             │
├──────────────┬──────────────────────────────────────────────┤
│              │                                              │
│   预览画面    │    管理面板                                   │
│   (可缩放)    │   ┌──────┬──────┬──────┐                    │
│              │   │ 选区  │ 按钮  │ 滑动  │  ← 分段控件       │
│              │   └──────┴──────┴──────┘                    │
│              │   ┌──────────────────────┐                   │
│  [截取帧]    │   │ #1 选区 A  (0.1,0.2) │ ← 列表            │
│  [缩放+/-]   │   │ #2 选区 B  (0.3,0.5) │                   │
│              │   └──────────────────────┘                   │
│              │                                              │
│              │   [ 获取位置 ] [ 新建选区 ]                    │
│              │   [ 导出图片 ] [ 导出截图 ]                    │
│              │                                              │
│              │   坐标: (0.452, 0.321)                       │
│              │                                              │
├──────────────┴──────────────────────────────────────────────┤
│  状态栏: 缩放 200% | 模式: 浏览 | 选区数: 3                  │
└─────────────────────────────────────────────────────────────┘
```

### 14.3 ThemeManager 主题化方案

两个工具窗口中的 **所有硬编码颜色** 均替换为 `ThemeManager::instance()` 动态调用：

| 旧值 | Token 映射 | 用途 |
|------|-----------|------|
| `#18181b` / `#09090b` | `tm.base()` | 对话框/覆盖层背景 |
| `#27272a` | `tm.surface()` | 工具栏、按钮默认背景 |
| `#3f3f46` | `tm.border()` | 边框、分隔线 |
| `#fafafa` / `#e4e4e7` | `tm.textPrimary()` | 主文字 |
| `#a1a1aa` | `tm.textTertiary()` | 提示文字、坐标标签 |
| `#71717a` | `tm.textSecondary()` | 次要文字 |
| `#6366f1` | `tm.accentPrimary()` | 强调色 (选中边框、确认按钮) |
| `#818cf8` | `tm.accentHover()` | Hover 状态 |

### 14.4 截图工具 (ImageCaptureDialog)

包含三个内部类，全部已主题化：

| 类名 | 用途 | 主题化范围 |
|------|------|-----------|
| `ImageCaptureOverlay` | 主截图覆盖层 | 工具栏、缩放按钮、滚动区、确认/取消按钮 |
| `PositionResultDialog` | 坐标结果对话框 | 对话框背景、标签、复制/关闭按钮 |
| `PositionSelectOverlay` | 坐标选取覆盖层 | 工具栏、坐标标签、确认/取消按钮 |

### 14.5 选区管理工具 (SelectionEditorDialog)

| 组件 | Token 映射 |
|------|-----------|
| 工具栏背景 | `tm.base()` |
| 分段 Tab 按钮 (激活) | `tm.surface()` + accent 底部指示线 |
| 分段 Tab 按钮 (非激活) | transparent + `tm.textSecondary()` |
| 列表项 | `tm.surface()` 背景 + `tm.border()` 边框 |
| 操作按钮 (确认) | `tm.accentPrimary()` 背景 + 白色文字 |
| 操作按钮 (取消/次要) | `tm.surface()` 背景 + `tm.textPrimary()` 文字 |
| 预览控件选区描边 | `tm.accentPrimary()` |
| 预览控件按钮标记 | `Accent::Success` |
| 预览控件冲突色 | `Accent::Error` |
| 坐标显示 | `tm.textSecondary()` + monospace 字体 |

### 14.6 DWM 标题栏适配

所有 `WinUtils::setDarkBorderToWindow(hwnd, true)` 调用已改为：
```cpp
WinUtils::setDarkBorderToWindow(hwnd, tm.isDarkMode());
```
确保浅色主题下标题栏正确跟随。

---

## 15. 键位配置侧边栏 (KeyMapSidePanel)

> **文件**: `KeyMapSidePanel.h/cpp` (~100 + 394 行)
> **状态**: ✅ 已实现

### 15.1 设计概述

KeyMapSidePanel 是 VideoForm 的核心辅助面板，**替代原 ToolForm 的全部键位编辑功能**。从视频窗口右侧以动画滑出（0→260px），包含配置管理、可拖拽键位组件、拟人参数调节和显示设置。

### 15.2 布局结构

```
┌──────────────────────┐
│  键位配置        ✕    │  ← 标题行 + Ghost 关闭按钮
├──────────────────────┤
│  [ default.json   ▼] │  ← QComboBox 配置选择
│  [ + ] [ ↻ ] [ 📁 ]  │  ← 新建 / 刷新 / 打开目录
│  [     保存      ]   │  ← FluentButton::Primary
├──────────────────────┤
│  显示键位     [●  ]  │  ← FluentToggle
├──────────────────────┤
│  拖拽到视频窗口       │  ← 区段标题
│  ┌─────┐ ┌─────┐    │
│  │ 点击 │ │ 长按 │    │  ← 2列 DraggableLabel 网格
│  ├─────┤ ├─────┤    │
│  │ 脚本 │ │ 轮盘 │    │
│  ├─────┤ ├─────┤    │
│  │ 视角 │ │小眼睛│    │
│  └─────┘ └─────┘    │
├──────────────────────┤
│  拟人化参数            │  ← 区段标题
│  随机偏移  ──●── 35   │  ← FluentSlider (0-100)
│  轮盘平滑  ───●── 50  │
│  轮盘曲线  ──●── 30   │
│  滑动曲线  ───●── 50  │
├──────────────────────┤
│  透明度设置            │
│  键位提示  ───●── 60  │
│  脚本弹窗  ──●── 70   │
└──────────────────────┘
```

### 15.3 交互行为

| 行为 | 说明 |
|------|------|
| **展开/收起** | 即时切换（无动画），宽度在 0 和 260px 之间直接跳变 |
| **展开时窗口拓宽** | VideoForm 自动增加 260px 宽度容纳面板，视频区域不被遮挡 |
| **收起** | 点击 ✕ 或再次点击侧边栏键位按钮，宽度 260→0px |
| **拖拽组件** | DraggableLabel 支持拖拽到视频窗口的 KeyMapEditView 上生成键位 |
| **滑块自动保存** | 所有 FluentSlider 值变化时立即写入 `ConfigCenter` |
| **配置切换** | QComboBox 切换触发 `configChanged` 信号，VideoForm 刷新键位 |

### 15.4 主题化方案

面板自绘背景 (`paintEvent`)，不使用 QSS：

```cpp
void KeyMapSidePanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    auto& tm = ThemeManager::instance();
    p.fillRect(rect(), QColor(tm.surface()));     // Surface 层背景
    p.setPen(QPen(QColor(tm.border()), 1));
    p.drawLine(0, 0, 0, height());                // 左侧 1px 分隔线
}
```

| 元素 | Token |
|------|-------|
| 面板背景 | `tm.surface()` |
| 左边框 | `tm.border()` |
| 标题文字 | `tm.textPrimary()` 13px 700 |
| 区段标题 | `tm.textSecondary()` 11px 600 |
| 滚动条 | `tm.scrollThumb()` 4px 宽 |
| QComboBox | `tm.inputBg()` 背景 + `tm.inputBorder()` 边框 + `tm.accentPrimary()` hover |
| 工具按钮 | `tm.inputBg()` 背景 + `tm.inputBorder()` 边框 |
| 分隔线 | `tm.borderSoft()` 1px |

### 15.5 与 VideoForm 的集成

面板作为 `hWrapper` QHBoxLayout 的成员：

```
┌────────────────────────────┬────┬──────────────────────┐
│       keepRatioWidget       │侧边│   KeyMapSidePanel     │
│     (视频 + 键位编辑)       │按钮│   (260px, 可折叠)      │
│                             │栏  │                        │
└────────────────────────────┴────┴──────────────────────┘
              hWrapper QHBoxLayout
```

面板展开/收起时 VideoForm 窗口宽度同步变化 (±260px)。

---

## 16. 实施路线图

### 16.1 Phase 0: 基础设施 (第 1 周)

| 任务 | 产出 |
|------|------|
| 建立 `theme/` 目录 | `ThemeManager.h/cpp`, `DesignTokens.h`, `MotionTokens.h` |
| 建立 `components/` 目录 | `FluentCard`, `FluentButton`, `FluentToggle`, `FluentSlider` |
| QSS 模板系统 | `fluent_template.qss` + `ThemeManager::compileQss()` |
| 深色/浅色主题 | 两套完整 Token 值 |

### 16.2 Phase 1: 组件库 (第 2 周)

| 任务 | 产出 |
|------|------|
| `FluentComboBox` | 增强下拉框 |
| `FluentInput` | 增强输入框 |
| `FluentDialog` | 替代 QMessageBox |
| `FluentInfoBar` | 通知条系统 |
| `FluentProgressRing` | 环形进度 |
| `SettingRow` | 设置项统一组件 |
| 基础动效工具 | `Motion::fadeIn/slideIn/expandHeight` |

### 16.3 Phase 2: 主界面重构 (第 3-4 周)
| 任务 | 产出 |
|------|------|
| `MainWindow` | 替代 Dialog，NavigationView + QStackedWidget |
| `NavigationView` | 左侧导航栏 (可折叠) |
| `HomePage` | 快速连接 + 设备卡片网格 + 活动日志 |
| `DeviceDetailPage` | 设备信息 + 投屏控制 + 键位配置 |
| `SettingsPage` | 全局设置 (替代 SettingsDialog) |
| `TerminalPage` | 嵌入式终端 (替代 TerminalDialog) |
| `PerformancePage` | 性能监控 (替代 PerformanceDialog) |
| `DeviceCard` | 设备信息卡片组件 |
| 迁移 `Dialog` 所有逻辑 | ADB 进程管理、设备监听、系统托盘 |

### 16.4 Phase 3: VideoForm 重构 (第 4-5 周)

| 任务 | 产出 |
|------|------|
| `VideoBottomBar` | 底部操作栏 (替代 ToolForm 常用按钮) |
| `KeyMapSidePanel` | 侧边键位面板 (替代 ToolForm 键位模式) |
| `KeyMapPropertyPanel` | 键位属性编辑面板 |
| 自定义标题栏 | 设备名 + FPS + 窗口控制 |
| 加载状态整合 | ConnectionProgressWidget → 内嵌动画 |
| 全屏模式增强 | 自动隐藏工具栏 + 浮动 FPS |
| 删除 ToolForm | 功能完全迁移后删除 |

### 16.5 Phase 4: 对话框 & 工具窗口 (第 5-6 周)

| 任务 | 产出 |
|------|------|
| 全局替换 QMessageBox | 使用 `FluentDialog` |
| 全局替换 QInputDialog | 使用 `FluentDialog::input()` |
| `ScriptEditorPanel` | 拆分脚本编辑器 |
| `FluentToolWindow` 基类 | 工具窗口统一样式 |
| ImageCaptureWindow 样式统一 | 应用 Fluent Focus 主题 |
| SelectionEditorWindow 样式统一 | 应用 Fluent Focus 主题 |

### 16.6 Phase 5: 消除硬编码 & 打磨 (第 6 周)

| 任务 | 产出 |
|------|------|
| 清除所有内联 `setStyleSheet()` | 统一使用 ThemeManager |
| 键位编辑器文件拆分 | `KeyMapItems.h` → 独立文件 |
| 动效打磨 | 所有过渡场景补全动画 |
| 浅色主题完善 | 测试所有组件的浅色表现 |
| 无障碍审查 | 键盘导航 + 对比度 + 屏幕阅读器 |
| 性能测试 | 动画帧率 + 内存占用 |
| 国际化翻译更新 | 中/英 `.ts` 文件 |

### 16.7 文件变更总览

**新增文件** (~30个):
```
ui/MainWindow.h/cpp
ui/NavigationView.h/cpp (方案A)
ui/pages/HomePage.h/cpp
ui/pages/DeviceDetailPage.h/cpp
ui/pages/SettingsPage.h/cpp
ui/pages/TerminalPage.h/cpp
ui/pages/PerformancePage.h/cpp
ui/components/FluentCard.h/cpp
ui/components/FluentButton.h/cpp
ui/components/FluentToggle.h/cpp
ui/components/FluentSlider.h/cpp
ui/components/FluentComboBox.h/cpp
ui/components/FluentInput.h/cpp
ui/components/FluentDialog.h/cpp
ui/components/FluentInfoBar.h/cpp
ui/components/FluentBadge.h/cpp
ui/components/FluentProgressRing.h/cpp
ui/components/DeviceCard.h/cpp
ui/components/ActivityLog.h/cpp
ui/components/SettingRow.h/cpp
ui/components/VideoBottomBar.h/cpp
ui/components/KeyMapSidePanel.h/cpp
ui/keymap/KeyMapPropertyPanel.h/cpp
ui/keymap/KeyConflictIndicator.h/cpp
ui/keymap/items/ClickItem.h/cpp
ui/keymap/items/SteerWheelItem.h/cpp
ui/keymap/items/CameraItem.h/cpp
ui/keymap/items/FreeLookItem.h/cpp
ui/keymap/items/ScriptItem.h/cpp
ui/keymap/KeyMapHelper.h/cpp
ui/theme/ThemeManager.h/cpp
ui/theme/DesignTokens.h
ui/theme/MotionTokens.h
ui/theme/fluent_template.qss
```

**重大修改文件** (~10个):
```
ui/videoform.h/cpp/ui      — 整合底部栏、侧边面板、标题栏
ui/KeyMapEditView.h/cpp    — 关联属性面板
ui/KeyMapOverlay.h/cpp     — 适配 ThemeManager 颜色
ui/ScriptTipWidget.h/cpp   — 适配 ThemeManager 颜色
ui/widgets/iconhelper.h/cpp — 可能替换为 SVG 图标系统
app/main.cpp               — 初始化 ThemeManager
app/config.h/cpp           — 增加主题/强调色配置项
common/ConfigCenter.h/cpp  — 增加主题配置方法
```

**删除/弃用文件** (~6个):
```
ui/dialog.h/cpp/ui          — 方案A/B 中被 MainWindow 替代
ui/settingsdialog.h/cpp     — 被 SettingsPage 替代
ui/terminaldialog.h/cpp     — 被 TerminalPage 替代
ui/PerformanceDialog.h/cpp  — 被 PerformancePage 替代
ui/toolform.h/cpp/ui        — 被 VideoBottomBar + KeyMapSidePanel 替代
resources/qss/modern_dark.qss — 被 fluent_template.qss 替代
resources/style/modern_dark.qss — 同上
```

---

## 附录: Fluent Focus 设计参考

| 参考来源 | 用途 |
|----------|------|
| [Microsoft Fluent Design 2](https://fluent2.microsoft.design/) | 层级体系、Motion 系统 |
| [Windows 11 Settings App](https://learn.microsoft.com/windows/apps/design/) | NavigationView 布局 |
| [shadcn/ui](https://ui.shadcn.com/) | 组件设计语言 (Zinc/Indigo 色系来源) |
| [Tailwind CSS Colors](https://tailwindcss.com/docs/customizing-colors) | 色板系统 |
| [Apple HIG](https://developer.apple.com/design/human-interface-guidelines/) | 侧边面板交互 |


