# 引导与帮助示例（tour-and-help）

简体中文 · [English](README.en.md)

扩展可以自带功能引导（`contributes.tours`）和帮助文章（`contributes.help`）。这个示例带一段五步的引导、两篇中英文的帮助文章，以及一个主窗口页面和设置里的一张卡片，让引导有地方可指。

## 文件

| 文件 | 内容 |
|:--|:--|
| `manifest.json` | 扩展点 `script`、`panel`、`help`、`tour`；两个面板、一段引导、两篇帮助；引导与标题的中文译文 |
| `src/main.ts` | 画页面（`where: "page"`）和设置卡片（`where: "settings"`） |
| `help/guide.md`、`help/guide.zh_CN.md` | 示例的说明，带重放引导的链接 |
| `help/writing.md`、`help/writing.zh_CN.md` | 扩展帮助文章能用的写法 |
| `tsconfig.json` | 给 `gs check` 和编辑器用 |

## 试一试

1. `gs validate examples\extensions\tour-and-help`，`gs check examples\extensions\tour-and-help`。
2. `gs pack examples\extensions\tour-and-help -o tour-and-help.gsext`，在扩展页“从文件安装…”，然后打开它。
3. 扩展第一次打开并载入后，引导自己开始：先是中间的一张卡片，再照亮扩展页上它的卡片、左栏里的“引导示例”页面和设置 › 扩展里它的卡片，最后提示去看帮助。
4. 按 F1，帮助页左栏的“扩展”一组里有“引导与帮助示例”和“给扩展写帮助”两篇；第一篇里的“播放引导”会重放这段引导。扩展卡片的“详情”里也有重放按钮。

## 要点

- 引导的步骤 `target` 写 `card`、`device`、`page:<面板名>`、`settings:<面板名>` 或省掉；清单检查会核对面板存在、位置对得上。
- 引导要照亮的页面和设置卡片由应用脚本画出来；目标不在时那一步被跳过，所以脚本一启动就 `render`。
- 引导的文字用 `l10n` 翻译：`tours.intro.title`、`tours.intro.<第几步>.title`、`tours.intro.<第几步>.text`（从 0 数）；帮助文章的标题用 `help.<id>.title`，正文用 `locales` 给出各语言的文件。
- 帮助文章里的链接：`help:ext/example.tour-and-help/writing` 打开另一篇，`action:tour:ext/example.tour-and-help/intro` 重放引导。
