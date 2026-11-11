# 按键桥（deck-bridge）

简体中文 · [English](README.en.md)

让 Stream Deck 这类本机程序按下方案里绑定的键：扩展在 `127.0.0.1` 上开一个 HTTP 服务（`net.listen`），收到的请求经输入源 `deck` 送进键位（`input.source`），并给别的扩展提供两个服务（`ext.provide`）。

## 文件

| 文件 | 内容 |
|:--|:--|
| `manifest.json` | 一个应用脚本、一个主窗口页面、两个服务，权限 `network.listen` 与 `hardware` |
| `src/main.ts` | HTTP 服务、按键、页面与服务 |
| `tsconfig.json` | 给 `gs check` 和编辑器用 |

## 试一试

1. `gs validate examples\extensions\deck-bridge`，`gs check examples\extensions\deck-bridge`。
2. `gs pack examples\extensions\deck-bridge -o deck-bridge.gsext`，在扩展页“从文件安装…”，打开它并同意权限。
3. 左栏多了“按键桥”一页，写着 `Listening on http://127.0.0.1:8765`。端口可以在页面上改，改完点“Listen again”。
4. 在键位编辑器里给一个点击组件绑键：等按键时运行下面的命令，就录成了 `Ext.example.deck-bridge.deck.k1`。

   ```powershell
   Invoke-RestMethod -Method Post http://127.0.0.1:8765/tap/k1
   ```

5. 在 Stream Deck 里用一个能发 HTTP 请求的动作（插件），向 `http://127.0.0.1:8765/tap/k1` 发 POST。按键送给前台的投屏窗口所在的手机。

## 接口

| 请求 | 做什么 |
|:--|:--|
| `GET /` | 返回 `{ listening, port, held }` |
| `POST /tap/<键>?ms=80` | 按下这个键，`ms` 毫秒（10～2000，默认 80）后松开 |
| `POST /press/<键>` | 按住这个键 |
| `POST /release/<键>` | 松开这个键 |

键名是 1～32 个字母、数字、`_` 或 `-`。会按键的请求只接受 POST：网页能让浏览器不带 `Origin` 地发出 GET（例如一张图片），只收 POST 就不会被随便哪个网页按下键。

别的扩展把 `example.deck-bridge` 写进 `dependencies` 后可以调用它的服务：

```ts
const state = await ext.call("example.deck-bridge/status");
await ext.call("example.deck-bridge/tap", { key: "k1", ms: 120 });
```

服务按双方都有的权限运行，所以调用 `tap` 的扩展自己也要有 `hardware` 权限；`status` 谁都能调。

## 说明

- 用户可以在扩展详情里关掉“接受本机程序的连接”，服务随之关闭；再打开后在页面上点“Listen”。
- 逻辑停止时（关掉扩展、退出程序）按住的键全部松开。
