# 语音命令（voice-commands）

简体中文 · [English](README.en.md)

说一句命令，扩展就按下你为它选的键，再出声回答：`speech.listen` 听命令，`input.source` 把键送进键位，`speech.say` 朗读回答。

## 文件

| 文件 | 内容 |
|:--|:--|
| `manifest.json` | 一个应用脚本、一个主窗口页面，权限 `speech.listen` 与 `hardware` |
| `src/main.ts` | 监听、按键、回答，以及管理命令的页面 |
| `tsconfig.json` | 给 `gs check` 和编辑器用 |

## 试一试

1. `gs validate examples\extensions\voice-commands`，`gs check examples\extensions\voice-commands`。
2. `gs pack examples\extensions\voice-commands -o voice-commands.gsext`，在扩展页“从文件安装…”，打开它并同意权限。
3. 左栏多了“语音命令”一页。第一次打开时按识别语言准备了三条命令：中文是“打开地图”“打开背包”“换弹”，英文是“open map”“open bag”“reload”。页面上可以换引擎、语言和把握程度，加减命令（说的话、按哪个键、回答什么）。
4. 在键位编辑器里把 `Ext.example.voice-commands.voice.map` 绑到打开地图的按钮上：等按键时说一声“打开地图”就录进去了。之后在投屏窗口里说“打开地图”，键位就按下那个按钮，并回答“地图”。

Windows 自带的引擎只认命令列表，用的是系统里装了的语音识别语言（中文 Windows 通常有 zh-CN）。没有麦克风、引擎不认这种语言时，页面顶上会写出原因。

## 怎么做的

- 打开或改了设置后，`restart()` 关掉旧的监听，用当前的命令、引擎、语言和把握程度重新 `speech.listen`。
- 每听到一条命令，`heard()` 按下这条命令的键 80 毫秒（`keys.press` / `keys.release`），再用 `speech.say(…, { interrupt: true })` 回答，打断还没读完的上一句。
- 命令、引擎、语言和开关存在 `store` 里，下次启动还在。扩展的逻辑读不到清单里的设置，所以选项放在自己的页面上。
- 应用脚本没有手机，按键送给前台的投屏窗口所在的手机。
