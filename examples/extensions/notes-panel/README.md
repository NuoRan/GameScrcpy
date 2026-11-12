# 笔记（notes-panel）

简体中文 · [English](README.en.md)

主窗口左栏多一页“笔记”：笔记存成你选定的文件夹里的文本文件（`files.folder`），点一下按钮，还能从 GitHub 取回 GameScrcpy 的最新版本、记成一条笔记（`net.fetch`）。

## 文件

| 文件 | 内容 |
|:--|:--|
| `manifest.json` | 一个应用脚本、一个主窗口页面；权限 `files.paths`（声明文件夹 `notes`）与 `network`（只列 `api.github.com`） |
| `src/main.ts` | 读写笔记、取最新版本，以及画页面 |
| `tsconfig.json` | 给 `gs check` 和编辑器用 |

## 试一试

1. `gs validate examples\extensions\notes-panel`，`gs check examples\extensions\notes-panel`。
2. `gs pack examples\extensions\notes-panel -o notes-panel.gsext`，在扩展页“从文件安装…”，打开它并同意权限。
3. 左栏多了“笔记”一页。第一次打开时 GameScrcpy 请你为它选一个文件夹（对话框里写着清单 `purpose` 的那句话），以后记住这个文件夹；在扩展的设置里能“忘记”它。
4. 填标题和内容、点“Add”，文件夹里多一个 `标题.txt`；在列表里点一条笔记看全文，“Remove the note”删掉文件。直接往文件夹里放 `.txt`，点“Reload”也会出现。（面板里的文字由脚本给出，不经过清单的 `l10n`，所以是英文。）
5. 点“Add the newest GameScrcpy release”，扩展向 `api.github.com` 要最新的发布，记成一条“GameScrcpy v…”的笔记。

## 怎么做的

- `files.folder("notes")` 打开清单里声明的文件夹 `notes`；GameScrcpy 第一次会请用户选真正的位置。句柄只能碰这个文件夹里面的东西：`list()`、`text(路径)`、`write(路径, 文字)`、`remove(路径)`。页面第一次打开时才去打开文件夹，用户取消了就显示原因，并留一个“Choose the folder”按钮。
- `net.fetch(地址, { headers })` 由 GameScrcpy 去问 GitHub 的接口；清单 `network` 的 `hosts` 只列了 `api.github.com`，别的主机（包括重定向过去的）都会被拒绝（`GS-API-155`）。回答不管状态码多少都会给出，所以要自己看 `status`。
- 出错时把信息写在页面顶上，不弹窗。
