# 原生扩展指南

简体中文 · [English](native.en.md)

原生扩展是一个用 C 或 C++ 写的 Windows DLL，照 [`sdk/include/gamescrcpy_ext.h`](../../sdk/include/gamescrcpy_ext.h) 的接口和 GameScrcpy 打交道。要直接碰硬件、要每帧都快速处理触点、要给程序加新的设备或连接方式时用它；只是画界面、调已有接口的，用[逻辑](logic.md)更简单，也不需要用户完全信任你。

头文件由 `gs api write sdk` 从程序里的接口表生成，每个结构、字段和函数的说明都在里面，这里讲怎么把它们用起来。这一页说的接口版本是 `GS_EXT_ABI_VERSION` 2。

## 一个最小的 DLL

```cpp
#include <gamescrcpy_ext.h>

#include <string>

namespace {

const gs_ext_host* g_host = nullptr;

int32_t greet(void*, const gs_ext_value* args, uint32_t, gs_ext_result* result)
{
    const std::string text = "Hello, " + std::string(args[0].text, args[0].size) + "!";
    return g_host->set_text(result, GS_EXT_TYPE_STRING, text.data(), text.size());
}

const uint32_t kGreetParams[] = {GS_EXT_TYPE_STRING};

const gs_ext_node kGreet = {sizeof(gs_ext_node), "greet", kGreetParams, 1, GS_EXT_TYPE_STRING, nullptr, &greet};

const gs_ext_contribution kContributions[] = {
    {sizeof(gs_ext_contribution), GS_EXT_KIND_NODE, "greet", &kGreet},
};

const gs_ext_plugin kPlugin = {sizeof(gs_ext_plugin), GS_EXT_ABI_VERSION, nullptr, 1, kContributions};

}

GS_EXT_EXPORT int32_t gs_ext_main(const gs_ext_host* host, const gs_ext_plugin** plugin)
{
    if (host == nullptr || plugin == nullptr || host->abi_version != GS_EXT_ABI_VERSION) {
        return GS_EXT_UNSUPPORTED;
    }
    g_host = host;
    *plugin = &kPlugin;
    return GS_EXT_OK;
}
```

清单里写出同样的东西：

```json
"requires": { "native": "^1.0" },
"contributes": {
  "native": {
    "dll": "native.dll",
    "nodes": [ { "name": "greet", "params": [ { "name": "name", "type": "string" } ], "returns": "string",
                 "description": "Says hello to a name." } ]
  }
}
```

逻辑里这样调用：`ext.alice.hello.greet("world")`（扩展 id 的每一段写成小驼峰，`alice.lap-timer` 写成 `ext.alice.lapTimer`）。

## 入口与结构的大小

- DLL 导出 `gs_ext_main`。GameScrcpy 载入 DLL 后调它一次，传入宿主函数表 `gs_ext_host`；DLL 把指向自己的描述 `gs_ext_plugin` 的指针交回来。这份描述和它指向的一切由 DLL 持有，在 `unload` 被调用之前一直有效，GameScrcpy 从不往里写。
- 只导出旧入口 `gs_ext_entry`（接口版本 1）的 DLL 不会被载入，扩展页提示作者用当前的 SDK 重新编译。
- 每个由 DLL 填写、或者含有数组的结构，第一个字段都是 `uint32_t size`，填 `sizeof`。双方只读两边都认识的字段，读不到的当作 0，所以以后的版本只在结构末尾加字段，新 DLL 在旧程序上、旧 DLL 在新程序上都能用。数组的元素结构（`gs_ext_value`、`gs_ext_contact` 等）头文件里写明“以后不改”，没有 `size`。
- 后加的宿主函数在用之前先问 `supports("<功能名>")`。2.1.0 认识的功能名：`jobs`、`events`、`settings`、`claims`、`job-output`、`app-events`、`input-sources`、`services`、`transports`、`media`、`frame-taps`。

## 贡献表

`gs_ext_plugin.contributions` 是 `gs_ext_contribution { size, kind, name, data }` 的数组（最多 256 项），`kind` 是字符串，`data` 指向那一种的结构，`name` 与结构里的 `name` 都要和清单里的名字一致。GameScrcpy 不认识的 `kind` 记一行日志后跳过，DLL 照常载入；清单里声明了、DLL 里却没有的，扩展页会写出来。

| `kind` | 常量 | 结构 | 清单 |
|:--|:--|:--|:--|
| `touch-method` | `GS_EXT_KIND_TOUCH_METHOD` | `gs_ext_touch_method` | `touchMethods` |
| `node` | `GS_EXT_KIND_NODE` | `gs_ext_node` | `nodes` |
| `attachment` | `GS_EXT_KIND_ATTACHMENT` | `gs_ext_attachment` | `attachments` |
| `behavior` | `GS_EXT_KIND_BEHAVIOR` | `gs_ext_behavior` | `behaviors` |
| `job` | `GS_EXT_KIND_JOB` | `gs_ext_job` | `jobs` |
| `device-provider` | `GS_EXT_KIND_DEVICE_PROVIDER` | `gs_ext_device_provider` | `deviceProviders` |
| `input-source` | `GS_EXT_KIND_INPUT_SOURCE` | `gs_ext_input_source` | `inputSources` |
| `transport` | `GS_EXT_KIND_TRANSPORT` | `gs_ext_transport` | `transports` |
| `video-source` | `GS_EXT_KIND_VIDEO_SOURCE` | `gs_ext_video_source` | `videoSources` |
| `audio-source` | `GS_EXT_KIND_AUDIO_SOURCE` | `gs_ext_audio_source` | `audioSources` |
| `media-sink` | `GS_EXT_KIND_MEDIA_SINK` | `gs_ext_media_sink` | `mediaSinks` |
| `recording-exporter` | `GS_EXT_KIND_RECORDING_EXPORTER` | `gs_ext_recording_exporter` | `recordingExporters` |
| `frame-tap` | `GS_EXT_KIND_FRAME_TAP` | `gs_ext_frame_tap` | `frameTaps` |

语音引擎没有自己的种类：它由清单把几个长任务和一个节点组合起来（见[语音引擎](#语音引擎)）。插件描述里另有四个可选回调：`unload`（卸载前调一次）、`settings_changed`、`claim_wanted`、`on_event`，用不到就填 `NULL`。

## 宿主函数

| 函数 | 用途 |
|:--|:--|
| `log`、`now_us` | 写程序日志；只往前走的微秒时钟 |
| `store_get`、`store_set` | 关掉程序后还在的存储，整个最多 1 MiB 的 JSON |
| `allowed`、`notify` | 问用户是否同意了某项权限；显示系统通知（要 `notify`） |
| `set_text`、`set_error` | 节点把文字结果、失败原因写进 `gs_ext_result` |
| `supports` | 问 GameScrcpy 有没有某个后加的功能 |
| `job_progress`、`job_log`、`job_finish`、`job_fail`、`job_output` | 长任务报进度、写日志、结束、中途交出部分结果 |
| `emit_event` | 给本扩展的逻辑与面板（以及依赖它的扩展的逻辑）发事件 |
| `events_subscribe` | 订阅 GameScrcpy 的程序事件，在 `on_event` 里收 |
| `setting_get` | 读扩展的设置 |
| `devices_changed` | 让设备页重新列出某个设备提供者的设备 |
| `claim`、`release` | 占用、放开硬件 |
| `input_event` | 把输入设备的按键与轴送进键位 |
| `call_service` | 调别的扩展的服务 |
| `transport_changed`、`stream_opened`、`stream_data`、`stream_writable`、`stream_closed` | 连接方式 |
| `sources_changed`、`video_frame`、`audio_samples`、`stream_ended` | 画面与声音来源 |

字符串都是 UTF-8、带长度、不要求以零结尾（结构里注明“ended by a zero byte”的除外）。返回值是 `gs_ext_status`：`GS_EXT_OK`、`GS_EXT_FAILED`、`GS_EXT_UNSUPPORTED`、`GS_EXT_DENIED`（缺权限）、`GS_EXT_BAD_ARGUMENT`、`GS_EXT_BUSY`。

## 线程与时间

GameScrcpy 在不同的线程上调 DLL，每种回调有自己的要求：

| 回调 | 要求 |
|:--|:--|
| 节点 `call` | 同一个 DLL 的节点一次一个，100 毫秒内返回；可能和触控方式同时被调 |
| 触控方式 `open` / `close` / `frame` / `key`… | 同一时间只有一个线程调；`frame` 在实时线程上，要立刻返回 |
| 附加、行为的回调 | 同一份状态一次一个，在实时线程上，不能等；在独立进程里每次最多 2 毫秒；10 秒内失败三次，整个扩展被关掉 |
| 长任务 `start` | 立即返回，活在 DLL 自己的线程里干 |
| 设备提供者 `list` | 100 毫秒内返回 |
| `settings_changed` | 在 GameScrcpy 的线程上调（进程内是界面线程，独立进程里是它的消息线程） |
| `claim_wanted` | 一秒内回答 |
| `on_event` | 在不画界面的线程上按顺序一个一个来，要很快返回；排队超过 256 个的会被丢掉 |
| `call_service`、带 `GS_EXT_CLAIM_WAIT` 的 `claim` | 只能在长任务或 DLL 自己的线程里用，不能在节点、动作、触控方式、附加、行为、任务的 `start` 或回调里用 |
| `input_event`、`emit_event`、`job_*` | 任何线程都可以 |

## 在哪个进程里运行

用户装的扩展，每个 DLL 在自己的 `gs-exthost.exe` 进程里运行，GameScrcpy 经共享内存的通道和它说话；随程序发布的官方扩展载入到 GameScrcpy 自己的进程里。两种情况下 DLL 看到的接口完全一样，但独立进程里有这些规矩：

- 节点调用 2 秒没有回答、或通道坏了，进程会被结束，扩展页写“没有及时回应，已被结束”。
- 进程意外退出时，GameScrcpy 写一份崩溃报告（小型转储与写明出错模块和偏移的 `crash.txt`），记进扩展的健康状况；一分钟内停了三次，扩展被关掉，直到用户重新打开它。
- 进程以用户的权限运行，不是沙箱：DLL 能做的事和用户能做的一样多，所以安装前要过杀毒扫描与导入函数的检查。

开发时用 `gs ext test <文件夹> --hosted` 让测试也在独立进程里跑（见[测试扩展](testing.md)）。

## 系统节点

`gs_ext_node { size, name, params, param_count, returns, context, call }`：`params` 是每个参数的 `gs_ext_type`，顺序和清单一致。`call` 收到的参数已经按类型检查过；把结果写进 `result->value`（文字类的用 `set_text`）并返回 `GS_EXT_OK`，失败时 `set_error` 写原因、返回 `GS_EXT_FAILED`，逻辑里得到 `GS-API-062` 和这句原因。节点要的权限写在清单 `nodes[].permissions` 里，用户没同意时逻辑得到 `GS-API-063`，不会调到 DLL。

## 触控方式

`gs_ext_touch_method` 出现在设置的“触控方式”里，排在普通、UHID、AOA 之后。`open` 收到 `gs_ext_touch_open`：手机序列号和一段 JSON 选项 `{ settings, deviceSettings, phone, device, touchBox }`——扩展的全局设置、这台手机的单独设置、给 `setting_get` 用的手机 id、用户为它选的 USB / 串口 / 蓝牙设备（清单 `wantsDevice` 为 `true` 时，`{ kind, path, name }`，否则 `null`）、从触控盒卡片开始控制或采集卡配了这个触控盒时的设备 id（别的为 `null`）。`frame` 收到一帧触点（坐标同时给出画面比例和手机自然方向的比例），`key`、`text`、`mouse`、`gamepad` 可以为 `NULL`（交给普通方式）。任何调用返回非 `GS_EXT_OK`，触控就退回普通方式。

## 附加与行为

- **附加**（`gs_ext_attachment`）挂在组件上：`input` 在组件看到输入之前改它、加事件或吞掉，`output` 在手指命令到手机之前改它、推迟、加命令或丢掉，`wake` 在要求的时间到了时交出推迟的东西。`start` 收到参数 JSON（清单 `params` 的值，加上组件位置 `@position` 和画面短边单位 `@unit`）与随机种子：同样的输入必须得到同样的输出，随机只能用这个种子。
- **行为**（`gs_ext_behavior`）是组件库“扩展”分组里的一种新组件：清单声明它的输入槽和参数，`input` 收槽的事件，`frame`（`wants_frames` 为 1 时按输入整形的节拍调用）与 `wake` 里给出手指命令。

这两种都在实时线程上调，不能等待、不能做文件或网络操作。

## 长任务

```cpp
int32_t flashStart(void* context, uint64_t job, const char* args, size_t size);
void flashCancel(void* context, uint64_t job);

const gs_ext_job kFlash = {sizeof(gs_ext_job), "flash", nullptr, &flashStart, &flashCancel};
```

- `start(context, job, args, size)`：`job` 是任务号，`args` 是符合清单 `params` 的 JSON 对象。立即返回 `GS_EXT_OK`，在自己的线程里干活；返回别的表示没开始，之后不能再提这个任务。
- 进度用 `job_progress(context, job, 0～1 或 -1, 说明)`（每秒最多传 20 次）、日志用 `job_log`，最后**恰好一次** `job_finish(context, job, 结果 JSON)` 或 `job_fail(context, job, 原因)`。结果最多 252 KiB，要符合清单的 `returns`，没写 `returns` 的结果给逻辑的是 `null`。
- `job_output(context, job, JSON)` 在任务进行中交出一部分结果（例如扫描时每找到一台设备），每秒最多 100 次，逻辑用 `job.onOutput()` 收。先问 `supports("job-output")`。
- `cancel(context, job)` 请任务尽快停下；任务仍然以 `job_fail`（或已经做完时的 `job_finish`）结束。扩展被关掉、换版本或程序退出时，GameScrcpy 先取消所有任务，3 秒后不再等。
- 同一个扩展同时最多 4 个任务在跑，清单的 `most` 可以再收紧；逻辑超出时得到 `GS-API-067`。逻辑里任务失败是 `GS-API-065`、被取消是 `GS-API-066`。

逻辑里这样用：

```ts
const job = ext.alice.board.flash({ port: "COM9" });
job.onProgress((fraction, text) => log.info(Math.round(fraction * 100), text));
const result = await job.result;
```

蓝图里一个任务是一个带“进度、完成、失败”出口的方块。设备卡片上的动作也能启动任务（见[设备提供者](#设备提供者)）。

## 事件

`emit_event(context, 主题, JSON)` 把事件发给本扩展的逻辑与面板，以及在 `dependencies` 里列了本扩展的扩展的逻辑：

- 主题 1～64 个字节，只有小写字母、数字、`.` 和 `-`；不能以 GameScrcpy 自己的前缀开头（`device.`、`session.`、`profile.`、`app.`、`orientation.`、`recording.`、`window.`、`extension.`、`settings.`），这样扩展不能冒充程序。
- JSON 最大 64 KiB，空字符串表示 `null`。
- 每个主题每秒前 100 条立刻送出，同一秒里之后的只留最后一条，在这一秒结束时送出。
- 本扩展的逻辑用 `events.on("board.added", …)` 收，依赖它的扩展用 `events.on("alice.board:board.added", …)` 收。

## 程序事件

`events_subscribe(context, 主题, 1)` 订阅 GameScrcpy 自己的事件、`0` 退订，事件到 `on_event(context, 主题, JSON)`：

| 主题 | 值 | 权限 |
|:--|:--|:--|
| `device.connected`、`device.disconnected` | `{ phone, name }` | `events.app` |
| `session.started`、`session.ended` | `{ phone, name }`，结束时多 `failed` | `events.app` |
| `profile.applied` | `{ phone, name, profile, profileName }` | `events.app` |
| `app.foreground` | `{ phone, name, package, game }` | `events.app` |
| `orientation.changed` | `{ phone, name, rotation }`（四分之一圈） | `events.app` |
| `recording.started`、`recording.ended` | `{ phone, name, path }`，结束时多 `durationMs` | `events.app` |
| `window.focused` | `{ phone, name, window: "mirror" 或 "grid", focused }` | `events.app` |
| `extension.enabled`、`extension.disabled` | `{ id, name, version }` | — |
| `settings.changed` | `{ keys, values, extension, device }`，只说本扩展自己的设置与语言、外观设置 | — |

不认识的主题返回 `GS_EXT_UNSUPPORTED`，缺 `events.app` 返回 `GS_EXT_DENIED`。合并规则同上。

## 设置

清单 `settings` 里的设置用 `setting_get(context, 键, 手机, value)` 读，值以 JSON 文字放在 `value` 里，在同一线程下次调用前有效。手机传空是全局的值；传触控方式选项里的 `phone`，得到这台手机的单独值（`scope` 为 `device` 的设置），没有单独值时得到全局值。清单没声明的键返回 `GS_EXT_BAD_ARGUMENT`。用户改了设置后，GameScrcpy 调 `settings_changed(context, 改了的键的 JSON 数组)`，只报真正变了的键。

## 设备提供者

`gs_ext_device_provider { size, name, context, list, act }`，要权限 `devices.provide`。`list` 用 `set_text` 写一个 JSON 数组（最多 64 台），每台：

```json
{ "id": "board-30C6F7A1", "name": "Touch box", "kind": "touchBox",
  "summary": "Firmware 1.0.2 · phone on USB", "state": "ready", "stateText": "",
  "icon": "svg:icons/board.svg", "touchMethod": "box", "resource": "serial:COM9", "capture": true,
  "actions": [ { "id": "settings", "title": "Settings", "panel": "board" },
               { "id": "flash", "title": "Flash firmware", "job": "flash" },
               { "id": "disconnect", "title": "Disconnect" } ] }
```

| 字段 | 规则 |
|:--|:--|
| `id`、`name` | 必填，最多 64 个字节 |
| `kind` | 清单 `kinds` 里的一种，现在是 `touchBox`：能触控、没有画面的盒子 |
| `summary`、`stateText` | 卡片上的一行说明与状态文字，最多 200 个字节 |
| `state` | `ready`、`busy`、`problem`、`offline`，省掉是 `ready` |
| `icon` | 图标名或 `svg:<包内路径>` |
| `touchMethod` | 本扩展的一个触控方式；“控制”用它打开无画面的控制窗口，设备 id 放进触控选项的 `touchBox` |
| `resource` | 设备经由的硬件（写法同 `claim`）；别人占着时卡片写“正被 某某 使用” |
| `capture` | `true` 表示可以配采集卡画面：盒子连着时出现在采集卡设置的“触控方式”里，选了它，采集卡就用 `touchMethod` 触控，触控选项的 `touchBox` 同样是设备 id |
| `actions` | 最多 8 个按钮：`panel` 打开本扩展的一个面板（`where` 为 `device`），`job` 启动本扩展的一个任务，两者都没有的交给 `act` |

id 为 `settings` 的动作是卡片上的设置按钮，其余进卡片的“更多”菜单。任务的参数里有字符串参数 `device` 时，卡片把设备 id 传进去。设备变了就调 `devices_changed(context, 提供者名)`，GameScrcpy 会重新 `list`。

## 硬件占用

打开串口、HID、蓝牙或 USB 设备之前，先 `claim(context, 资源, flags)`，用完 `release`。资源名不分大小写：`serial:COM9`、`ble:AA:BB:CC:DD:EE:FF`、`hid:` 加 CreateFile 打开的路径、`usb:<vid>:<pid>:<序列号>`（十六进制）。

- 别人占着时返回 `GS_EXT_BUSY`。带 `GS_EXT_CLAIM_WAIT` 时先问占用方，对方同意就最多等 3 秒它放开——这要时间，只能在长任务或 DLL 自己的线程里用。
- 别人想要你占着的硬件时，GameScrcpy 调 `claim_wanted(context, 资源, 谁)`：一秒内返回 `GS_EXT_OK` 并在 3 秒内 `release` 表示让出，返回别的表示不让。`NULL` 等于不让。
- 扩展停下时，它占的全部放开。程序自己的 AOA 会话也走这一套，逻辑里的 `hardware.claim` 与此同义。

## 输入设备

清单 `inputSources` 声明设备和它的槽（`button`、`axis`、`stick`），贡献表里登记 `gs_ext_input_source { size, name }`，要权限 `input.provide`。按键或轴变化时调 `input_event(context, 设备, 槽, x, y, 时间)`：按钮按下 `x` 为 1、松开为 0，轴 `x` 为 -1～1，摇杆 `x` 左 -1 右 1、`y` 上 -1 下 1，时间用 `now_us` 的微秒或 0 表示现在。任何线程都可以调，立即返回；每个设备每秒最多 1000 条，回到 0 的总能送达。

方案里绑定写 `Ext.<扩展 id>.<设备>.<槽>`，键位编辑器等按键时直接按那个设备就能录入。扩展停下时它的按键全部松开、轴回到 0。

## 服务

`call_service(context, "<扩展 id>/<服务>", 参数 JSON, result)` 调别的扩展在 `contributes.services` 里提供的服务，最多等 10 秒。成功时 `result` 里是结果 JSON；失败时是原因：`GS_EXT_UNSUPPORTED`（没有打开的扩展提供它，或在 GameScrcpy 的线程上调了）、`GS_EXT_DENIED`（本扩展的 `dependencies` 里没列对方）、`GS_EXT_BAD_ARGUMENT`（参数或结果不符合对方的 schema）、`GS_EXT_FAILED`（服务失败或超时）。只能在 DLL 自己的线程里调；先问 `supports("services")`。

DLL 自己回答服务时，服务的 `node` 指向一个只有一个 `json` 参数、返回 `json` 的节点，它收到 `{ caller, permissions, input }`：调用方的扩展 id（用户自己的逻辑是空串）、双方都有的权限，和参数。`allowed()` 只回答本扩展自己的权限，所以要按 `permissions` 自己判断能为调用方做什么。

## 语音引擎

语音引擎由清单把任务和节点组合起来，扩展的引擎 id 是 `<扩展 id>/<名字>`，出现在设置 › 音频 › “朗读与语音命令”的下拉里，逻辑的 `speech.*` 也能选它：

```json
"speechEngines": [ { "name": "cloud", "title": "Cloud voice", "say": "speak", "voices": "voices",
                     "listen": "hear", "dictation": false, "languages": ["en-US", "zh-CN"] } ]
```

- `say` 任务的参数正好是 `text`（string）、`voice`（string，空是默认）、`rate`（number，0.5～2）、`volume`（number，0～1）；读完才结束，取消就停。
- `listen` 任务的参数正好是 `phrases`（json：字符串数组，或能自由听写时为 `null`）、`language`（string）、`confidence`（number）；一直跑到被取消，每听到一句用 `job_output` 交出 `{ phrase, text, confidence }`。
- `voices` 节点没有参数、返回 json 数组 `[{ id, name, language }]`。

清单检查会核对这些任务和节点在不在、参数对不对（`speech-engine-invalid`）。

## 连接方式

`gs_ext_transport` 给程序加一种连手机的办法（中转服务器、隧道、云手机、别的发现方式），要权限 `transport`，联网的另加 `network`：

- `discover` 用 `set_text` 写出现在能连的手机 `[{ id, name, model, detail }]`，100 毫秒内返回；能连的手机变了就调 `transport_changed`。这些手机出现在设备页，标出连接方式。
- 会话的每个通道（先 `control`，再按需 `video`、`audio`、`bulk`）各要一条字节流：`open(context, stream, gs_ext_stream_open*)` 立即返回，能传数据时调 `stream_opened`，连不上时调 `stream_closed` 并写原因，都要在 20 秒内。这条流要通到这次会话的服务端在手机上监听的抽象套接字（`gs_ext_stream_open.session` 里写着它、会话的 token，以及 GameScrcpy 经 adb 启动服务端的做法），在手机上启动服务端是连接方式自己的事。GameScrcpy 在这条流上跑正常的会话协议，所以加密、认证和画面编码都不变。
- `send` 每次最多 64 KiB：`GS_EXT_OK` 表示全部收下，`GS_EXT_BUSY` 表示一个也没收，等 `stream_writable` 后 GameScrcpy 再发。从手机来的字节用 `stream_data` 按顺序交上来。
- 流断了（包括手机那边关了）调 `stream_closed`；会话按连接断开处理并尝试续上。GameScrcpy 用完一条流时调 `close`。

先问 `supports("transports")`。

## 画面、声音与媒体输出

要权限 `media.capture`（来源）或 `media.output`（输出、导出）。先问 `supports("media")`。

- **画面来源** `gs_ext_video_source`：`list` 写出能打开的画面 `[{ id, name }]`，它们像采集卡一样出现在设备页；`open(context, id, 参数, stream)` 立即返回，之后在自己的线程里用 `video_frame(context, stream, gs_ext_video_frame*)` 交帧（`GS_EXT_PIXEL_BGRA` 或 `GS_EXT_PIXEL_NV12`，2～4096 像素，时间戳用 `now_us` 的时钟），GameScrcpy 复制进一个几帧的环、显示最新的一帧。设备拔了用 `stream_ended` 说明原因；画面列表变了调 `sources_changed`。
- **声音来源** `gs_ext_audio_source`：同样的 `list` / `open` / `close`，用 `audio_samples` 交 48 kHz 的 16 位或浮点 PCM（1～8 声道）。和同一个扩展里 id 相同的画面一起播放。
- **媒体输出** `gs_ext_media_sink`：用户在投屏窗口的“更多”菜单里把窗口送到这里（虚拟摄像头、推流、OBS 等）。`open` 收到手机、尺寸与格式，`frame` 收到按清单尺寸加了黑边的 BGRA 帧，`audio` 收到声音，都要复制后立即返回；`close` 时释放。
- **录像导出** `gs_ext_recording_exporter`：录像列表“导出为”里多一种格式。`start(context, job, 输入 MP4 路径, 输出路径)` 像长任务一样在自己的线程里转换，用 `job_progress` 报进度、`job_finish` / `job_fail` 结束；算作扩展的 4 个任务之一。

## 取帧

`gs_ext_frame_tap` 读每台手机的全速投屏画面（用户在投屏窗口的“更多”菜单里打开它），要权限 `media.read`，先问 `supports("frame-taps")`。`open` 收到手机、尺寸与旋转（画面转了或尺寸变了会关掉再打开），`frame` 在 GameScrcpy 为它开的线程上收到只读的 BGRA 帧（长边按清单的 `size` 缩小、每秒不超过清单的 `fps`），可以慢慢处理，期间新来的帧只留最新的一帧。`frame` 可以往 `gs_ext_overlay` 里放最多 256 个形状（矩形、线、点、文字，颜色取主题的几种），画在画面上；每一帧的形状替换上一帧的，`keep` 设为 1 保留上次的。

逻辑里拿低频的帧用 `media.frames`（见[逻辑扩展指南](logic.md)）。

## 一个 DLL 对应多种处理器

清单的 `dll` 可以写成 `{ "x64": "bin/x64/native.dll", "arm64": "bin/arm64/native.dll" }`，GameScrcpy 载入与这台电脑相同处理器的那一份，没有就说明“这个扩展没有适合这台电脑的版本”。`gs validate` 检查每份 DLL 的处理器和它是否导出 `gs_ext_main`。

## 构建

- 用 Visual Studio 2022 编译 x64（需要时另编 arm64）的 DLL，包含 `gamescrcpy_ext.h`，导出 `gs_ext_main`（头文件的 `GS_EXT_EXPORT` 就是 `__declspec(dllexport)`）。
- 建议静态链接 C/C++ 运行库（`/MT`），这样 DLL 不依赖用户电脑上的运行库版本。
- `gs ext init native <文件夹>` 生成一个能直接编译的起点：CMake 工程、头文件的副本、一个节点、一个长任务、一个 `contributes.cli` 命令和一个测试（见[测试扩展](testing.md)）。
- 包里带上编好的 DLL；源代码要不要一起发由你决定，名字以点开头的文件夹（如 `.build`）不会被打包。

## 逻辑里看到的错误

| 代码 | 意思 |
|:--|:--|
| `GS-API-060` | 扩展没打开 |
| `GS-API-061` | 参数类型不对，或任务缺参数 |
| `GS-API-062` | 节点失败（带 `set_error` 的原因） |
| `GS-API-063` | 扩展缺这个节点或任务要的权限 |
| `GS-API-064` | 节点超过 100 毫秒没有返回 |
| `GS-API-065` | 任务失败（带 `job_fail` 的原因），或结果不符合 `returns` |
| `GS-API-066` | 任务被取消 |
| `GS-API-067` | 扩展同时在跑的任务已经到上限 |
