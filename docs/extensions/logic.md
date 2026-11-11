# 逻辑扩展指南

简体中文 · [English](logic.en.md)

扩展的逻辑用 TypeScript（或 JavaScript）写，和方案逻辑用的是同一套脚本接口。它在低权限的 `gs-scripthost.exe` 子进程里运行，不能直接碰文件、网络和设备，一切都经 GameScrcpy 代办，并按清单申请、用户同意过的权限检查。这一页讲扩展特有的部分；每个函数的参数、返回值和错误见 [sdk/docs](../../sdk/docs/README.md)。

## 脚本

```json
"requires": { "script": "^1.0" },
"contributes": {
  "scripts": [
    { "name": "main", "entry": "src/main.ts", "run": "app" },
    { "name": "phone", "entry": "src/phone.ts", "run": "session" }
  ]
}
```

| | 应用脚本 `"run": "app"` | 会话脚本 `"run": "session"` |
|:--|:--|:--|
| 几份 | 扩展打开期间只有一份 | 每台投屏中的手机各一份 |
| 什么时候运行 | 扩展打开、GameScrcpy 启动时开始，扩展关掉、换版本或程序退出时停止 | 手机开始投屏时开始，断开时停止 |
| 能用的 | 主窗口的界面位置（页面、设置、卡片菜单、托盘……）、服务、MCP 工具、本机连接、语音、硬件、程序事件 | 投屏窗口的界面位置（侧边面板、右键菜单、状态区、工具栏按钮）、这台手机的触控、识图、键位、剪贴板 |
| 不能用的 | 要手机的接口（`touch.*`、`vision.*`、`ui.toast` 等），会得到 `GS-API-073` 或 `GS-API-001` | 主窗口的界面位置 |

- 要权限的接口在扩展没申请或用户没同意时抛 `GS-API-072`，信息里写着缺哪一项。用户自己的逻辑不受这些限制。
- 脚本可以用 `import ... from "./util.ts"` 引用包里的其他文件。GameScrcpy 不编译 TypeScript，只去掉类型标注，所以 `enum`、带代码的 `namespace`、构造函数的参数属性、装饰器、`import x = …` 和 `export =` 都不能用。
- 在任务里（事件处理、`every` 等启动的任务）打开的东西随任务结束关掉，在最上层打开的随逻辑停止关掉：监听、`net.listen` 的服务、语音监听、硬件设备与占用都是这样。
- `store.get` / `store.set` 是这个脚本自己的存储（最多 1 MiB 的 JSON，会话脚本按手机分开），程序重启后还在。脚本读不到清单 `settings` 的值，只会收到 `settings.changed` 事件（只有改了哪些键）；脚本自己的选项放在面板里、存进 `store`。
- `log.info` 等写进程序日志（`logic <名字> on <手机>: …`）。
- 应用脚本所在的沙箱进程意外停止时，GameScrcpy 一秒后重新启动它；一分钟内停了三次，就停掉这个扩展的逻辑。
- 开发时在扩展文件夹里放一个 `tsconfig.json`（可以照抄 [examples/components](../../examples/components) 里的），用 `gs check <文件夹>` 做类型检查；`gs types <文件夹>` 把接口的类型声明写进 `types/`，给编辑器用。`gs check` 要求有 `src/main.ts`（或 `src/main.js`），所以把一个脚本放在那里最省事。

## 面板与界面位置

扩展不运行界面代码：脚本给出一棵描述界面的树，GameScrcpy 用自己的控件和当前主题画出来，用户的操作作为事件交回脚本。面板在清单 `contributes.panels` 里声明（要扩展点 `panel` 和 `script`，见[清单参考](manifest.md)）。

```ts
const page = ui.panel("laps");
let laps: number[] = [];

function draw(): void {
    page.render({
        type: "column",
        children: [
            { type: "text", text: `${laps.length} laps`, style: "heading" },
            { type: "table", columns: ["Lap", "Time"], rows: laps.map((ms, index) => [index + 1, `${ms} ms`]) },
            { type: "button", id: "clear", title: "Clear", style: "danger", enabled: laps.length > 0 },
        ],
    });
}

page.on("click", ({ id }) => {
    if (id === "clear") {
        laps = [];
        draw();
    }
});

draw();
```

- `render(tree)` 换掉面板里原来的内容，`render(null)` 清空。树最大 256 KiB、最多 2000 个节点、16 层；GameScrcpy 每秒最多画 30 次，更快的只画最后一次。树不对时抛 `GS-API-075`，指出哪个节点；面板没为这个脚本声明时抛 `GS-API-076`。
- 节点：`column`、`row`、`group`（带标题的卡片）、`text`（最多 4000 个字符，`format: "markdown"` 时支持粗体、斜体、代码和列表，不打开链接、不显示图片）、`button`、`field`、`number`、`toggle`、`choice`、`slider`、`list`、`table`（最多 16 列、200 行）、`chart`、`canvas`、`image`（扩展文件夹里最大 2 MiB 的 PNG 或 SVG）、`progress`、`separator`、`deviceItem`、`steps`、`qr`、`keyValue`。能交互的节点要有 `id`。
- 事件：`on(类型, 处理函数)`，类型是 `click`（按钮）、`change`（输入框、数字、开关、选项、滑块，`value` 是新值）、`submit`（输入框里按 Enter）、`select`（列表项）、`open` 与 `close`（面板显示与收起）。处理函数收到 `{ id, value, signal }`。
- 树里的文字由脚本给出，不经过清单的 `l10n`。

各个位置在脚本里的用法：

| `where` | 脚本里 |
|:--|:--|
| `mirror`、`page`、`settings`、`extension`、`device` | `ui.panel(名字)` 的 `render` / `on`；`device` 的 `open` / `close` 事件的 `id` 是设备 id；`settings` 只在设置里这一节打开时收到 `open` / `close` |
| `menu` | `ui.menu(项)` 放进投屏画面的右键菜单，`ui.menu.on("select", …)` 收选中的项 |
| `status` | `ui.status(名字).render(树)`：一行文字、图标与进度，最多 64 个节点，文字最多 100 个字符 |
| `toolbar` | `ui.toolbar(名字).set({ title, icon, checked, enabled })` 改按钮，`set(null)` 回到清单的样子；`on("click", …)` 收点击。同名的 `ui.panel` 画过树时，点击还会在按钮下方弹出它 |
| `contextMenu` | `ui.contextMenu(名字).set(项)`（最多 16 项）放进卡片的“更多”菜单；`on("choose", ({ id, value, target }) => …)`，`target` 是 `{ kind: "device" 或 "profile", id, name }` |
| `tray` | `ui.tray(名字).set(项)`（最多 16 项）放进托盘菜单；`on("choose", …)` |

菜单项是 `{ id, title, icon, checked, enabled }`，有 `checked` 的项显示勾选，选中时 `value` 是相反的值。图标写 GameScrcpy 的图标名；面板、工具栏按钮和 `deviceItem` 还可以写 `svg:<包内路径>` 用扩展自带的 SVG（规则见[清单参考](manifest.md)）。

## 程序事件

```ts
events.on("profile.applied", ({ value }) => {
    log.info("profile applied", JSON.stringify(value));
});
```

`events.on(主题, 处理函数, 选项)` 每来一个事件启动一个任务，处理函数收到 `{ topic, value, signal }`。主题和值见[原生扩展指南](native.md#程序事件)里的表，也在 [sdk/docs/events.md](../../sdk/docs/events.md)。谁能收到什么：

- 用户自己的逻辑收到全部事件。
- 扩展的逻辑总能收到 `extension.enabled`、`extension.disabled` 和 `settings.changed`（只说本扩展自己的设置与语言、外观设置）。
- 扩展的会话脚本还能收到自己那台手机的事件；别的手机的事件，以及应用脚本和任务收手机事件，都要权限 `events.app`。没有权限时这些事件只是不来，不会报错。
- 其他主题是扩展的原生 DLL 用 `emit_event` 发的：本扩展的逻辑直接写主题，依赖的扩展发的写成 `"<扩展 id>:<主题>"`（要在 `dependencies` 里列出它）。
- 每个主题每秒前 100 个立刻送到，之后的合并成这一秒的最后一个。

## 扩展之间的服务

服务是一个扩展公开给别的扩展（和用户自己的逻辑）调用的函数。提供方在清单里声明，在应用脚本里回答：

```json
"services": [
  { "name": "best", "description": "The best lap so far, in milliseconds, or null.",
    "input": { "type": "object", "properties": { "track": { "type": "string" } }, "required": ["track"] },
    "output": { "type": ["integer", "null"] },
    "script": "main" }
]
```

```ts
const best = new Map<string, number>();

ext.provide<{ track: string }>("best", ({ track }, { caller }) => {
    log.info("asked by", caller === "" ? "the user's own logic" : caller);
    return best.get(track) ?? null;
});
```

调用方在 `dependencies` 里列出提供方（可选依赖也行），然后：

```ts
try {
    const ms = await ext.call<number | null>("alice.lap-timer/best", { track: "monza" });
    log.info("best lap", ms);
} catch (error) {
    log.warn("no best lap", String(error));
}
```

- 名字是 `<扩展 id>/<服务名>`。扩展的逻辑只能调它依赖的扩展和它自己的服务（否则 `GS-API-147`）；用户自己的逻辑可以调任何打开的扩展的服务。
- 每次调用在提供方里是一个单独的任务，处理函数收到参数和 `{ caller, permissions, signal }`：调用方的扩展 id（用户自己的逻辑是空串）、这次调用可以用的权限、调用方取消或超过 10 秒时中止的信号。返回值就是结果（不返回是 `null`），抛错就是失败。
- **权限取双方的交集**：处理函数里用到要权限的能力（如 `hardware.*`、`input.source`、`net.*`、`speech.listen`）时，调用方也必须有这项权限，否则被拒（`GS-API-072`）。只读数据、不需要权限的服务谁都能调。
- 参数和结果都要是 JSON，最多 252 KiB，并符合清单里的 `input` / `output`（只支持 `type`、`properties`、`required`、`items`、`enum`、`minimum`、`maximum`、`maxLength`、`title`、`description`）。
- 错误：`GS-API-145`（`ext.provide` 了清单没为这个脚本声明的服务）、`GS-API-146`（没有打开的扩展提供它，或提供方的应用脚本 10 秒内没有 `provide`）、`GS-API-147`（没有声明依赖）、`GS-API-148`（参数或结果不符合 schema、不是 JSON 或太大）、`GS-API-149`（服务失败，带它自己的信息，或超过 10 秒）。
- 调用方取消任务会取消这次调用。服务也可以由原生节点回答，见[原生扩展指南](native.md#服务)。

## 本机传入连接

`net.listen` 在 `127.0.0.1` 上开一个 HTTP 服务，让本机的程序（Stream Deck 的插件、OBS、自动化脚本、浏览器里的页面）调用扩展。扩展要申请权限 `network.listen`；用户还可以在扩展详情里单独关掉“接受本机程序的连接”。

```ts
const server = await net.listen(8765, async (request) => {
    if (request.method !== "POST") {
        return { status: 405, headers: { allow: "POST" }, json: { error: "send POST" } };
    }
    const body = request.json();
    log.info("asked", request.path, JSON.stringify(body));
    return { json: { ok: true } };
});
log.info("listening on", server.port);
```

- 端口 1024～65535，被占用或扩展已经开了 4 个服务时抛 `GS-API-102`；端口或来源写法不对抛 `GS-API-101`；没有权限或用户关掉了开关抛 `GS-API-100`。
- 每个请求在单独的任务里交给处理函数，可以同时处理几个。请求是 `{ method, path, query, headers, text, bytes, json() }`：`path` 不含查询串、已解码，头的名字是小写。
- 回答：什么都不返回是 204；返回一段文字是 200 的纯文本；返回对象 `{ status, headers, text | json | bytes }`。处理函数抛错时回 500。回答要在 10 秒内给出，否则回 504。
- 安全：只绑 `127.0.0.1`，别的电脑连不上；`Host` 只认 `127.0.0.1:<端口>` 与 `localhost:<端口>`；带 `Origin` 的请求只放行同一个服务的页面、本地文件（`null`）和 `origins` 选项里写的网站（最多 16 个，写成 `https://example.com`），它们的预检请求由程序回答。请求头最多 16 KiB、正文最多 1 MiB，每个服务最多同时 16 个连接，每个回答后关闭连接。只有 HTTP，没有 WebSocket。
- 网页可以让浏览器不带 `Origin` 地发出 GET 请求（例如一张图片），所以会改变状态的操作只接受 POST，GET 只用来读。
- 用户关掉开关时，正在听的服务被关掉；再打开不会自动重开，逻辑要自己再 `net.listen` 一次（或者重启扩展）。服务的 `isOpen` 说明它还开着没有。

完整的例子见 [deck-bridge](../../examples/extensions/deck-bridge)。

## 朗读与语音命令

```ts
await speech.say("Ready", { rate: 1.1 });

const listener = await speech.listen(["open map", "close map"], async ({ phrase, confidence }) => {
    log.info("heard", phrase, confidence);
    await speech.say(phrase === "open map" ? "Map open" : "Map closed", { interrupt: true });
}, { confidence: 0.7 });
```

- `speech.say(文字, { engine, voice, rate, volume, interrupt })` 朗读 1～1000 个字符，读完给 `true`，被打断、`speech.stop()`、任务取消或逻辑停止时给 `false`。同一个引擎一次读一段，其余排队；`interrupt: true` 先停掉正在读的、清空队列。朗读不需要权限。
- `speech.listen(命令, 处理函数, { engine, language, confidence })` 听麦克风里的语音命令：1～256 条、每条 1～64 个字符；把握不低于 `confidence`（默认 0.6）时一条一条调处理函数，收到 `{ phrase, text, confidence }`。引擎能自由听写时命令可以是 `null`。要权限 `speech.listen`（否则 `GS-API-113`）。监听在任务里开的随任务关、在最上层开的随逻辑停止关，引擎停了也会关，日志写原因；`isOpen` 说明它还在不在听。
- `speech.engines()` 列出引擎：`system` 是 Windows 自带的（朗读用经典与 OneCore 声音，识别用装了的识别器、只认命令列表），其余是扩展提供的 `<扩展 id>/<名字>`。`speech.voices({ engine })` 列出声音。没给的选项用设置 › 音频 › “朗读与语音命令”里的。
- 错误：`GS-API-110`（没有这个引擎，或它不能朗读、不能听、不认这种语言、没有这个声音）、`GS-API-111`（参数不对，或排队的文字、监听已经太多）、`GS-API-112`（引擎出错，例如没有麦克风）、`GS-API-113`（缺权限）。

完整的例子见 [voice-commands](../../examples/extensions/voice-commands)。

## 硬件与自制设备

- `hardware.request({ kind, filters })` 列出连着的串口、HID 或低功耗蓝牙设备让用户勾选；`hardware.list()` 列出已经允许的；`hardware.open(id, 串口选项)` 打开（同一台设备在 GameScrcpy 里只能开一次）；`hardware.forget(id)` 放弃。设备在任务里打开的随任务关，最上层打开的随逻辑停止关。扩展的逻辑要权限 `hardware`（`GS-API-080`），每个逻辑都只能开用户勾选过的设备（`GS-API-081`）。
- `hardware.claim(资源, { wait, onWanted })` / `hardware.release(资源)` 告诉别的扩展和 GameScrcpy 自己的会话“这个硬件我在用”，资源名写法同原生的 `claim`；`hardware.open` 打开的设备自动占用。
- `input.source(名字)` 给出一个输入源：`press(键)`、`release(键)`、`axis(键, x, y)` 把按键和轴送进键位，方案里绑定写 `Ext.<扩展 id>.<名字>.<键>`（用户自己的逻辑是 `Ext.mine.<名字>.<键>`），键位编辑器等按键时按下的键直接录入。应用脚本没有手机，按键送给前台的投屏窗口所在的手机。扩展的逻辑要权限 `hardware`。

## 原生节点与长任务

扩展的 DLL 提供的节点和任务在逻辑里都挂在 `ext.<扩展>` 下面（扩展 id 的每一段写成小驼峰）：

```ts
const version = ext.alice.board.version();
const job = ext.alice.board.flash({ port: "COM9" });
job.onProgress((fraction, text) => log.info(Math.round(fraction * 100), text));
job.onOutput((value) => log.info("found", JSON.stringify(value)));
const result = await job.result;
```

节点立即返回结果（100 毫秒以内），任务给出 `ExtensionJob`：`result` 在任务结束时兑现，`onProgress`、`onLog`、`onOutput` 收进度、日志和中途交出的结果，`follow(进度, 完成, 失败)` 是蓝图任务方块的写法，`cancel()` 请它停下；逻辑停止时任务被取消。错误码见[原生扩展指南](native.md#逻辑里看到的错误)。

## 手机画面的帧

会话脚本用 `media.frames(处理函数, { fps, size, region })` 拿手机画面的低频帧（每秒 0.1～10 帧、默认 2 帧，长边 64～1920 像素、默认 640），给分析或 AI 用：处理函数收到 `{ image, time }`，`image` 和 `vision.capture()` 给的一样。扩展的逻辑要权限 `media.read`；应用脚本没有手机，会得到 `GS-API-073`；参数不对是 `GS-API-120`，手机没有画面（例如没配采集卡的触控盒）是 `GS-API-121`，同时已经有 4 个订阅是 `GS-API-122`。

## MCP 工具与 AI

- 应用脚本可以用 `mcp.tool(名字, { description, input }, 处理函数)` 给本机的 AI 客户端（经 GameScrcpy 的 MCP 服务）提供工具，客户端看到的名字是 `ext_<扩展 id>_<名字>`（id 里的点和连字符写成下划线）；每次调用都要用户确认，60 秒内要回答，每个扩展最多 32 个。
- `ai.ask(问题, { picture, timeoutMs })` 向设置 › AI 里配好的服务提问，要权限 `ai`，并且用户在那里打开了“允许逻辑调用 AI”；每个逻辑每分钟最多 10 次。应用脚本不能带画面。

## 功能引导

扩展可以在清单的 `contributes.tours` 里带最多 4 段功能引导（扩展点 `tour`），写法见[清单参考](manifest.md)。引导不需要脚本，但要照亮页面或设置里的一节时，那个面板得是本扩展的（`where` 为 `page` 或 `settings`），并由应用脚本画出来。

- `start: "enabled"` 的引导在扩展第一次打开并载入后自动放一次；用户在设置里关掉了新手引导时不自动放。看完或跳过都会记住。
- 每一步先切到目标所在的页面再照亮它；目标不在（没有设备、面板还没画出来）时那一步跳过，所以先 `render` 面板。
- 扩展详情里每段引导有一个“重放”按钮；帮助文章里用链接 `action:tour:ext/<扩展 id>/<引导 id>` 重放。
- 标题最多 60 个字符，正文最多 300 个字符，2～12 步。文字的译文写在 `l10n` 里：`tours.<id>.title`、`tours.<id>.<第几步>.title`、`tours.<id>.<第几步>.text`（从 0 数）。

## 帮助文章

扩展可以带最多 16 篇帮助文章（`contributes.help`，扩展点 `help`），它们出现在帮助页左栏的“扩展”一组，能被搜到，AI 小助手的 `read_help` 也能读到（标明来自哪个扩展）。`file` 是英文的 Markdown，`locales` 给出各种语言的文件，界面语言没有对应文件时用英文的。

文章的写法和 GameScrcpy 自己的帮助相同：

- `#` 标题、段落、`-` 列表、`1.` 编号列表（每项开头的 **粗体** 成为这一步的标题）、表格（第一列是 `<kbd>Ctrl</kbd>` 这样的按键时显示成快捷键表）、带语言的代码块（` ```ts `）。
- 行内：`**粗体**`、`` `代码` ``、`<kbd>按键</kbd>`、`“名称”`、`**设置 > 音频**` 这样的路径。
- 提示框：`> [!TIP]`、`> [!NOTE]`、`> [!WARNING]` 开头的引用块；最后一行只有一个链接时，它变成提示框上的按钮。
- 链接：`help:<文章>` 打开 GameScrcpy 自己的文章，`help:ext/<扩展 id>/<id>` 打开扩展的文章，`action:tour:ext/<扩展 id>/<引导 id>` 重放扩展的引导，`https://` 打开浏览器。
- 图片：`![说明](images/pic.png)` 只认扩展文件夹里的相对路径，带 `:` 或出文件夹的图片会被丢掉；程序自己的示意图（`figure:`）不能用。

上一篇、下一篇只在同一个扩展的文章之间走。完整的例子见 [tour-and-help](../../examples/extensions/tour-and-help)。

## 常见错误码

| 代码 | 意思 |
|:--|:--|
| `GS-API-001` | 这个逻辑没有连着手机（应用脚本用了要手机的 `ui.*`） |
| `GS-API-072` | 扩展没申请或用户没同意这项权限；在服务里，也可能是调用方没有 |
| `GS-API-073` | 这个逻辑没有手机：应用脚本用了要手机的接口 |
| `GS-API-075`、`GS-API-076` | 面板、菜单的树不对；清单没为这个脚本声明这个面板 |
| `GS-API-080`～`GS-API-085` | 硬件：没权限、设备没允许、不在、打不开、参数不对 |
| `GS-API-100`～`GS-API-102` | 本机连接：没权限或开关关了、端口或来源不对、端口被占或太多 |
| `GS-API-110`～`GS-API-113` | 语音：引擎、参数、引擎出错、缺权限 |
| `GS-API-120`～`GS-API-122` | 画面帧：参数、没有画面、订阅太多 |
| `GS-API-145`～`GS-API-149` | 服务：没声明、没人提供、没依赖、不符合 schema、失败或超时 |
