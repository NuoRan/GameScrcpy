# 读屏朗读（read-aloud）

简体中文 · [English](README.en.md)

投屏窗口的工具栏多一个“朗读”按钮：按一下，扩展用 Windows 自带的识字读出手机画面一块区域里的字（`vision.recognize("system", "ocr", …)`），再读出声来（`speech.say`）。区域和语言在侧边面板里调。

## 文件

| 文件 | 内容 |
|:--|:--|
| `manifest.json` | 一个会话脚本；两个面板：工具栏按钮 `read` 与侧边面板 `area`；权限 `device.screen` |
| `src/main.ts` | 识别、朗读，以及画侧边面板 |
| `tsconfig.json` | 给 `gs check` 和编辑器用 |

## 试一试

1. `gs validate examples\extensions\read-aloud`，`gs check examples\extensions\read-aloud`。
2. `gs pack examples\extensions\read-aloud -o read-aloud.gsext`，在扩展页“从文件安装…”，打开它并同意权限。
3. 打开一台手机的投屏窗口（没有手机时可以用 `GameScrcpy.exe --demo-device tests\data\video\emulator_h265.media` 的演示设备）。工具栏多了一个喇叭按钮，侧边面板里多了“读屏朗读”。
4. 在侧边面板里用“Left”“Top”“Width”“Height”四个滑块框出要读的区域（画面的比例，默认是左下角聊天区那一块），需要的话选识别的语言。面板里的文字由脚本给出、不经过清单的 `l10n`，所以是英文。
5. 按工具栏的按钮或面板里的“Read now”：读出的字显示在“Last read”里，同时读出声来。读的时候按钮显示为按下。

Windows 的识字用的是系统里装了的 OCR 语言（中文 Windows 通常有简体中文与英文），“As in Windows”按用户的语言来认。

## 怎么做的

- `vision.recognize("system", "ocr", 区域, { language })` 在下一帧里识别这块区域，结果是一段一段的字，各带位置与把握；`"text" in part` 只取文字。2 秒内没有画面时得到 `null`。
- `speech.say(文字, { interrupt: true })` 朗读，打断还没读完的上一句；朗读不需要权限。
- 只能在会话脚本里识别（应用脚本没有手机），所以清单里的脚本是 `"run": "session"`，每台投屏的手机各有一份，区域与语言存在这个脚本的 `store` 里，下次还在。
- `ui.toolbar("read").set({ checked: true, … })` 在读的时候把按钮显示为按下，读完 `set(null)` 回到清单的样子。
