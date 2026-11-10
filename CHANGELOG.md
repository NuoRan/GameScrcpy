# 更新日志

本文件记录每个版本的主要变化，格式参照 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循[语义化版本](https://semver.org/lang/zh-CN/)。

## [未发布]

### AI

- 设置里的“模型”一栏改成下拉框加“获取”按钮；选好模型后，下面只列出这个模型支持的深度思考、思考强度和长上下文（100 万 token）选项。每个模型服务各自记住这些选项。
- 小助手输入框左下角可以给当前这段对话换模型、开关深度思考、调强度和长上下文，跟着对话一起保存；“设为默认”写回设置，新对话用设置里的默认值。
- 所有对话都保留在电脑上。“最近对话”菜单列出最新 20 段，“查看全部…”在单独的窗口里列出全部。
- 气泡和小助手卡片打开、收起时有动画；开启“减少动态效果”时只淡入淡出。

### 修复

- 没有打开投屏窗口时，从方案页“编辑逻辑…”打开较大的方案会卡死并占满内存（Qt 6.10.1 的问题，已绕开）。
- ESP32 触控盒闲置几分钟后会话断开、第一下触控失败：盒子不出声时会重新询问，必要时切回握手波特率再升回高速。

### English summary

AI settings now pick the model from a drop-down with a Get button and show only the thinking options the chosen model supports: deep thinking, reasoning effort and a one-million-token context. Each conversation can switch its own model and options from the composer and make them the default. Every conversation is kept; the Recent menu lists the newest 20 and Show all opens the rest. The bubble and the card animate. Fixed: the logic editor no longer hangs on larger profiles when no mirror window is open, and the ESP32 touch box no longer drops the session after a few idle minutes.

## [2.0.0] - 2026-11-10

GameScrcpy 2.0 是一次完全重写：电脑端程序、手机端服务、通信协议和伴侣应用都是新的。

### 投屏与连接

- 新的投屏链路：显卡硬件解码，解码出的画面直接交给界面显示；支持 H.264、H.265、AV1，带宽不够时先降分辨率、再降帧率。
- 声音（Android 11 及以上）。
- USB、无线调试（配对码或二维码）与 WiFi 直连三种连接；WiFi 直连用 UDP 加纠错传画面，断线后自动续上。
- 多台手机：独立窗口或宫格，同步操作，对选中的手机一起打开应用、安装 APK、发送文件、运行任务。
- 录像：录下画面、声音和操作，导出 MP4，可以用来回放测试逻辑。
- 手机文件管理、应用管理、状态浮层，把应用开在单独的虚拟屏幕上；测真实延迟。
- 手机伴侣应用：手机上的光标与键位提示、输入法、扫码连接。

### 键位映射

- 在投屏窗口里直接编辑键位，随时试玩；撤销重做、对齐、冲突检查。
- 组件库：点击、按住、长按、连发、滑动、双指缩放、WASD 轮盘、右键行走、视角、自由视角、技能、方向轮盘、多点同按、动作轮盘、临时光标、手柄摇杆、手柄视角。
- 按游戏自动切换方案，每台手机记住自己的选择。
- 手柄（Xbox、PlayStation、Switch Pro）驱动键位。

### 蓝图与脚本

- 蓝图与 TypeScript 是同一份逻辑，两边随时切换；每个键位一个文件，默认动作可见可删；函数库。
- 识图找色、Windows OCR 文字识别、定时任务。
- 断点、单步、实时数值；用录像回放测试。
- 命令行工具 `gs`：新建、类型检查、回放测试；脚本接口文档。

### AI

- 内置 AI 小助手（接你自己的 OpenAI 或 Anthropic 兼容接口），能读当前窗口的内容、调用工具、展示思考过程。
- 本地 MCP 服务，供 Claude、Cursor 等 AI 工具操作方案、蓝图、测试与录像。

### 触控方式与硬件

- ESP32 触控盒：程序内刷写固件，S3、S2、P4 走 USB。
- 实验性（还没有在足够多的真机和硬件上验证）：AOA 触控、蓝牙 ESP32（经典 ESP32、C3、C6）、采集卡画面、手柄直通。

### 扩展

- 扩展包 `.gsext`（主题、画面滤镜、方案与补丁、逻辑扩展、原生 DLL 扩展）与整合包 `.gsbundle`；原生扩展在单独的低权限进程里运行，安装时做安全检查；可以一步步找出出问题的扩展。

### 界面

- 全新的深色优先界面，可选浅色主题与六种强调色；命令面板（Ctrl+K）、可自定义的快捷键。
- 内置图文帮助（F1）、九段新手引导；中文与英文界面。
- 安装包（默认只为当前用户安装）与便携版，都自带 adb 和运行库。

### 不兼容的变化

- 1.x 的键位方案不能导入，请在新的键位编辑器里重新摆放；常见游戏类型有现成的模板可以起步。
- 1.x 的脚本接口已经取消，2.0 的逻辑用新的 TypeScript 接口（见 `sdk/docs`）或蓝图来写。

### English summary

GameScrcpy 2.0 is a complete rewrite of the PC app, the phone server, the protocol and the companion app. Highlights: a zero-copy hardware-decoded mirroring pipeline with H.264, H.265 and AV1; audio; USB, wireless debugging and WiFi direct; several phones in windows or a grid; recording to MP4; an in-place key mapping editor with a large component library; blueprints that are the same thing as TypeScript, with vision, OCR, tasks and a debugger; a built-in AI assistant and a local MCP server; the ESP32 touch box with in-app flashing; `.gsext` extensions with sandboxed native extensions; a new dark-first interface with built-in help and guided tours; an installer and a portable build. Experimental: AOA touch, Bluetooth ESP32 boards, capture cards and gamepad passthrough. Profiles and scripts from 1.x cannot be imported.

## 1.x

1.x 的更新记录见 [v1.3.4 标签里的 CHANGELOG](https://github.com/NuoRan/GameScrcpy/blob/v1.3.4/CHANGELOG.md)。

[2.0.0]: https://github.com/NuoRan/GameScrcpy/releases/tag/v2.0.0
