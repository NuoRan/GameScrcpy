# 测试扩展

简体中文 · [English](testing.en.md)

不开 GameScrcpy、不接手机，也能把扩展的大部分问题找出来。下面的命令都是程序旁边的 `gs.exe` 提供的。

| 命令 | 做什么 |
|:--|:--|
| `gs validate <文件夹 \| 包.gsext \| 整合包.gsbundle> [--json]` | 检查清单和文件，编译滤镜、读方案，检查签名和每份 DLL 的处理器与入口 |
| `gs check <文件夹> [--json]` | 对文件夹里的 TypeScript 做类型检查和规则检查 |
| `gs types <文件夹>` | 把脚本接口的类型声明写进 `types/`，给编辑器用 |
| `gs ext test <文件夹 \| 包.gsext> [--test <文件>]... [--hosted] [--json]` | 在模拟宿主里载入清单、DLL 和脚本，跑 `tests/*.json` |
| `gs ext init <native \| script \| theme> <文件夹> [--id <扩展 id>]` | 生成一个带测试的最小扩展 |
| `gs ext <扩展 id \| 文件夹 \| 包.gsext> <命令> [--<参数> <值>]... [--hosted] [--json]` | 运行扩展在 `contributes.cli` 里声明的命令 |

## `gs validate`

```powershell
gs validate .\my-extension
gs validate .\alice.lap-timer-1.2.0.gsext --json
```

它和 GameScrcpy 安装时用同一套检查：清单的每个字段、包里的文件、路径与大小；再编译每个滤镜步骤（`--compile-timeout <秒>` 改每个着色器的等待时间，默认 20 秒）、读每个方案和补丁，检查包的签名，以及 `contributes.native.dll` 里每份 DLL 的处理器和是否导出 `gs_ext_main`。每条结果一行：严重程度、代码、位置和细节，下面一行是改法；最后一行是 `<id> <版本>: ok` 或错误与警告的个数。`--json` 输出同样的内容给工具用。有错误时退出码是 1，用法不对是 2。

## `gs check` 与 `gs types`

`gs check <文件夹>` 用 GameScrcpy 自带的 TypeScript 编译器检查文件夹里所有的 `.ts`（`types/`、`node_modules` 和点开头的文件夹除外），并检查 GameScrcpy 去不掉的写法（`enum`、装饰器等）和几条规则（从不 `await` 的死循环、找图之前固定的 `sleep`）。它需要文件夹里有 `tsconfig.json` 和 `src/main.ts`（或 `src/main.js`）。全部通过时最后一行是 `no problems`。

`gs types <文件夹>` 写出 `types/gamescrcpy.d.ts` 与 `types/extensions.d.ts`（这台电脑上装的原生扩展的节点），编辑器就能补全和提示；GameScrcpy 更新后再运行一次。

## 模拟宿主：`gs ext test`

```powershell
gs ext test .\my-extension
gs ext test .\my-extension --hosted
gs ext test .\alice.lap-timer-1.2.0.gsext --test .\more-tests\flash.json --json
```

`gs ext test` 不用 GameScrcpy 也不用手机，就能跑扩展：

1. 用 `gs validate` 的规则检查清单和文件，有错误就停下（退出码 2）。
2. 载入每个脚本，给它 2 秒的测试时间和一台假手机。对 `ui.*` 的调用被记下来、不真正画；用到模拟宿主没有的服务（例如本机连接、语音）的脚本标成 `limited`，脚本自己出错的标成 `failed`，扩展就算载入失败。
3. 载入 DLL：默认在 `gs` 自己的进程里，`--hosted` 时像 GameScrcpy 对待别人的扩展那样放进 `gs-exthost.exe`。模拟宿主的宿主函数记下日志、通知、事件和设备变化，实现长任务、设置、硬件占用与服务的规则。
4. 跑扩展文件夹里 `tests/` 下的每个 `.json`（按文件名排序），或者 `--test` 给出的文件。每个测试从新的状态开始：新载入的 DLL、清单里的默认设置、清单申请的全部权限。

报告里每个测试一行，下面是每一步的结果（`pass` 或 `FAIL`、序号、测试时钟的毫秒、说明），最后是“N passed, M failed”。退出码：0 全部通过，1 有失败的步骤（或者一个测试都没有），2 扩展载入不了。`--json` 输出同样的内容。

### 测试文件

```json
{
  "format": "gamescrcpy.ext-test",
  "version": 1,
  "name": "flashing",
  "granted": ["hardware"],
  "settings": { "speed": 115200 },
  "deviceSettings": { "mock-phone": { "port": "COM7" } },
  "holders": [ { "resource": "serial:COM9", "by": "AOA session", "yields": true } ],
  "timeout": 20000,
  "steps": [
    { "do": "call", "node": "version", "expect": { "value": "1.0.2" } },
    { "do": "job", "job": "flash", "args": { "port": "COM9" },
      "expect": { "result": { "written": true }, "progress": [ { "text": "erasing" }, { "fraction": 1 } ] } },
    { "do": "devices", "expect": { "count": 1, "devices": [ { "state": "ready" } ] } },
    { "do": "settings", "settings": { "speed": 9600 }, "expect": { "changed": ["speed"] } },
    { "do": "wait", "ms": 1500 },
    { "do": "events", "expect": { "events": [ { "topic": "board.added" } ] } }
  ]
}
```

| 字段 | 意思 |
|:--|:--|
| `format`、`version` | 可以省掉；写了就必须是 `gamescrcpy.ext-test` 与 `1` |
| `name` | 报告里的名字，默认是文件名 |
| `granted` | 这个测试同意的权限（必须是清单申请了的），省掉就是全部 |
| `settings`、`deviceSettings` | 开始时的设置；`deviceSettings` 按手机给，假手机叫 `mock-phone` |
| `holders` | 开始时已经占着硬件的“别人”：`{ resource, by, yields }`，`yields` 表示被问时会让出 |
| `timeout` | 每一步等结果的最长毫秒数（1～600000，默认 10000） |
| `steps` | 1～1000 步；每步 `do` 说做什么，`name` 是说明，`expect` 是要检查的结果，`timeout` 覆盖这一步的等待 |

### 步骤

| `do` | 输入 | `expect` 能查 |
|:--|:--|:--|
| `call` | `node`、`args`（数组） | `value`、`error` |
| `job` | `job`、`args`（对象）、`id`、`wait`（默认 `true`；`false` 时只启动） | `result`、`error`、`progress`、`logs` |
| `job.cancel` | `id` | `ended` |
| `job.wait` | `id` | `result`、`error`、`progress`、`logs` |
| `touch.open` | `method`、`session`、`phone`、`device`、`touchBox`、`options` | `opened` |
| `touch.frame` | `session`、`contacts`（`{ id, action, x, y, … }`，`action` 为 `down`、`move`、`up`、`cancel`）、`rotation` | `ok`、`sent` |
| `touch.key` | `session`、`key` 或 `keycode`、`down`、`repeat`、`meta` | `ok` |
| `touch.text` | `session`、`text` | `ok` |
| `touch.mouse` | `session`、`buttons`、`dx`、`dy`、`wheel` | `ok` |
| `touch.pad` | `session`、`buttons`、`hat`、`leftX`、`leftY`、`rightX`、`rightY`、`leftTrigger`、`rightTrigger` | `ok` |
| `touch.close` | `session` | — |
| `devices` | `provider` | `devices`、`count`、`error`、`problems` |
| `device.act` | `provider`、`device`、`action` | `ok`、`error` |
| `events` | — | 输出上一次 `events` 之后来的事件 |
| `settings` | `settings`、`deviceSettings` | `changed`（`settings_changed` 收到的键） |
| `wait` | `ms` | 测试时钟向前跳 `ms` 毫秒（进程内的 DLL 读 `now_us` 也跟着跳） |
| `holder` | `resource`、`by`、`yields` | 让“别人”占着一个硬件 |
| `holder.release` | `resource` | `released` |
| `claim.want` | `resource`、`by` | `held`、`yielded`、`released`：别人想要扩展占着的硬件时它怎么回答 |
| `claims` | — | `held`、`holders` |
| `scripts` | — | `loaded`（应该载入成功的脚本名）、`logs`（按顺序出现的日志片段） |
| `manifest` | — | `value`（读进来的清单） |

每一步都还能查四种“发生了的事”，都是按顺序、在这一步的等待时间内出现就算：`events`（DLL 发的事件 `{ topic, value }`）、`logged`（日志 `{ level, text }` 或一段文字）、`notified`（通知 `{ title, text }` 或一段文字）、`devicesChanged`（调了 `devices_changed` 的提供者名）。

比较的规则：对象只比 `expect` 里写了的键；数组长度要一样、逐个比；数字允许极小的误差；`error` 写 `true`、`false` 或错误信息里的一段文字；`progress`、`logs` 这类列表按顺序找，文字只要包含就算。

### 测脚本扩展

模拟宿主不画界面、没有手机，所以对脚本扩展主要检查它能载入、日志对不对：

```json
{ "steps": [ { "do": "scripts", "expect": { "loaded": ["main"], "logs": ["ready"] } } ] }
```

面板画得对不对、和程序的交互，到 GameScrcpy 里看（见下文）。

## 生成起点：`gs ext init`

```powershell
gs ext init script .\lap-timer --id alice.lap-timer
gs ext init native .\board-tools
gs ext init theme .\night-sky
```

- `script`：`src/main.ts`（一个画主窗口页面的应用脚本）、`tsconfig.json`、`types/gamescrcpy.d.ts`、`tests/basic.json`；`gs check`、`gs validate` 与 `gs ext test` 都能直接通过。
- `native`：CMake 工程、`include/gamescrcpy_ext.h` 的副本、`src/native.cpp`（一个节点 `greet` 和一个长任务 `countdown`）、清单里一个 `contributes.cli` 命令、`tests/basic.json`。用 Visual Studio 2022 编译：`cmake -S . -B .build`，再 `cmake --build .build --config Release`，DLL 落在文件夹里。
- `theme`：一个深色、浅色各一组颜色的主题和它的测试。

每种都带一份 `AGENTS.md`，写着怎么检查、测试和打包，给你和 AI 编程助手看。不写 `--id` 时 id 是 `my.<文件夹名>`；`gamescrcpy.` 开头的 id 不能用。目标文件夹必须是空的。

## 命令行子命令

清单的 `contributes.cli` 把原生任务变成命令（写法见[清单参考](manifest.md#cli最多-16-个)）：

```powershell
gs ext .\board-tools --help
gs ext .\board-tools countdown --from 5
gs ext alice.board flash --port COM9 --json
```

- 第一个参数可以是文件夹、`.gsext`，或者这台电脑上已经装了的扩展的 id（用最新的版本）。
- 参数写成 `--名字 值` 或 `--名字=值`，按任务参数的类型解析：`true` / `false`、数字、点写 `x,y`、区域写 `x,y,宽,高`、`json` 写 JSON；没给的用清单里的 `default`，必需的参数没给会提示。
- 进度一行一行打出来（`[ 40%] 说明`），日志缩进打出来，最后打出结果 JSON；`--json` 输出一份报告。按 Ctrl+C 取消任务。`--hosted` 让 DLL 在 `gs-exthost.exe` 里跑。
- 退出码：0 任务成功，1 任务失败，2 扩展载入不了或用法不对。

## 在 GameScrcpy 里试

1. `gs pack <文件夹>` 打包（见[打包与发布](publishing.md)），在扩展页“从文件安装…”装上，打开它并同意权限。
2. 改了代码就重新打包、再装一次：同一个版本号也会替换。装好的扩展在 `%APPDATA%\GameScrcpy\extensions\<扩展 id>\<版本>\`（便携版在 `data\extensions`）。
3. 看程序日志（`%APPDATA%\GameScrcpy\logs`）里的 `[extensions]` 和 `logic …` 行；扩展页卡片的“详情”列出清单问题、面板问题和健康状况。
4. 投屏窗口不接手机也能看：`GameScrcpy.exe --demo-device tests\data\video\emulator_h265.media` 多出一台循环播放录像的演示设备（这个文件在源码仓库里）。
