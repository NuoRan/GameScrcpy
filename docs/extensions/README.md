# 扩展开发

简体中文 · [English](README.en.md)

GameScrcpy 的扩展是一个带 `manifest.json` 的文件夹，打包后是一个 `.gsext` 文件。扩展可以只带数据（主题、画面滤镜、方案、帮助文章），可以带 TypeScript 写的逻辑（在低权限的子进程里运行，界面由 GameScrcpy 照扩展给的描述来画），也可以带一个用 C 接口写的原生 DLL。这一组文档讲 2.1.0 里扩展能做什么、每一种怎么做、怎么测试和发布。

| 文档 | 内容 |
|:--|:--|
| [清单参考](manifest.md) | `manifest.json` 的每个字段、每种贡献、文字翻译与上限 |
| [逻辑扩展指南](logic.md) | 应用脚本与会话脚本、面板与界面位置、程序事件、扩展之间的服务、本机连接、语音、硬件、功能引导与帮助 |
| [原生扩展指南](native.md) | C 接口：入口与贡献表、宿主函数、长任务、事件、设置、设备提供者、硬件占用、输入设备、服务、语音引擎、连接方式、媒体 |
| [测试扩展](testing.md) | `gs validate`、`gs check`，模拟宿主 `gs ext test`，在程序里试 |
| [打包与发布](publishing.md) | `gs pack`、签名、`gamescrcpy://` 安装链接、更新、整合包 |

逐个函数的说明不在这里重复：脚本接口见 [sdk/docs](../../sdk/docs/README.md)（类型声明在 [sdk/types](../../sdk/types/gamescrcpy.d.ts)），原生接口见 C 头文件 [gamescrcpy_ext.h](../../sdk/include/gamescrcpy_ext.h)。两者都由 `gs api write sdk` 从程序里的接口表生成，和程序始终一致。

## 扩展能做什么

一个扩展可以同时带下面几类东西，清单里每一类都要在 `requires` 里声明对应的扩展点（见下一节）。

### 数据

| 能力 | 清单里写 | 扩展点 |
|:--|:--|:--|
| 颜色主题：深色、浅色各一组设计令牌 | `contributes.themes` | `theme` |
| 画面滤镜：HLSL 像素着色器，参数可以在设置里调 | `contributes.filters` | `filter` |
| 方案，以及对别的扩展方案的补丁 | `contributes.profiles`、`contributes.profilePatches` | `profile` |
| 组件：用 TypeScript 写的组件，出现在键位编辑器的组件库里 | `contributes.components` | `component` |
| 任务：按时间表或手动运行的流程，出现在任务页 | `contributes.tasks` | `task` |
| 帮助文章：出现在帮助页的“扩展”一组 | `contributes.help` | `help` |
| 功能引导：在主窗口里一步步照亮扩展的卡片、页面与设置 | `contributes.tours` | `tour` |
| 设置：出现在设置 › 扩展与扩展的设置窗口，可以按手机分开 | `settings` | — |
| 名称、说明和各种标题的译文 | `l10n` | — |

方案页、键位编辑器的组件库和任务设置也能把你做好的方案、组件和任务直接导出成扩展包，可以拿来当起点。

### 逻辑

| 能力 | 怎么用 | 扩展点 / 权限 |
|:--|:--|:--|
| 脚本：每台手机一份的会话脚本，或整个程序一份的应用脚本 | `contributes.scripts` | `script` |
| 面板与界面位置：投屏窗口侧边面板、右键菜单、状态区、工具栏按钮，主窗口的页面、设置里的一节、扩展设置窗口、设备卡片的设置、卡片的“更多”菜单、托盘菜单 | `contributes.panels` + `ui.*` | `panel` |
| 服务：给别的扩展（和用户自己的逻辑）调用的函数 | `contributes.services` + `ext.provide` / `ext.call` | — |
| 程序事件：手机连上、方案换了、前台应用变了…… | `events.on` | 别的手机与窗口要 `events.app` |
| 本机传入连接：在 127.0.0.1 上开 HTTP 服务，给 Stream Deck、OBS 这类程序调 | `net.listen` | `network.listen` |
| 朗读与语音命令 | `speech.say` / `speech.listen` | 听要 `speech.listen` |
| 串口、HID、低功耗蓝牙设备，自制设备的按键送进键位 | `hardware.*`、`input.source` | `hardware` |
| 手机画面的低频帧，给分析与 AI | `media.frames` | `media.read` |
| 给 AI 客户端的 MCP 工具 | `mcp.tool` | — |

### 原生

原生扩展在 `contributes.native` 里写 DLL 的路径和它提供的东西，扩展点是 `native`。

| 能力 | 清单里写 | 权限 |
|:--|:--|:--|
| 触控方式：把触点送到手机的另一种办法（例如接在手机上的盒子） | `touchMethods` | — |
| 系统节点：逻辑里 `ext.<扩展>.<节点>()` 调用、蓝图里是一个方块的函数 | `nodes` | 按节点声明 |
| 附加：挂在组件上，改它的输入和输出 | `attachments` | — |
| 行为：组件库“扩展”分组里的一种新组件，自己决定手指怎么按 | `behaviors` | — |
| 长任务：有进度、可以取消、最后给结果，例如刷固件 | `jobs` | 按任务声明 |
| 设备提供者：在设备页列出设备（例如触控盒） | `deviceProviders` | `devices.provide` |
| 语音引擎：给 `speech.*` 提供朗读与识别 | `speechEngines` | — |
| 输入设备：自制设备的按键与轴进入键位 | `inputSources` | `input.provide` |
| 连接方式：中转服务器、隧道、云手机这类连手机的新办法 | `transports` | `transport` |
| 画面与声音来源：像采集卡一样显示的画面 | `videoSources`、`audioSources` | `media.capture` |
| 媒体输出：把投屏窗口送到虚拟摄像头、推流等 | `mediaSinks` | `media.output` |
| 录像导出：把录像转成别的格式 | `recordingExporters` | `media.output` |
| 取帧：读全速的投屏画面并在上面画标记 | `frameTaps` | `media.read` |
| 命令行子命令：`gs ext <扩展> <命令>` 运行一个长任务 | `contributes.cli` | — |

DLL 还能读写自己的存储、发通知、给自己的逻辑发事件、读扩展的设置、占用硬件、听程序事件、调别的扩展的服务，见[原生扩展指南](native.md)。

## 扩展点与版本

清单的 `requires` 写出扩展用到的每个扩展点和可以接受的版本范围，例如 `"requires": { "script": "^1.0", "panel": "^1.0" }`。GameScrcpy 2.1.0 提供的扩展点都是 1.0：

| 扩展点 | 用于 |
|:--|:--|
| `theme` | `contributes.themes` |
| `filter` | `contributes.filters` |
| `profile` | `contributes.profiles`、`contributes.profilePatches` |
| `component` | `contributes.components` |
| `task` | `contributes.tasks` |
| `script` | `contributes.scripts`，以及用到脚本的面板和服务 |
| `panel` | `contributes.panels`（还要 `script`） |
| `native` | `contributes.native` |
| `help` | `contributes.help` |
| `tour` | `contributes.tours` |

清单格式里还留着 `command` 与 `preset` 两个扩展点，2.1.0 还不提供，写了的扩展装得上但不会被打开。贡献了东西却没在 `requires` 里声明扩展点，`gs validate` 会报 `contribution-undeclared`；声明了程序不提供的扩展点或版本，扩展页会写明原因、不打开它。以后的版本只加扩展点、只在原有扩展点上加东西，已经发布的字段与函数不改含义。

## 权限

权限默认一项都没有。扩展在清单的 `permissions` 里逐项申请，每项写一句理由；用户打开扩展时看到每一项和理由，同意了扩展才会开始运行。没申请的能力在运行时直接被拒绝（逻辑里是 `GS-API-072` 一类的错误，DLL 里是 `GS_EXT_DENIED`）。只有带逻辑或原生代码的扩展（声明了 `script`、`component`、`task` 或 `native`）才能申请权限。

| 权限 | 安装框里的说法 | 用于 |
|:--|:--|:--|
| `device.input` | 发送触控与按键 | 逻辑里的 `touch.*`、按键与文字输入 |
| `device.screen` | 读取手机画面 | `vision.*` 找图、找色、读字 |
| `device.clipboard` | 读写手机剪贴板 | `clipboard.*` |
| `device.apps` | 查看与打开应用 | 前台应用、打开应用 |
| `devices.other` | 操作其他手机 | 逻辑操作别的手机；可以加 `maxDevices`（1～64） |
| `mapping` | 触发控件、切换方案 | `controls`、`mapping.*`、`profile.*`、`globals` |
| `notify` | 发送系统通知 | `ui.notify`，DLL 的 `notify` |
| `network` | 联网 | 必须在 `hosts` 里列出域名（可以写 `*.example.com`，可以带端口）；原生 DLL 导入了联网函数时，安装检查也看它 |
| `files.pick` | 打开你选择的文件 | 经系统的文件选择框选中的文件 |
| `files.paths` | 访问固定的文件夹 | 必须在 `paths` 里列出文件夹 |
| `input.read` | 读取你的键盘和鼠标 | `input.cursor`、`input.isDown`、按键事件 |
| `hardware` | 访问你选中的设备 | `hardware.*`、`input.source` |
| `ai` | 使用你设置好的 AI 服务（会花你的额度） | `ai.ask` |
| `devices.provide` | 在设备页添加设备 | 原生设备提供者 |
| `network.listen` | 接受本机程序的连接 | `net.listen`；用户还可以在扩展详情里单独关掉 |
| `speech.listen` | 听你说的语音命令 | `speech.listen`（朗读不要权限） |
| `events.app` | 知道所有手机与窗口的动静 | 应用脚本、任务和 DLL 收到每台手机的事件 |
| `input.provide` | 把自制设备的按键送进键位 | 原生输入设备 |
| `transport` | 提供连接手机的方式 | 原生连接方式；要联网的另加 `network` |
| `media.capture` | 提供画面或声音 | 原生画面与声音来源 |
| `media.read` | 读取投屏的画面与声音 | `media.frames`、原生取帧 |
| `media.output` | 把画面或声音送到别处 | 原生媒体输出与录像导出 |

`adb.shell` 永远不会授予扩展，写了就是清单错误。原生 DLL 在安装时还会按它导入的系统函数检查：用到联网、读写文件、读输入、操作别的进程的函数却没申请对应权限时，安装框里会写出来；往别的进程里注入代码的 DLL 不能安装。

## 代码在哪里运行

- **数据**：主题、滤镜、方案、帮助、引导与设置没有代码。滤镜着色器在显卡上执行，`gs validate` 会先编译检查它。
- **逻辑**：扩展的脚本在低权限的 `gs-scripthost.exe` 子进程里运行，只能经 GameScrcpy 代办来做事，每件事都按清单申请并经同意的权限检查。会话脚本（`"run": "session"`）在每台投屏的手机上各运行一份，应用脚本（`"run": "app"`）在 GameScrcpy 运行期间只有一份。扩展的界面全部由 GameScrcpy 用自己的控件来画，扩展只给一棵描述界面的树，不运行任何界面代码。
- **原生**：DLL 以你的 Windows 账户的全部权限运行，所以安装前有更严的检查。用户装的扩展，每个 DLL 在自己的 `gs-exthost.exe` 进程里运行：它崩溃不会连累 GameScrcpy，没有及时回应会被结束。GameScrcpy 随程序发布的官方扩展才载入到 GameScrcpy 自己的进程里。两种情况下 DLL 看到的接口完全一样。

## 安装、同意与更新

1. **安装**：在扩展页点“从文件安装…”选 `.gsext` 或 `.gsbundle`，或者打开网页上的 `gamescrcpy://install` 链接（见[打包与发布](publishing.md)）。装之前 GameScrcpy 先检查包：清单、文件路径、文件个数与大小；带原生代码的还要过杀毒扫描（Windows 自带的反恶意软件接口）、签名与导入函数的检查。检查不过的不装，安装框里写明原因。
2. **同意**：安装框和扩展卡片列出扩展申请的每项权限、理由和它依赖的扩展。用户打开扩展时同意这些要求，扩展才会载入。
3. **更新**：装同一个 `id` 的新版本会替换旧版本，设置与已同意的权限按 `id` 保留。新版本多申请了权限（或者在 `network` 里多了域名、在 `files.paths` 里多了文件夹），或者多了必需的依赖，扩展会停在“等待同意”，卡片上写“重新打开，同意新的要求”，用户重新打开它并同意后才载入。少申请、改理由不需要重新同意。
4. **签名**：包可以带作者签名（`SHA512SUMS` 与 `SHA512SUMS.minisig`，清单里写作者公钥 `authorKey`）。签了名的扩展，卡片上写“已校验的发布者”，之后的更新必须用同一把钥匙签名：没签名或换了钥匙的更新装不上，要换钥匙只能先删掉旧的再装。没签名的原生扩展，安装框里有更醒目的提醒。
5. **自动检查更新**：2.1.0 不检查更新，也不自动安装；新版本由用户自己下载或经链接安装。

## 出了问题

- 扩展页的卡片会写出清单问题、缺少的扩展点或依赖、等待同意等状态，“详情”里有完整列表。
- 独立进程一分钟内停了三次、附加或行为 10 秒内失败三次，扩展会被自动关掉；卡片上写原因，“详情”里的“健康状况”列出最近的事和崩溃报告，用户重新打开扩展时清掉。
- 扩展的日志写在程序日志里（`%APPDATA%\GameScrcpy\logs`，便携版在 `data\logs`）：DLL 的是 `[extensions] <扩展 id>: …`，逻辑的是 `logic <名字> on <手机>: …`。设置 › 关于 › “导出诊断包…”会带上装了哪些扩展、它们的健康记录和最新的崩溃报告。
- 扩展页的“查找出问题的扩展”会一半一半地关掉扩展，帮用户找出是哪一个；`GameScrcpy.exe --safe-mode` 启动时不载入任何扩展；上一次运行在载入了扩展时意外结束的，下次启动也先不载入扩展。

## 示例

[examples/extensions](../../examples/extensions) 里的示例（带 DLL 的要先编译）：

| 示例 | 演示 |
|:--|:--|
| [crt-scanlines](../../examples/extensions/crt-scanlines) | 画面滤镜与它的设置 |
| [slate-theme](../../examples/extensions/slate-theme) | 颜色主题 |
| [pc-clock](../../examples/extensions/pc-clock) | 最小的原生扩展：一个系统节点 |
| [deck-bridge](../../examples/extensions/deck-bridge) | `net.listen` 给 Stream Deck 这类程序调，`input.source` 把按键送进键位，`ext.provide` 给别的扩展用 |
| [voice-commands](../../examples/extensions/voice-commands) | `speech.listen` 听语音命令、`speech.say` 回答 |
| [event-log](../../examples/extensions/event-log) | `events.on` 记录 GameScrcpy 的程序事件 |
| [tour-and-help](../../examples/extensions/tour-and-help) | 扩展自带的功能引导与帮助文章 |
