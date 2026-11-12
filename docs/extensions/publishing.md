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
- 清单写了 `updates.index` 的扩展，GameScrcpy 能提示用户有新版本（见下文[更新列表](#更新列表)）；没写的，在你的发布页、仓库或网页上告诉用户，最方便的是给一个安装链接（见下文）。
- 不要改 `id`：换了 id 就是另一个扩展，设置、权限和签名都不跟过去。

## 签名

签了名的包，这台电脑第一次见到这个公钥时，安装框和卡片写“已用公钥 … 签名，这台电脑以前没见过这个公钥”；以后再装用同一个公钥签的版本，才写“已验证的发布者 · 公钥 …”。两处都只写公钥，不写清单里作者自称的名字。之后的更新也必须由同一把钥匙签：别人改过的包、冒充你发的更新都装不上。没签名的原生扩展在安装框里有醒目的提醒，受限的原生扩展没签名时只是一条提示。

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
- 第一次安装时就信任包里的公钥，以后都按它校验。请把你的公钥公布在别处（例如仓库的 README），让用户第一次装时能核对。
- 可以装回同一个 id 的旧版本（用来回退），签名规则照样适用。

## 安装链接

在网页、论坛或 README 里放一个链接，用户点了就会在 GameScrcpy 里打开安装框：

```html
<a href="gamescrcpy://install?url=https%3A%2F%2Fexample.com%2Fdownloads%2Falice.lap-timer-1.2.0.gsext&sha256=9f2b5c0e4d7a8b1c3e5f6a7b8c9d0e1f2a3b4c5d6e7f8091a2b3c4d5e6f70812">在 GameScrcpy 里安装</a>
```

- `url` 是包的下载地址，必须是带主机名、不带用户名的 `https://`，按 URL 编码写，解码后最长 2048 个字符；`sha256` 是包的 SHA-256，64 位十六进制。两个参数各写一次，不能有别的参数，也不能带 `#`；整条链接只能是可打印的 ASCII 字符，最长 4096 个。
- GameScrcpy 下载这个包（重定向最多 8 次，而且必须一直是 https，包最大 200 MiB），算出的 SHA-256 和链接对不上就不装；对上了就打开平常的安装框，上面写着从哪里下载的、SHA-256 与链接一致，之后的检查和同意与从文件安装完全一样。安装框里的来源是下载结束时（重定向之后）的主机，国际化域名显示成 ASCII 形式（`xn--…`），免得形近的字母冒充。
- `gamescrcpy://extension/<扩展 id>` 打开扩展页并定位到这个扩展的卡片，适合放在“设置说明”里。
- 安装包会注册 `gamescrcpy://`；便携版在设置 › 通用的“扩展链接”卡片上点一下注册。GameScrcpy 已经开着时，链接交给开着的那个。
- 链接必须是命令行里唯一的参数：`GameScrcpy.exe "gamescrcpy://…"` 后面再带别的参数时，GameScrcpy 直接以失败退出、什么也不做，这样网页没法借链接给程序加选项。浏览器和系统打开链接时都只传这一个参数。
- 一次只装一个：上一个链接的包还在等用户装时，新的链接被拒绝并提示先装完上一个；安装框开着时下载好的包也会被放掉，提示用户关掉安装框后再打开链接。

算 SHA-256 和编码 URL 可以用 PowerShell：

```powershell
(Get-FileHash .\alice.lap-timer-1.2.0.gsext -Algorithm SHA256).Hash.ToLower()
[uri]::EscapeDataString("https://example.com/downloads/alice.lap-timer-1.2.0.gsext")
```

每次发新版本，包的 SHA-256 都会变，链接要跟着换。

## 更新列表

清单写了 `updates.index`（见[清单参考](manifest.md#updates)）的扩展，GameScrcpy 到这个地址取一份更新列表，看有没有新版本。一份列表可以给你的好几个扩展用：

```json
{
  "format": "gamescrcpy.extension-index",
  "version": 1,
  "extensions": [
    {
      "id": "alice.lap-timer",
      "versions": [
        {
          "version": "1.3.0",
          "url": "https://example.com/downloads/alice.lap-timer-1.3.0.gsext",
          "sha256": "9f2b5c0e4d7a8b1c3e5f6a7b8c9d0e1f2a3b4c5d6e7f8091a2b3c4d5e6f70812",
          "size": 48213,
          "publisher": "RWQ…",
          "requires": { "script": "^1.1", "panel": "^1.0" },
          "released": "2026-12-01",
          "notes": "Laps can be named."
        }
      ]
    }
  ]
}
```

| 字段 | 规则 |
|:--|:--|
| `format`、`version` | 固定为 `gamescrcpy.extension-index` 与 `1` |
| `extensions` | 最多 256 个扩展，每个 `id` 只列一次 |
| `versions` | 每个扩展 1～128 个版本，同一版本只列一次 |
| `url` | 包的下载地址，规则同 `updates.index`：https、有主机名、不带用户名 |
| `sha256` | 包的 SHA-256，64 位十六进制 |
| `size` | 包的字节数，1 字节～200 MiB |
| `publisher` | 可选，签这个包的 minisign 公钥（同清单的 `authorKey`） |
| `requires` | 可选，写法同清单的 `requires`（最多 32 项）；有这台 GameScrcpy 不提供的扩展点或版本时，不提示这个版本 |
| `released` | 可选，发布日期，写成 `YYYY-MM-DD` |
| `notes` | 可选，更新说明，最多 4096 字节，显示在扩展详情里 |

- 列表最大 1 MiB、最多嵌套 8 层，整份严格读取：多余的字段、重复的字段、写错的值都让整份列表作废，扩展详情里写出问题和位置（如 `$.extensions[0].versions[1].sha256: not 64 hexadecimal digits`）。
- 提示哪个版本：比装着的版本和用户跳过的版本都新、能在这台电脑上运行的最高版本；预发布版（如 `1.3.0-beta.1`）只提示给装着预发布版的用户。
- 什么时候取：用户在扩展页点“检查更新”时；设置 › 通用的“检查扩展更新”打开时（默认关），GameScrcpy 启动时每天最多取一次。只走 https，重定向只跟到 https、最多 8 次，用系统代理，不带 Cookie、不用缓存，30 秒没有数据算失败；请求里不带扩展的 id。失败不打扰用户：日志一行，扩展详情里写原因，之前的提示保留；列表里没有这个扩展时提示去掉。
- 下载与安装：用户点“更新”才下载（同时只下一个，能看到进度、能取消）。包要和列表写的大小、SHA-256 对得上，包里必须是列表写的 id 与版本、能在这台电脑上运行；列表写了 `publisher` 时包必须用这个公钥签名；装着的版本签了名时，更新必须用同一个公钥签名，换了公钥一律装不上（要接受新公钥，用户得先删掉旧的扩展再装）。装着的版本没签名时，签了名的更新照常能装，之后就按它的公钥校验。都对上了才打开平常的安装框，按钮是“更新”，上面列出“与 X 版相比的变化”（增减的权限与依赖、接口版本）；要的比以前多时提醒装上后会保持关闭，等用户重新打开并同意。从不自动安装。
- 发布新版本的做法：打包、签名，把包放到下载地址，再在列表的 `versions` 里加上这一版（`size` 与 `sha256` 用最终的包算），最后上传列表。先传包、后传列表，用户就不会看到下载不了的版本。

## 整合包

扩展页的“导出整合包…”把打开着的扩展连同它们的设置一起导出成一个 `.gsbundle`，用户“从文件安装…”就能一次装好一组扩展。它也是一个 zip：`bundle.json`（`schemaVersion` 1、`name`、`description`、`extensions` 列出每个扩展的 `id`、`version`、是否内置、是否打开和设置的值）加上 `extensions/<扩展 id>-<版本>.gsext`，最多 64 个扩展。`gs validate <整合包>` 会逐个检查里面的包。

## 发布前检查

- `gs validate` 没有错误，最好也没有警告；有脚本的过了 `gs check`；有测试的过了 `gs ext test`（原生扩展再用 `--hosted` 跑一次）。
- `requires` 写成你照着写的那个版本的扩展点版本，用到新东西就写新的次版本（原生扩展写 `"native": "^2.0"`）。
- `id` 用你自己的前缀，`license`、`authors`、`repository` 都写好；`description` 一两句说清扩展做什么。
- 每项权限的 `reason` 写的是用户能看懂的理由。
- 名字、说明、面板标题、引导与帮助有中文译文（`l10n` 与 `locales`）。
- 原生扩展：DLL 用 `/MT` 静态链接运行库，`gs validate` 显示它导出了 `gs_ext_main`；需要的话同时提供 arm64 的 DLL。用不到文件、网络和别的程序时，考虑写 `"sandbox": "restricted"`，用户装起来更放心。
- 用 `gs pack --sign` 签名，保存好私钥，把公钥公布在仓库或网页上。
- 写了 `updates.index` 的：新包传上去之后，更新列表里加上这一版。
- 在 GameScrcpy 里从文件装一次，打开、同意权限，走一遍主要功能。
