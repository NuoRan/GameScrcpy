# 给扩展写帮助

扩展的帮助文章是包里的 Markdown 文件，列在清单的 `contributes.help` 里。GameScrcpy 用和自己的帮助一样的部件来画它们，看起来和其他文章一样。

## 清单里怎么写

```json
"help": [
  { "id": "guide", "title": "The tour and help sample", "file": "help/guide.md",
    "locales": { "zh_CN": "help/guide.zh_CN.md" } }
]
```

`file` 是英文的文章，`locales` 给出其他语言的文件；界面语言没有对应的文件时显示英文的。清单还要声明扩展点 `help`。

## 文章里能用什么

- 标题、段落和列表。编号列表显示成一步一步的样子，开头的 **粗体** 成为这一步的标题。
- `代码`、**粗体**、<kbd>F1</kbd> 这样的按键，以及 **设置 > 扩展** 这样的路径。
- 以 `> [!TIP]`、`> [!NOTE]` 或 `> [!WARNING]` 开头的提示框。
- 表格。第一列是按键的表格显示成快捷键列表，就像下面这张：

| 按键 | 作用 |
|---|---|
| <kbd>F1</kbd> | 打开帮助 |
| <kbd>Ctrl</kbd>+<kbd>K</kbd> | 打开命令面板 |

## 链接

- `help:` 加 GameScrcpy 自己的文章名，例如[连接手机](help:connecting)。
- `help:ext/` 加扩展的 id 和文章的 id，例如[引导与帮助示例](help:ext/example.tour-and-help/guide)。
- `action:tour:ext/` 加扩展的 id 和引导的 id，播放那段引导。
- `https://` 开头的地址用浏览器打开。

> [!WARNING]
> 图片只能来自扩展自己的文件夹，写成 `images/step.png` 这样的相对路径。路径里带 `:` 的、或者在文件夹外面的图片会被略过。
