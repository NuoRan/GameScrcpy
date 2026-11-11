# 事件记录（event-log）

简体中文 · [English](README.en.md)

用 `events.on` 订阅 GameScrcpy 自己的全部事件主题，在主窗口的一页里按时间列出来：手机连上与断开、投屏开始与结束、方案应用、前台应用、屏幕方向、录像、窗口焦点、扩展开关和设置改动。

## 文件

| 文件 | 内容 |
|:--|:--|
| `manifest.json` | 一个应用脚本、一个主窗口页面，权限 `events.app` |
| `src/main.ts` | 订阅事件、整理成表格、页面上的暂停、筛选与清空 |
| `tsconfig.json` | 给 `gs check` 和编辑器用 |

## 试一试

1. `gs validate examples\extensions\event-log`，`gs check examples\extensions\event-log`。
2. `gs pack examples\extensions\event-log -o event-log.gsext`，在扩展页“从文件安装…”，打开它并同意权限。
3. 打开左栏的“事件记录”，然后连上一台手机、开始投屏、换个方案、在手机上换个应用、关掉再打开一个扩展：每件事都会出现在表格里，最新的在最上面。

## 谁能收到哪些事件

- 扩展的逻辑总能收到 `extension.enabled`、`extension.disabled` 和 `settings.changed`（只说本扩展自己的设置与语言、外观设置）。
- 手机与窗口的事件（`device.*`、`session.*`、`profile.applied`、`app.foreground`、`orientation.changed`、`recording.*`、`window.focused`）：会话脚本能收到自己那台手机的；应用脚本、任务和别的手机都要权限 `events.app`。这个示例用的是应用脚本，所以申请了它；不同意时表格里只有扩展和设置的事件。
- 每个主题每秒前 100 个立刻送到，之后的合并成这一秒的最后一个。

每个事件的值见 [sdk/docs/events.md](../../../sdk/docs/events.md)。表格里 Phone 一列是值里的 `name`（没有时用 `phone`，也就是序列号），其余字段写在 Details 一列。页面上的文字是脚本给的，示例里只写了英文。
