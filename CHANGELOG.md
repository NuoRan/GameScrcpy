# 更新日志

本文件记录每个版本的主要变化，格式参照 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循[语义化版本](https://semver.org/lang/zh-CN/)。

## [2.2.1] - 未发布

2.2.1 修了一批问题：方案可以改名，扩展页卡片的间距恢复正常，录像的声音与画面对齐，触控盒与 AOA 的控制窗口键鼠和转屏更顺，界面上常驻的说明小字也精简了。手机上的服务端随之更新（2.2.1，声音采集），伴侣 App 没有变化。

### 修复

- **方案改名**：方案卡片的菜单里选“重命名”、双击方案名，或者在键位编辑器的菜单里改；开着的编辑器跟着显示新名字。
- **扩展页**：两张卡片并排时中间不再空出一大块；每行卡片只按这一行里最高的那张排；卡片署名先写“为 GameScrcpy … 编写”。
- **录像音画同步**：导出 MP4 时声音按一条连续的时钟排在画面上，中途没声音的地方补静音，声音晚开始时从开头补静音，完全重叠的声音包丢掉，中途重连后的第二段声音不再把声音截断；手机端先把每 20 毫秒的声音读出来再等编码器，编码器忙时不再丢声音。
- **控制窗口**（没有画面，扩展提供的触控盒、AOA）：视角放开时按住 WASD 再动鼠标，按键不再失灵（鼠标移动按手机的触控频率合并后再发）；伴侣 App 报告手机转屏时窗口跟着转，竖屏转横屏不再卡住一块；视角锁定时手机光标停在角落，解锁后回到电脑鼠标所在的位置，回来之前的点击先排队。
- **扩展面板**：在输入框里打完字直接点按钮，按钮收到的就是刚打的内容（以前要先按回车或点一下别处，否则 ESP32 触控盒改名会报“名称要有 1～32 个字节”，保存 Wi‑Fi 时密码是空的）；什么也不显示的部分不再占位置。
- **打开设置时闪退**：偶尔一打开设置就闪退——Qt 6.10 的增量垃圾回收在页面创建到一半时回收了新对象的属性存储。现在每次垃圾回收一口气做完。
- **扩展被误关**：Windows 重启或注销时会结束扩展的进程，以前被算成“一分钟内停了三次”、把扩展关掉（例如手感样例）；现在不算崩溃、不重启，也不关掉。之前因此被关掉的扩展，到扩展页重新打开即可。

### 界面

- 删去一大批常驻的说明小字（安装对话框、设置、工作台、AI 小助手、设备页、蓝图、投屏窗口、键位编辑器）：细节放到旁边的 ⓘ 里，或者只在用得上时出现。

### English summary

- **Fixes**: profiles can be renamed from the card's menu, by double-clicking the name and in the key mapping editor. Two extension cards side by side no longer stand far apart, and each row is as tall as its own tallest card. Recordings keep sound and picture together: the MP4 sound track runs on one continuous clock, silence fills gaps and a late start, a second audio session no longer cuts the sound off, and the phone no longer drops sound while the encoder is busy. In the Control window of a touch box or AOA the keys keep working while the mouse moves with the camera released (mouse motion is merged at the phone's touch rate), the window turns with the rotation the companion reports, and the phone's pointer parks while the camera is locked and comes back under the computer's pointer, with clicks held until it is there. A click in an extension panel carries the text just typed into a field. Opening Settings no longer crashes now and then (Qt 6.10's incremental garbage collector freed the property storage of objects still being created; each collection now runs to the end). Extensions are no longer turned off when Windows ends their process at a restart or sign-out; one turned off that way can be switched on again on the Extensions page.
- **Interface**: many standing explanations are gone; the details sit behind info buttons or appear only when they apply.

The server on the phone is updated (2.2.1, sound capture); the companion app is unchanged.

## [2.2.0] - 2026-11-15

2.2 让投屏更快、更稳：手机端在 120 帧时改用块模式给编码器送画面，整条链路的线程优先级、WiFi 直连与电脑端显示都重新调过；手机插网线时也能一键改走网络；可以自己设触控发送频率；AI 小助手按模型给出强度与上下文选项。手机上的服务端随之更新（2.2.0），伴侣 App 没有变化。

### 投屏延迟与流畅

- **手机端**：120 帧时改用 MediaCodec 的块模式把画面交给编码器（帧率上限或屏幕最快档高于 100 帧时自动使用），不再经安卓输入面每 10 毫秒成对送帧。Redmi K80 在和平精英里实测，采集到编码输出约 18 毫秒（原来约 22 毫秒），画面到达电脑也更均匀，游戏自己的帧率不受影响。编码、发送、接收与触控线程用视频优先级，声音用音频优先级，游戏忙时不会被挤到后面。所有编码器都要求低延迟，关键帧只在需要时才出（录像每 10 秒要一个）。
- **触控**：触控时间提前 5 毫秒交给手机，配合安卓的触控重采样，手指位置更新得更早。
- **WiFi 直连**：只唤醒有活要干的线程，触控在排队的那个线程上当场发出；大包先发完所有数据片、各组交错发送，校验数据用乘法表算；两端都把视频标成 CS5，电脑在投屏时让无线网卡保持媒体流模式并开启 HighQoS；丢包后按往返时间重新要关键帧（至少 100 毫秒，翻倍到 500 毫秒为止）。
- **电脑端**：画面在渲染线程上取出就画，不经过界面线程；刷新率低于 100 Hz 的屏幕只排一帧；控制链路卡住时只合并手指移动，不再晚到地逐条重放；看不见的界面停掉计时器与动画；更多界面代码编译成 C++；启动时能晚做的事等主窗口第一帧之后再做。
- **F4 诊断**：画面一行写明每秒收到、绘制、绘制前就被替换、被 Windows 丢掉的帧；新增每段输入延迟花在哪里、显卡画一帧要多久、等关键帧的停顿次数。

### 连接

- **通过网络连接**（实验性）：手机插着网线、没有 Wi‑Fi 时用不了无线调试。现在先用数据线连上，在手机卡片的“更多”里选“通过网络连接”，再拔掉数据线、让手机连上同一个网络（网线或 Wi‑Fi），GameScrcpy 会在这台电脑所在的网络里找到这台手机并连上，之后留在“最近连接”里。手机重启后要再用数据线点一次。Android 11 以前的手机也能这样用。
- **“自动”连接方式**改为按手机怎么连着来选：无线调试或网络调试连着的走 WiFi 直连，数据线连着的走 adb 隧道。WiFi 直连不通时直接报错，不再悄悄改走隧道。

### 输入

- 新设置“触控发送频率”（设置 › 输入与键位 › 触控，每台手机可以单独设）：默认自动，为手机屏幕刷新率的两倍（120 Hz 时每秒 240 次）；可以手动设为每秒 60～1000 次，改了立即生效。调高能让每次移动早一点到手机，但手机要多花些算力。

### AI 小助手

- 强度和上下文按模型给出：Claude 4.6 及以后（含 Claude 5 系列）的强度可以选到“超高”“最高”（按型号），用新的自适应思考与 `output_config.effort`（Claude 4.7 起服务端不再接受思考预算）；本来就有百万上下文的模型显示“长上下文：100 万 token，一直开着”。GPT 的强度按型号给出（5.2 起有“超高”，5.6 起有“最高”，GPT-5.1 起不再给“最低”）。DeepSeek V4（deepseek-flash、deepseek-v4-pro）有深度思考开关和低、高、最高三档强度，思考时调用工具不再报 400。经 OpenAI 兼容服务使用 Claude 或 Gemini 时可以选低、中、高。

### 扩展

- 扩展点 `native` 升到 2.1：AI 服务的模型能力多了 `xhigh`、`max` 两档强度与 `topEffortsThink`、`millionContext` 两项。按 2.0 写的扩展照常能用。

### English summary

- **Faster, steadier mirroring**: above 100 fps the phone hands frames to the encoder through MediaCodec's block model (capture to encoder output about 18 ms instead of 22 ms on a Redmi K80 in a game, with steadier arrival); the encoder, sending, receiving and touch threads run at video priority; every encoder is asked for low latency and key frames come on request; touches lead the clock by 5 ms. WiFi direct wakes only the thread with work, sends data shards first and interleaved, marks video CS5 and keeps the PC's WLAN in media streaming mode. The PC takes and draws frames on the render thread, queues one frame on screens below 100 Hz, merges touch moves when the control link stalls and stops hidden interface timers. F4 shows frames dropped by Windows, where each input millisecond goes, the GPU draw time and pauses for a key frame.
- **Connections**: Connect over the network (experimental) turns on network debugging for a phone on USB and finds it again on the network after it is unplugged, for phones on a network cable without Wi-Fi. Automatic now picks WiFi direct for phones on wireless or network debugging and the adb tunnel for USB, and reports an error instead of falling back.
- **Input**: a per-phone Touch send rate setting, automatic (twice the refresh rate) by default, 60 to 1000 a second.
- **AI assistant**: effort and context options follow the model: Claude 4.6 and later up to extra high and max with adaptive thinking and `output_config.effort`; GPT levels by version; DeepSeek V4 with a thinking switch and low, high and max, and tool calls work while it thinks; Claude and Gemini through OpenAI-compatible services offer low, medium and high.
- **Extensions**: the `native` extension point is 2.1; AI providers' model traits gained `xhigh`, `max`, `topEffortsThink` and `millionContext`. Extensions written for 2.0 keep working.

## [2.1.1] - 2026-11-13

2.1.1 只修了一处：没有画面的“控制”窗口里手机光标会和鼠标错开。手机上的服务端和伴侣 App 没有变化。

### 修复

- 没有画面的“控制”窗口（扩展提供的触控盒、AOA）里，鼠标移出窗口再从别处移回来，手机上的光标留在原地，两边的位置就对不上了；现在移回来时手机光标跟到鼠标所在的位置。鼠标在画面外的黑边里移动、切到别的窗口再回来、视角锁定结束后，也都会跟上。

### English summary

Fixed: in the Control window without a picture (touch boxes from extensions, AOA), the phone's pointer no longer stays behind when the mouse leaves the window and comes back somewhere else; it also catches up after the mouse moved through the letterbox, after another window was in front and after the camera lock.

## [2.1.0] - 2026-11-13

2.1 把扩展平台做到第三版：扩展可以带来设备、连接手机的方式、画面与声音、朗读与语音命令、界面语言、AI 模型服务和格式转换，安装与更新也更安全。ESP32 触控盒搬出主程序，成为社区扩展；手机上的伴侣 App 重做。

### 扩展平台第三版

- **设备与连接**：扩展可以在设备页加设备（例如触控盒），有自己的卡片、操作、带进度的长任务和面板；可以提供连接手机的新方式；可以提供画面与声音的来源，像采集卡一样给手机当画面。
- **画面与声音**：投屏窗口“更多”里的“发送到 …”把画面和声音送给扩展（例如虚拟摄像头）；扩展可以把录像导出成别的格式；扩展的原生代码可以逐帧读取投屏画面并在上面标注，每台手机分别开关；逻辑可以用 `media.frames` 取得画面的帧。
- **朗读与语音命令**：逻辑可以用 `speech.say` 朗读、用 `speech.listen` 听语音命令，默认用 Windows 自带的语音，扩展可以提供别的引擎。“设置 › 音频”多了“朗读与语音命令”。
- **界面语言**：语言包扩展添加界面语言，还可以带上帮助的译文。
- **AI 模型服务**：扩展可以提供模型服务，在“设置 › AI”里选用，小助手与 `ai.ask` 都用它。
- **识别引擎**：扩展可以给逻辑提供识别文字、物体或画面内容的引擎。
- **导入与导出**：转换器扩展把别的格式导入成方案，把方案、任务和录像导出成别的格式。
- **逻辑能做的更多**：经 GameScrcpy 发 https 请求、用 WebSocket（扩展的逻辑只能访问清单里写明的网站）；打开或保存用户选的文件、使用用户选的文件夹；在 127.0.0.1 上接受本机其他程序的连接（扩展详情里可以关掉）。
- **程序事件**：逻辑和原生扩展可以用 `events.on` 收听 GameScrcpy 自己的事件：手机连上与断开、投屏开始与结束、套用方案、前台应用变化、屏幕旋转、录像开始与结束、窗口焦点、扩展打开与关闭、设置改动。扩展的逻辑要收听别的手机或整个程序的事件，需要新权限“知道所有手机与窗口的动静”（`events.app`）。
- **更多位置**：扩展的面板可以放进设置、它自己的设置窗口、投屏窗口的工具栏、卡片的“更多”菜单和托盘菜单；扩展可以带帮助文章（帮助里的“扩展”一组）和功能引导；扩展原生代码里的输入设备（自制手柄、踏板等）可以在键位里使用；扩展之间可以互相调用、收听彼此的事件；硬件由扩展、逻辑和程序自己轮流使用，不再互相抢串口。
- **安全地安装**：扩展包可以签名，安装框和扩展卡片写明签名状态：第一次见到的公钥只说明用哪个公钥签了名，以前装过、记住了的公钥才显示“已验证的发布者”；签名不符、换了公钥或从有签名变成没签名的包都不予安装。网页上的 `gamescrcpy://` 链接可以下载扩展包，核对 SHA-256 后打开安装框，便携版在“设置 › 通用 › 扩展链接”里注册；受限的原生扩展在没有任何权限的独立进程（AppContainer）里运行；一个扩展可以为 x64 和 arm64 各带一个 DLL。
- **健康与诊断**：扩展卡片写明最近 7 天停止的次数和被自动关闭的原因，详情里有健康记录；扩展列表与 gs-exthost 的崩溃报告进诊断包。
- **更新检查**：扩展可以写明更新列表。在“设置 › 通用 › 更新”打开“检查扩展更新”后，每天启动时最多查一次（默认关闭，不会发送你装了哪些扩展），扩展页也可以随时“检查更新”。只有你点“更新”才会下载，大小、SHA-256 和签名都要对得上，再打开安装框，列出新版本多了或少了哪些权限与依赖；新版本要的权限比以前多时，更新后先保持关闭，等你重新打开。
- **原生接口第 2 版**：DLL 导出 `gs_ext_main`，可以提供带进度、日志和结果的长任务（逻辑里写 `ext.<扩展>.<任务>({ ... })`，蓝图里是一个带“进度”“完成”“失败”出口的块），给自己扩展的逻辑发事件（`events.on`），读取自己扩展的设置并在改动时得到通知，经 GameScrcpy 使用用户允许的设备。结构体都带着自己的大小，用旧一些或新一些的头文件编译的 DLL 都能加载。
- **给扩展作者**：`gs ext` 命令不开 GameScrcpy、不连手机就能测试扩展、运行它的命令、新建扩展；`gs ext keygen` 与 `gs pack --sign` 给扩展包签名；新的扩展作者指南和几个示例，示例在安装目录的 `sdk\samples` 里。

### ESP32 触控盒改为社区扩展

- ESP32 触控盒搬出主程序，成为社区扩展 `community.esp32`，在单独的项目“[GameScrcpy-ESP32](https://github.com/NuoRan/GameScrcpy-ESP32)”里发布，固件、刷写和协议都在那里。电脑和板子之间新增低功耗蓝牙连接（固件 1.1，实验性，还没有在真板子上核对过）。
- GameScrcpy 本身不再驱动任何板子，也不再附带 ESP32 的固件；触控盒都来自扩展。装上扩展后，板子以触控盒的身份出现在设备页，也可以和采集卡配对。
- 帮助里的 ESP32 文章换成通用的“触控盒”。

### 伴侣 App 2.1

- 重新设计：首页、功能、电脑、权限、关于五页，深浅色跟随手机，连上电脑后用电脑的强调色；通知里写着连着哪台电脑、连了多久。
- 手机上的光标、键位提示、电脑键盘输入和自动连接四个开关，在手机和电脑上都能改，两边同步；没连电脑时改的，连上后再发过去。键位提示的不透明度两边都能调。
- 光标可以选大小与样式，打字时的提示条可以放在底部或顶部。
- “电脑”页列出连过的电脑，可以马上重连，也可以删除。
- 旧版伴侣照旧能用，只是不同步开关。

### 扩展与界面

- 不再内置任何扩展：CRT 扫描线与石板主题改为示例，和 PC 时钟示例一起放在安装目录的 `sdk\samples` 里，需要时在扩展页“从文件安装”。
- 扩展与 GameScrcpy 的版本有了明文规范：扩展点按“主.次”定版本，程序提供的版本表只有一份。GameScrcpy 更新后第一次启动时，如果开着、却因为版本原因打不开的扩展，会弹一次提示列出它们，可以“检查更新”或“打开扩展页”；安装框对与这个 GameScrcpy 不兼容的包写明原因、不安装；扩展卡片写“为旧版 GameScrcpy 制作”或“需要更新版的 GameScrcpy”。
- 扩展的清单可以用 `gamescrcpy` 写明作者照着写的 GameScrcpy 版本（如 `"gamescrcpy": "2.1"`），卡片、详情、安装框与升级后的提示据此写“为 GameScrcpy 2.1 编写”；没写的按 `requires` 推算，兼容与否仍由 `requires` 决定，写着旧版本但兼容的扩展照常用、不提示。`gs ext init` 与“导出为扩展”写当前版本的主.次，`gs validate` 与 `gs pack` 对没写的只给 `gamescrcpy-missing` 警告。
- 帮助菜单与帮助页加了“赞助…”，弹出赞助对话框，可以去爱发电赞助。
- 帮助新增三篇：“GameScrcpy 伴侣”“扩展”“扩展带来的功能”。
- 手感样例移出仓库。

### AI

- 设置里的“模型”一栏改成下拉框加“获取”按钮；选好模型后，下面只列出这个模型支持的深度思考、思考强度和长上下文（100 万 token）选项。每个模型服务各自记住这些选项。
- 小助手输入框左下角可以给当前这段对话换模型、开关深度思考、调强度和长上下文，跟着对话一起保存；“设为默认”写回设置，新对话用设置里的默认值。
- 所有对话都保留在电脑上。“最近对话”菜单列出最新 20 段，“查看全部…”在单独的窗口里列出全部。
- 气泡和小助手卡片打开、收起时有动画；开启“减少动态效果”时只淡入淡出。

### 修复

- 没有打开投屏窗口时，从方案页“编辑逻辑…”打开较大的方案会卡死并占满内存（Qt 6.10.1 的问题，已绕开）。
- ESP32 触控盒闲置几分钟后会话断开、第一下触控失败（随社区扩展修复）：盒子不出声时会重新询问，必要时切回握手波特率再升回高速。
- 扩展的新版本要的权限比以前多、等你重新打开时，扩展卡片误写“需要的扩展没有安装”；现在写“重新打开，同意新的要求”。
- 扩展包里名为 `con .txt`（设备名后面跟空格）、`CONIN$`、`CONOUT$` 这类 Windows 设备名的文件现在也会被拒绝；从手机复制到电脑的这类文件，名字前面会加上 `_`。

### 不兼容的变化

- 原生扩展接口升到第 2 版：按第 1 版编译、只导出 `gs_ext_entry` 的 DLL 不再加载，要用新的 `sdk/include/gamescrcpy_ext.h` 重新编译，导出 `gs_ext_main`。
- 扩展点 `native` 升到 2.0：原生扩展的清单写 `"native": "^2.0"`；为 2.0 写的原生扩展在 2.1 里不打开，显示“为旧版 GameScrcpy 制作”。
- 内置的 ESP32 触控盒没有了，2.0 的 ESP32 设置也不迁移：选着 ESP32 触控方式的手机改回普通的触控方式，采集卡上选的 ESP32 触控改为不触控，`esp32.*` 设置被丢弃。装上社区扩展 `community.esp32` 后，要重新给手机选触控方式，并在扩展里重新设置一次板子的 Wi-Fi。
- 不再内置任何扩展：CRT 扫描线与石板主题改为 `sdk\samples` 里的示例，id 也从 `gamescrcpy.*` 改成了 `example.*`。正在用石板主题的回到默认主题，装上示例后要在“设置 › 外观 › 配色主题”里重新选；CRT 扫描线装上示例、打开后照常可用，但原来调过的参数接不上。
- 扩展清单里 `files.paths` 权限的固定路径 `paths` 不再给扩展任何访问，只得到警告 `permission-paths-fixed`；改用 `folders` 写明每个文件夹的用途，逻辑用 `files.folder(name)` 打开，真实的文件夹由用户第一次使用时选定。
- 设置 `general.language` 改为存语言代码（文字），不再只有 `system`、`zh_CN`、`en` 三个选项，也可以是语言包添加的语言（如 `ja`）。选了语言包的语言后再用 2.0 打开，2.0 不认这个值，界面回到跟随 Windows。

### English summary

GameScrcpy 2.1 brings the third version of the extension platform. Extensions can add devices such as touch boxes to the Devices page, new ways to connect phones, picture and sound sources, places to send the picture and sound, recording exporters and picture readers; reading aloud and voice commands for logic, with Windows speech built in; interface languages with translated help; AI model services; recognition engines; and converters that import and export other formats. Logic can make https requests, use WebSockets, open the files and folders the user picks, answer programs on this computer, take frames of the mirrored picture and hear GameScrcpy's own events with `events.on`. Native extension interface 2 adds jobs with progress (one block in blueprints), events, settings and the devices the user allows. Packages can be signed: GameScrcpy remembers each extension's publisher key and calls it a verified publisher once the key is known from an earlier install; `gamescrcpy://` links download a package, check its SHA-256 and open the install dialog; restricted native DLLs run in an AppContainer without rights; extensions report their health, bring their own help and tours, and offer newer versions from their update lists, installed only when you choose Update. Each extension names the interface versions it was built against, and GameScrcpy says when one was made for an older GameScrcpy; each extension can also say which GameScrcpy version it was written for (`"gamescrcpy": "2.1"`), which its card, its details and the install dialog show, worked out from `requires` when the field is missing, and a compatible one written for an older version keeps working; the first start after an update lists the extensions you had turned on that no longer open, with Check for updates and Open Extensions, and the install dialog refuses packages that do not fit this GameScrcpy and says why. The ESP32 touch box moved out of GameScrcpy into the community extension `community.esp32`, published in its own project [GameScrcpy-ESP32](https://github.com/NuoRan/GameScrcpy-ESP32), which adds Bluetooth LE between the computer and the board (experimental, not yet checked on a real board). The companion app 2.1 is redesigned, and its switches sync both ways between the phone and the computer. No extension is built in any more: the CRT scanlines and the slate theme are samples in `sdk\samples`, next to the PC clock and new examples for extension authors. AI settings pick the model from a drop-down with per-model thinking options, every conversation is kept, and Help offers a Sponsor dialog. Fixed: the logic editor no longer freezes on larger profiles; an ESP32 box no longer drops its session after a few idle minutes (in the community extension); an extension waiting for consent no longer claims that it needs a missing extension; Windows device names such as `con .txt` are refused in packages and get a leading `_` when copied from the phone. Breaking: native extensions must be rebuilt for interface version 2 (`gs_ext_main`) and write `"native": "^2.0"`; those made for 2.0 show as made for an older GameScrcpy; the built-in ESP32 touch box is gone and its 2.0 settings are not carried over, so after installing `community.esp32` you choose the touch method again and set up the board's Wi-Fi again; the samples use `example.*` ids, so the slate theme falls back to the default theme until you install and choose it again, and the CRT scanlines start from their default values; the fixed `paths` of `files.paths` give nothing and `folders` replaces them; `general.language` holds a language tag as text, and 2.0 falls back to following Windows when it finds a language pack's tag there.

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

[2.2.0]: https://github.com/NuoRan/GameScrcpy/releases/tag/v2.2.0
[2.1.1]: https://github.com/NuoRan/GameScrcpy/releases/tag/v2.1.1
[2.1.0]: https://github.com/NuoRan/GameScrcpy/releases/tag/v2.1.0
[2.0.0]: https://github.com/NuoRan/GameScrcpy/releases/tag/v2.0.0
