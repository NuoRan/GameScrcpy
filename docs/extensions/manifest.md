# 清单参考

简体中文 · [English](manifest.en.md)

`manifest.json` 放在扩展文件夹的最上层，是 UTF-8 的 JSON 对象，最大 256 KiB、最多嵌套 32 层。GameScrcpy 和 `gs validate` 用同一套规则检查它：错误让扩展装不上，警告（比如不认识的字段 `unknown-field`）只提醒。每条问题都有一个代码、出错的位置（如 `$.contributes.panels[0].where`）和一句改法，`gs validate --json` 会全部列出。

## 一个例子

```json
{
  "schemaVersion": 1,
  "id": "alice.lap-timer",
  "version": "1.2.0",
  "name": "Lap timer",
  "description": "Times laps with a key and shows them in a page of the main window.",
  "authors": ["Alice"],
  "license": "MIT",
  "repository": "https://github.com/alice/lap-timer",
  "requires": { "script": "^1.0", "panel": "^1.0", "help": "^1.0" },
  "dependencies": { "alice.common": "^2.0" },
  "contributes": {
    "scripts": [
      { "name": "main", "entry": "src/main.ts", "run": "app" },
      { "name": "phone", "entry": "src/phone.ts", "run": "session" }
    ],
    "panels": [
      { "name": "laps", "title": "Laps", "icon": "timer", "where": "page", "script": "main" }
    ],
    "services": [
      {
        "name": "best",
        "description": "The best lap so far, in milliseconds, or null.",
        "output": { "type": ["integer", "null"] },
        "script": "main"
      }
    ],
    "help": [
      { "id": "laps", "title": "Timing laps", "file": "help/laps.md", "locales": { "zh_CN": "help/laps.zh_CN.md" } }
    ]
  },
  "settings": [
    { "key": "sound", "type": "boolean", "title": "Beep on each lap", "default": true }
  ],
  "permissions": [
    { "name": "mapping", "reason": "Listens to the lap key of the profile in use." }
  ],
  "l10n": {
    "zh_CN": {
      "name": "圈速计时",
      "panels.laps.title": "圈速",
      "settings.sound.title": "每圈响一声"
    }
  }
}
```

## 顶层字段

| 字段 | 必填 | 规则 |
|:--|:--|:--|
| `schemaVersion` | 是 | 固定为 `1` |
| `id` | 是 | 两段以上、用点连起来，每段是小写字母、数字和连字符（不以连字符开头或结尾，不连写两个），最长 64 个字符，例如 `alice.lap-timer`。`gamescrcpy.` 开头的留给官方扩展 |
| `version` | 是 | 语义化版本 `主.次.修订`，可以带预发布标签，如 `1.2.0-beta.1` |
| `name` | 是 | 最长 100 个字符 |
| `description` | | 最长 1000 个字符 |
| `authors` | | 最多 32 个名字，每个最长 100 个字符 |
| `license` | 是 | 许可证，建议写 SPDX 标识，如 `MIT`、`Apache-2.0`；最长 100 个字符 |
| `repository` | | `https://` 开头的地址 |
| `authorKey` | | 作者的 minisign 公钥（`RW` 开头的 56 个字符），用来校验包里的签名，见[打包与发布](publishing.md) |
| `aiAssisted` | | `true` 表示扩展是借助 AI 做的 |
| `requires` | 是 | 用到的扩展点和版本范围，至少一项，见下 |
| `dependencies` | | 依赖的扩展，见下 |
| `contributes` | | 扩展提供的东西，见下 |
| `settings` | | 最多 128 项设置 |
| `permissions` | | 最多 32 项权限 |
| `l10n` | | 各种文字的译文 |

### 名字的写法

清单里不同的名字有不同的写法，下文用这些说法：

| 说法 | 写法 | 用在 |
|:--|:--|:--|
| 原生名 | 小写字母开头，后面是字母和数字，最长 64 个字符，如 `flashBoard` | 原生贡献的 `name`、节点与任务的参数名、服务名、附加与行为的参数 `key` |
| 面板名 | 1～32 个小写字母、数字或连字符，如 `lap-list` | 面板、帮助与引导的 `id`、脚本的 `name` |
| 方案名 | 1～64 个字母、数字、`_` 或 `-` | 方案、组件（最长 48）与任务的 `name` |
| 设置键 | 字母开头，后面是字母和数字，最长 64 个字符 | `settings[].key` |
| 包内路径 | 相对扩展文件夹、用 `/` 分隔，不能出文件夹、不能带 `\ : * ? " < > \|`，最长 260 个字符 | 各种 `file`、`entry`、`folder`、`dll` |

名字以点开头的文件和文件夹（如 `.build`、`.git`）不会被检查，也不会被打包。

### `requires` 与版本范围

`requires` 的键是扩展点（`theme`、`filter`、`profile`、`component`、`task`、`script`、`panel`、`native`、`help`、`tour`，以及 2.1.0 还不提供的 `command`、`preset`），值是版本范围。版本范围可以写成：

| 写法 | 意思 |
|:--|:--|
| `^1.0` | 1.0 及以上、2.0 以下（主版本为 0 时只放宽次版本） |
| `~1.2` | 1.2 及以上、1.3 以下 |
| `>=1.0.0 <2.0.0` | 空格分开的几个比较，全部满足 |
| `1.2.3` 或 `=1.2.3` | 恰好这个版本 |
| `*` | 任何版本 |

带预发布标签的版本只有在范围里写了同一个版本号的预发布时才算满足。GameScrcpy 2.1.0 的每个扩展点都是 1.0，所以写 `^1.0`。

### `dependencies`

```json
"dependencies": {
  "alice.common": "^2.0",
  "bob.deck-tools": { "version": ">=1.1.0", "optional": true }
}
```

键是扩展的 `id`，值是版本范围，或者 `{ "version": 范围, "optional": true }`。必需的依赖没装、没开或版本不对时，这个扩展不会打开，卡片上写明缺什么；可选依赖不在也照常打开。依赖决定载入顺序，也决定可以调用哪些扩展的服务、收哪些扩展的事件（见[逻辑扩展指南](logic.md)）。不能依赖自己。

## `contributes`

每一类贡献都要在 `requires` 里声明对应的扩展点，否则是 `contribution-undeclared` 错误。同一类里的 `name`（或 `id`）不能重复。

### `themes`（扩展点 `theme`，最多 32 个）

```json
"themes": [
  { "name": "Slate",
    "dark": { "bgBase": "#0E1116", "textPrimary": "#E6EAF0" },
    "light": { "bgBase": "#E6EAF1", "textPrimary": "#0F1520" } }
]
```

`name` 最长 64 个字符；`dark` 与 `light` 是“设计令牌 → 颜色”，令牌名是小写字母开头的字母和数字（如 `bgBase`、`textSecondary`），颜色写 `#RRGGBB` 或 `#RRGGBBAA`。没写的令牌用 GameScrcpy 自己的颜色。示例见 [slate-theme](../../examples/extensions/slate-theme)。

### `filters`（扩展点 `filter`，最多 32 个）

`{ "name": "Scanlines", "file": "shaders/scanlines.hlsl" }`：`file` 是一个 `.hlsl` 文件（最大 1 MiB），里面用 `//!` 开头的指令声明参数、处理步骤和贴图：

| 指令 | 意思 |
|:--|:--|
| `//! PARAM <名字> <float\|int\|bool> <默认值> [<最小> <最大> [<步长>]]` | 一个参数（最多 64 个）；清单里有同名的设置时，参数取设置的值 |
| `//! PASS <名字> [scale=<比例>] [format=rgba8\|rgba16f]` | 一个处理步骤（至少 1 个、最多 16 个），对应着色器里的 `float4 <名字>(float2 uv)`；`scale` 大于 0、最大 4 |
| `//! TEXTURE <名字> <文件.png>` | 一张贴图（最多 8 张） |

着色器里可以用 `Original`、`Source`、`Linear`、`Point`、`SourceSize`、`OriginalSize`、`OutputSize`、`Frame`；`Gs` 开头的名字留给 GameScrcpy。`gs validate` 会编译每个处理步骤。示例见 [crt-scanlines](../../examples/extensions/crt-scanlines)。

### `profiles` 与 `profilePatches`（扩展点 `profile`，各最多 256 个）

- `profiles`：`{ "name": "pubg-default", "file": "profiles/pubg.json" }`，`name` 是方案名，`file` 是方案页导出的方案文件（`gamescrcpy.profile` 格式，最大 1 MiB）。
- `profilePatches`：`{ "target": "alice.pubg/pubg-default", "file": "patches/left-hand.json" }`，`target` 是“扩展 id/方案名”，`file` 是补丁（`"format": "gamescrcpy.profile-patch", "version": 1`）：`meta` 与 `screen` 合并进目标方案，`controls` 与 `vars` 各自用 `remove`（id 列表）、`change`（带 `id` 的对象）、`add`（新的对象）修改，`gamepad` 整个替换。目标方案没装时补丁不生效。`gs validate` 检查补丁的格式；目标是本扩展自己的方案时，还检查补丁提到的控件在不在（不在只给警告）。

### `components` 与 `tasks`（扩展点 `component` / `task`，各最多 256 个）

- `components`：`{ "name": "burst", "folder": "components/burst" }`，文件夹里是 `component.ts`（一个继承 `Component` 的类，见 [examples/components](../../examples/components)）。
- `tasks`：`{ "name": "daily-check-in", "folder": "tasks/daily" }`，文件夹里是 `task.json`（`gamescrcpy.task` 格式：名字、时间表）、`task.ts`（继承 `Task` 的类）与它用到的图片，见 [examples/tasks](../../examples/tasks)。

组件与任务里的逻辑同样在低权限子进程里运行，按扩展的权限检查。

### `scripts`（扩展点 `script`，最多 8 个）

```json
"scripts": [ { "name": "main", "entry": "src/main.ts", "run": "app" } ]
```

| 字段 | 规则 |
|:--|:--|
| `name` | 必填，面板名的写法 |
| `entry` | 必填，`.ts`（不是 `.d.ts`）或 `.js` 文件，必须在包里 |
| `run` | 必填：`session` 在每台投屏的手机上各运行一份；`app` 在 GameScrcpy 运行期间只运行一份 |

### `panels`（扩展点 `panel`，还要 `script`；最多 8 个）

```json
"panels": [
  { "name": "laps", "title": "Laps", "icon": "timer", "where": "page", "script": "main" },
  { "name": "more", "title": "Lap timer", "where": "contextMenu", "on": ["device", "profile"], "script": "main" }
]
```

| 字段 | 规则 |
|:--|:--|
| `name` | 必填，面板名的写法；脚本里 `ui.panel(name)` 等用它 |
| `title` | 必填，最长 40 个字符 |
| `icon` | GameScrcpy 的图标名（如 `timer`、`keyboard`，不认识的显示成拼图），或 `svg:<包内路径>` 用自带的 SVG 图标 |
| `where` | 必填，位置，见下表 |
| `script` | 必填，画这个面板的脚本的 `name` |
| `on` | 只用于 `contextMenu`，而且必须写：`device`（设备卡片）、`profile`（方案卡片），或两个都写 |

| `where` | 位置 | 脚本 |
|:--|:--|:--|
| `mirror` | 投屏窗口的侧边面板 | 会话脚本 |
| `menu` | 投屏画面右键菜单里的一组项；面板名必须是 `menu`，最多一个 | 会话脚本 |
| `status` | 投屏窗口状态区里的小部件 | 会话脚本 |
| `toolbar` | 投屏窗口工具栏上的一个按钮，可以弹出同名面板 | 会话脚本 |
| `page` | 主窗口左栏里的一页 | 应用脚本 |
| `settings` | 设置 › 扩展里的一张卡片 | 应用脚本 |
| `extension` | 扩展卡片上“设置”按钮打开的窗口里的一组 | 应用脚本 |
| `device` | 设备卡片的设置窗口（给设备提供者的设备用） | 应用脚本 |
| `contextMenu` | 设备卡片、方案卡片“更多”菜单里的一组项 | 应用脚本 |
| `tray` | 托盘菜单里的一组项 | 应用脚本 |

面板配错了脚本（比如页面配了会话脚本）时扩展照样能装，卡片上会写出这个面板的问题。自带的 SVG 图标只读扩展文件夹里的文件，最大 32 KiB，根元素 `svg`、`viewBox` 为空或 `0 0 24 24`，只认 `path`、`circle`、`rect`、`line`、`polyline`、`polygon`（以及 `g`、`title`、`desc`），不能有 `transform`、DTD 与实体；GameScrcpy 把它转成路径，按主题的线宽和颜色画。每个扩展最多 64 个。

### `help`（扩展点 `help`，最多 16 篇）

```json
"help": [
  { "id": "laps", "title": "Timing laps", "file": "help/laps.md",
    "locales": { "zh_CN": "help/laps.zh_CN.md" }, "icon": "timer" }
]
```

`id` 是面板名的写法；`title` 必填，最长 60 个字符；`file` 是英文的 Markdown（`.md`，最大 1 MiB）；`locales` 是“语言 → 文件”，语言写成 `zh_CN`、`en` 这样，最多 8 种；`icon` 同面板。文章出现在帮助页的“扩展”一组，地址是 `help:ext/<扩展 id>/<id>`。写法见[逻辑扩展指南](logic.md#帮助文章)。

### `tours`（扩展点 `tour`，最多 4 个）

```json
"tours": [
  { "id": "intro", "title": "Lap timer in two minutes", "start": "enabled",
    "steps": [
      { "title": "Welcome", "text": "This tour shows where the lap timer lives." },
      { "target": "card", "title": "Its card", "text": "Turn it on or off here." },
      { "target": "page:laps", "title": "Your laps", "text": "Every lap lands on this page." }
    ] }
]
```

| 字段 | 规则 |
|:--|:--|
| `id` | 必填，面板名的写法 |
| `title` | 必填，最长 60 个字符 |
| `start` | `enabled`（默认，扩展第一次打开并载入后自动放一次）或 `manual`（只能手动放） |
| `steps` | 2～12 步；每步 `title` 必填（最长 60 个字符）、`text` 必填（最长 300 个字符） |
| `steps[].target` | 省掉：页面中央的卡片；`card`：扩展页上这个扩展的卡片；`device`：设备页上这个扩展提供的第一台设备（要有设备提供者）；`page:<面板名>`：左栏里这个扩展的页面（`where` 为 `page` 的面板）；`settings:<面板名>`：设置 › 扩展里这个面板的卡片（`where` 为 `settings` 的面板） |

目标不在（没有设备、面板还没画出来）时那一步跳过。看完或跳过都会记住，不再自动放；扩展详情里每个引导有一个重放按钮，帮助文章里可以用链接 `action:tour:ext/<扩展 id>/<id>` 重放。

### `services`（最多 32 个）

```json
"services": [
  { "name": "best", "title": "Best lap", "description": "The best lap so far, in milliseconds, or null.",
    "input": { "type": "object", "properties": { "track": { "type": "string", "maxLength": 64 } } },
    "output": { "type": ["integer", "null"] },
    "script": "main" }
]
```

| 字段 | 规则 |
|:--|:--|
| `name` | 必填，原生名的写法；别的扩展调用 `<扩展 id>/<name>` |
| `title` | 最长 40 个字符 |
| `description` | 必填，最长 500 个字符 |
| `input`、`output` | 参数和结果的 JSON Schema（最大 16 KiB、最多嵌套 16 层），只认 `type`、`properties`、`required`、`items`、`enum`、`minimum`、`maximum`、`maxLength`、`title`、`description`；省掉就不检查 |
| `script` | 由这个应用脚本回答（`ext.provide`）；脚本必须是 `"run": "app"` |
| `node` | 或者由这个原生节点回答：节点只有一个 `json` 参数、返回 `json` |

`script` 与 `node` 必须二选一。服务本身不需要扩展点，但用脚本回答要声明 `script`，用节点回答要声明 `native`。调用规则见[逻辑扩展指南](logic.md#扩展之间的服务)。

### `native`（扩展点 `native`）

```json
"native": {
  "dll": "bin/board.dll",
  "nodes": [ { "name": "version", "returns": "string", "description": "The firmware version." } ],
  "jobs": [ { "name": "flash", "title": "Flash firmware", "params": [ { "name": "port", "type": "string" } ],
              "returns": "json", "permissions": ["hardware"], "most": 1 } ],
  "deviceProviders": [ { "name": "boards", "title": "Boards", "kinds": ["touchBox"] } ]
}
```

`dll` 必填，是包里的一个 `.dll`。也可以按处理器写成 `{ "x64": "bin/x64/board.dll", "arm64": "bin/arm64/board.dll" }`（最多 8 项，名字只认 `x64` 与 `arm64`，别的名字被忽略）：GameScrcpy 载入与这台电脑相同处理器的那一份，没有就说明“这个扩展没有适合这台电脑的版本”；`gs validate` 检查每份的处理器和入口 `gs_ext_main`。下面各项的 `name` 都是原生名，`title` 最长 40 个字符、`description` 最长 500 个字符，必须和 DLL 在贡献表里登记的名字一致（见[原生扩展指南](native.md)）。

| 键 | 最多 | 每一项 |
|:--|:--|:--|
| `touchMethods` | 8 | `name`、`title`（必填）、`description`、`wantsDevice`（`true` 时设置里让用户为它选一个 USB、串口或蓝牙设备） |
| `nodes` | 64 | `name`、`params`（最多 16 个 `{ name, type }`）、`returns`、`description`、`permissions`（最多 10 项，必须也在顶层 `permissions` 里） |
| `attachments` | 32 | `name`、`title`（必填）、`description`、`appliesTo`（组件类型如 `tap`、`wasd`，或 `*`）、`params`（最多 24 个参数，见下） |
| `behaviors` | 32 | `name`、`title`（必填）、`description`、`slots`（最多 8 个 `{ name, kind, title }`，`kind` 为 `key`、`axis2`、`axis1`）、`fingers`（1～10）、`frames`（是否每帧调用）、`params`（最多 24 个） |
| `jobs` | 32 | `name`、`title`（必填）、`params`（最多 16 个）、`returns`、`description`、`permissions`（最多 10 项）、`most`（1～4，同时最多几个，默认 4）；不能和节点重名 |
| `deviceProviders` | 8 | `name`、`title`（必填）、`kinds`（必填，现在只有 `touchBox`）；要权限 `devices.provide` |
| `speechEngines` | 4 | `name`、`title`（必填）、`say`（朗读用的任务名）、`listen`（识别用的任务名）、`voices`（列出声音的节点名）、`dictation`（能否听任意的话）、`languages`（最多 32 个，如 `zh-CN`）；`say` 与 `listen` 至少有一个，任务和节点的参数要符合约定（见[原生扩展指南](native.md#语音引擎)） |
| `inputSources` | 8 | `name`（1～32 个小写字母和数字，字母开头）、`title`（必填）、`slots`（1～64 个 `{ name, kind, title }`，`name` 是 1～32 个字母、数字、`_` 或 `-`，`kind` 为 `button`、`axis`、`stick`）；要权限 `input.provide` |
| `transports` | 8 | `name`、`title`（必填）、`icon`；要权限 `transport` |
| `videoSources` | 8 | `name`、`title`（必填）、`kinds`（这路画面可以配给 `phone` 还是 `touchBox`）；要权限 `media.capture` |
| `audioSources` | 8 | `name`、`title`（必填）；要权限 `media.capture` |
| `mediaSinks` | 8 | `name`、`title`（必填）、`video`（`{ fps: 1～60, width, height: 16～4096 }`，最多 3840 × 2160 像素）、`audio`（`true` 要声音）；`video` 与 `audio` 至少一个；要权限 `media.output` |
| `recordingExporters` | 8 | `name`、`title`（必填）、`extension`（必填，导出文件的扩展名，小写字母和数字、不带点，不能是 `gsrec`）；要权限 `media.output` |
| `frameTaps` | 8 | `name`、`title`（必填）、`fps`（1～60，默认 30）、`size`（长边 64～3840 像素，默认 1280）；要权限 `media.read` |

节点与任务的参数和返回值用这些类型：`boolean`、`number`、`string`、`point`（画面上的点，比例）、`region`（画面上的区域，比例）、`color`（`#RRGGBB`）、`duration`（毫秒）、`json`。

附加与行为的 `params` 是给用户在键位编辑器里调的参数，每个是 `{ key, type, title, description, default, min, max, step, unit, log, options, shape }`：`key` 是原生名，`type` 是 `number`、`integer`、`toggle`、`choice`、`key`、`point`、`region`、`curve`、`color` 之一，`title` 必填（最长 60 个字符）；`min`、`max`、`step`、`unit`（最长 16 个字符）、`log`（对数刻度，要求 `min` 大于 0）只用于数字，`options`（最多 32 个原生名）只用于 `choice`，`shape`（`circle` 或 `rect`）只用于 `region`；`default` 要符合类型（点写 `[x, y]`，区域写 `[x, y, r]` 或 `[x, y, w, h]`，曲线写 2～16 个上升的 `[x, y]`，都在 0～1）。

### `cli`（最多 16 个）

```json
"cli": [
  { "name": "flash", "description": "Writes the firmware to a board.", "job": "flash",
    "params": [ { "name": "port", "description": "The serial port, such as COM9", "default": "COM3" } ] }
]
```

`gs ext <扩展> <name> --<参数> <值>` 在 `gs` 的模拟宿主里跑原生任务 `job`（见[测试扩展](testing.md#命令行子命令)）。`name` 是 1～32 个小写字母、数字或连字符；`description` 最长 200 个字符；`params` 只能是这个任务的参数，`default` 要符合参数的类型；任务的参数不能叫 `json`、`hosted` 或 `help`（这些是 `gs` 自己的选项）。不需要扩展点。

### `commands` 与 `presets`

格式里留着，2.1.0 不提供这两个扩展点。

## `settings`

```json
"settings": [
  { "key": "port", "type": "text", "title": "Serial port", "default": "COM3", "maxLength": 16, "scope": "device" },
  { "key": "speed", "type": "choice", "title": "Speed", "choices": ["9600", "115200"], "default": "115200" },
  { "key": "strength", "type": "number", "title": "Strength", "min": 0, "max": 1, "step": 0.05, "default": 0.35, "live": true }
]
```

| 字段 | 规则 |
|:--|:--|
| `key` | 必填，设置键的写法 |
| `type` | 必填：`choice`、`integer`、`number`、`boolean`、`text` |
| `title` | 必填，最长 100 个字符 |
| `description` | 最长 1000 个字符 |
| `keywords` | 设置页搜索用的词，最长 500 个字符 |
| `choices` | `choice` 必须写，最多 64 项 |
| `min`、`max`、`step` | 只用于 `integer` 与 `number`（`integer` 的要是整数，`step` 大于 0） |
| `maxLength` | 只用于 `text`，1～4096 |
| `default` | 必填，要符合类型与范围 |
| `scope` | `global`（默认）或 `device`：每台手机可以有自己的值 |
| `live` | `true` 表示改了立刻生效，像滤镜参数那样 |

设置出现在设置 › 扩展与扩展卡片的设置窗口里。滤镜参数读同名的设置；原生 DLL 用 `setting_get` 读、在 `settings_changed` 里得知改动；逻辑会收到 `settings.changed` 事件（只有改了哪些键）。

## `permissions`

```json
"permissions": [
  { "name": "network", "reason": "Downloads the newest firmware.", "hosts": ["api.github.com", "*.githubusercontent.com"] },
  { "name": "hardware", "reason": "Opens the board's serial port." }
]
```

| 字段 | 规则 |
|:--|:--|
| `name` | 必填，权限名，见[概览](README.md#权限) |
| `reason` | 必填，最长 120 个字符，安装框里显示在权限旁边 |
| `hosts` | `network` 必须写，最多 64 个域名；`*.` 开头表示子域名，可以带 `:端口`；只写 `*` 会得到警告 |
| `paths` | `files.paths` 必须写，最多 64 个文件夹 |
| `maxDevices` | 只用于 `devices.other`，1～64 |

只有声明了 `script`、`component`、`task` 或 `native` 的扩展能申请权限。节点和任务的 `permissions` 里写的每一项都必须也在这里。

## `l10n`

```json
"l10n": {
  "zh_CN": {
    "name": "圈速计时",
    "description": "按一个键记一圈，在主窗口的一页里列出来。",
    "panels.laps.title": "圈速",
    "help.laps.title": "记录圈速",
    "settings.sound.title": "每圈响一声"
  }
}
```

键是语言（`zh_CN` 先找 `zh_CN`，再找 `zh`），值是“文字键 → 译文”。没有译文的地方用清单里的原文。能翻译的文字：

| 文字键 | 对应 |
|:--|:--|
| `name`、`description` | 扩展的名字与说明 |
| `settings.<key>.title`、`.description`、`.keywords`、`.choices.<选项>` | 设置 |
| `panels.<name>.title` | 面板 |
| `help.<id>.title` | 帮助文章的标题 |
| `tours.<id>.title`、`tours.<id>.<第几步>.title`、`tours.<id>.<第几步>.text` | 引导（步数从 0 数） |
| `services.<name>.title`、`.description` | 服务 |
| `themes.<name>.name` | 主题 |
| `touchMethods.<name>.title`、`.description` | 触控方式 |
| `nodes.<name>.description` | 系统节点 |
| `attachments.<name>.title`、`.description`、`.params.<key>.title`、`.params.<key>.description`、`.params.<key>.options.<选项>` | 附加 |
| `behaviors.<name>.title`、`.description`、`.params.<key>…`、`.slots.<槽>.title` | 行为 |
| `inputSources.<name>.title`、`.slots.<槽>.title` | 输入设备 |
| `speechEngines.<name>.title` | 语音引擎 |

面板、菜单里由脚本给出的文字由脚本自己决定，不经过 `l10n`。

## 常见的检查结果

`gs validate` 的每条结果都有代码和改法，下面是写清单时最常遇到的：

| 代码 | 意思 |
|:--|:--|
| `missing-field`、`wrong-type`、`too-long`、`too-many`、`out-of-range`、`duplicate` | 缺字段、类型不对、太长、太多、不在允许的值里、名字重复 |
| `unknown-field` | 不认识的字段（警告） |
| `id-invalid`、`id-reserved` | `id` 写法不对，或者用了 `gamescrcpy.` 开头 |
| `version-invalid`、`range-invalid` | 版本或版本范围写法不对 |
| `path-invalid`、`file-missing` | 包内路径写法不对，或文件不在包里 |
| `extension-point-unknown`、`contribution-undeclared` | 不认识的扩展点，或者贡献了东西却没声明扩展点 |
| `permission-unknown`、`permission-reason-missing`、`permission-not-allowed`、`permission-never-granted` | 不认识的权限、没写理由、这种扩展不能申请权限、申请了 `adb.shell` |
| `permission-hosts-missing`、`permission-host-invalid`、`permission-any-host` | `network` 的 `hosts` 缺失、写法不对、只写了 `*`（警告） |
| `native-permission-undeclared` | 节点、任务或原生贡献要的权限没在 `permissions` 里 |
| `native-name-invalid`、`native-type-invalid`、`native-dll-invalid` | 原生名、类型或 DLL 路径写法不对 |
| `panel-name-invalid`、`script-name-invalid`、`script-entry-invalid` | 面板名、脚本名或脚本入口不对 |
| `tour-target-invalid` | 引导步骤的 `target` 写法不对，或指向不存在的面板、没有设备提供者 |
| `speech-engine-invalid` | 语音引擎引用的任务或节点不在、参数不合约定 |
| `service-target-invalid`、`service-schema-invalid` | 服务的 `script` / `node` 不对，或 JSON Schema 用了不支持的写法 |
| `filter-compile-failed`、`filter-directive-invalid` | 滤镜编译失败、指令写法不对 |
| `dependency-cycle` | 依赖了自己 |

GameScrcpy 内部按同一张字段表生成清单的 JSON Schema（draft 2020-12），目前不单独发布成文件；以 `gs validate` 的结果为准。
