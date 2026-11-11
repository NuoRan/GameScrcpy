# 打包与发布

简体中文 · [English](publishing.en.md)

扩展以 `.gsext` 文件发布：它是一个 zip，最上层是 `manifest.json` 和扩展的其他文件。用户在扩展页“从文件安装…”装它，或者点网页上的 `gamescrcpy://install` 链接。

## 打包：`gs pack`

```powershell
gs pack .\lap-timer
gs pack .\lap-timer -o .\dist\alice.lap-timer-1.2.0.gsext
gs pack .\lap-timer --sign $env:USERPROFILE\keys\alice.key
```

- 先做和 `gs validate` 一样的全部检查，有错误就不打包。
- 默认写到文件夹旁边，名字是 `<扩展 id>-<版本>.gsext`；`-o` 指定别的位置，但不能写在被打包的文件夹里面。
- 文件夹里的所有文件都会进包，名字以点开头的文件和文件夹（`.git`、`.build`、`.vscode` 等）除外。源代码、测试和 `README` 要不要一起发由你决定，不想发的放到别处或用点开头的文件夹。
- 包的上限：最多 4096 个文件，单个文件最大 64 MiB，解开后一共最大 256 MiB，压缩比不超过 100。
- `--official` 只给 GameScrcpy 自己的扩展用（允许 `gamescrcpy.` 开头的 id）。

## 版本号与更新

- 每次发布都提高 `version`（语义化版本：修了问题加修订号，加了功能加次版本号，改得不兼容加主版本号）。用户装新版本会替换旧版本，设置与已同意的权限按 `id` 保留。
- 新版本多申请了权限（或者 `network` 多了域名、`files.paths` 多了文件夹），或者多了必需的依赖，用户要重新打开扩展、同意新的要求，扩展才会载入。尽量一次把需要的权限申请清楚，理由写具体。
- 2.1.0 不会自己检查或下载更新：在你的发布页、仓库或网页上告诉用户有新版本，最方便的是给一个安装链接（见下文）。
- 不要改 `id`：换了 id 就是另一个扩展，设置、权限和签名都不跟过去。

## 签名

签了名的包，用户能看到“已校验的发布者”，之后的更新也必须由同一个人签：别人改过的包、冒充你发的更新都装不上。没签名的原生扩展在安装框里会有更醒目的提醒。

1. 生成一对钥匙（只做一次）：

   ```powershell
   gs ext keygen $env:USERPROFILE\keys\alice.key
   ```

   它写出私钥 `alice.key` 和公钥 `alice.pub`，并打印出要放进清单的一行，例如 `"authorKey": "RWQ…"`。私钥只能你自己有：谁拿到它谁就能以你的名义签名。带密码的 minisign 私钥 `gs` 用不了，可以用 `minisign -C -W` 给一份副本去掉密码。
2. 把 `authorKey` 写进 `manifest.json`。
3. 打包时签名：`gs pack <文件夹> --sign <私钥文件>`。私钥和 `authorKey` 对不上时报 `signing-key-mismatch`。打包后 `gs` 会再检查一遍签名，并打印“signed with key <钥匙 id>”。

签名的格式和 minisign 兼容，也可以用自己的工具签：包里放 `SHA512SUMS`（包里除它和签名之外每个文件一行：128 位十六进制的 SHA-512、两个空格、包内路径）和 `SHA512SUMS.minisig`（用 minisign 对 `SHA512SUMS` 的签名），清单里的 `authorKey` 是对应的公钥。

GameScrcpy 检查签名的规则：

- 文件和 `SHA512SUMS` 对不上、多了或少了文件、签名不是 `authorKey` 的钥匙签的：装不上（`signature-invalid`）。
- 装着的版本签了名，新包没签名：装不上（`signature-missing`）。
- 新包换了一把钥匙签名：装不上（`publisher-changed`）。真要换钥匙，用户得先删掉旧的扩展再装。所以请备份好私钥；丢了私钥，老用户只能删掉再装。

## 安装链接

在网页、论坛或 README 里放一个链接，用户点了就会在 GameScrcpy 里打开安装框：

```html
<a href="gamescrcpy://install?url=https%3A%2F%2Fexample.com%2Fdownloads%2Falice.lap-timer-1.2.0.gsext&sha256=9f2b5c0e4d7a8b1c3e5f6a7b8c9d0e1f2a3b4c5d6e7f8091a2b3c4d5e6f70812">在 GameScrcpy 里安装</a>
```

- `url` 是包的下载地址，必须是 `https://`，按 URL 编码写；`sha256` 是包的 SHA-256，64 位十六进制。两个参数各写一次，不能有别的参数，也不能带 `#`；整条链接最长 4096 个字符。
- GameScrcpy 下载这个包（重定向最多 8 次，而且必须一直是 https，包最大 200 MiB），算出的 SHA-256 和链接对不上就不装；对上了就打开平常的安装框，上面写着从哪里下载的、SHA-256 与链接一致，之后的检查和同意与从文件安装完全一样。
- `gamescrcpy://extension/<扩展 id>` 打开扩展页并定位到这个扩展的卡片，适合放在“设置说明”里。
- 安装包会注册 `gamescrcpy://`；便携版在设置 › 通用的“扩展链接”卡片上点一下注册。GameScrcpy 已经开着时，链接交给开着的那个。

算 SHA-256 和编码 URL 可以用 PowerShell：

```powershell
(Get-FileHash .\alice.lap-timer-1.2.0.gsext -Algorithm SHA256).Hash.ToLower()
[uri]::EscapeDataString("https://example.com/downloads/alice.lap-timer-1.2.0.gsext")
```

每次发新版本，包的 SHA-256 都会变，链接要跟着换。

## 整合包

扩展页的“导出整合包…”把打开着的扩展连同它们的设置一起导出成一个 `.gsbundle`，用户“从文件安装…”就能一次装好一组扩展。它也是一个 zip：`bundle.json`（`schemaVersion` 1、`name`、`description`、`extensions` 列出每个扩展的 `id`、`version`、是否内置、是否打开和设置的值）加上 `extensions/<扩展 id>-<版本>.gsext`，最多 64 个扩展。`gs validate <整合包>` 会逐个检查里面的包。

## 发布前检查

- `gs validate` 没有错误，最好也没有警告；有脚本的过了 `gs check`；有测试的过了 `gs ext test`（原生扩展再用 `--hosted` 跑一次）。
- `id` 用你自己的前缀，`license`、`authors`、`repository` 都写好；`description` 一两句说清扩展做什么。
- 每项权限的 `reason` 写的是用户能看懂的理由。
- 名字、说明、面板标题、引导与帮助有中文译文（`l10n` 与 `locales`）。
- 原生扩展：DLL 用 `/MT` 静态链接运行库，`gs validate` 显示它导出了 `gs_ext_main`；需要的话同时提供 arm64 的 DLL。
- 用 `gs pack --sign` 签名，保存好私钥。
- 在 GameScrcpy 里从文件装一次，打开、同意权限，走一遍主要功能。
