# 金标准测试向量（ADR-0018、ADR-0026）

本目录的 `*.json` 由 `protocol/tools/gen_vectors.py`（独立的 Python 参考编码器，只用标准库）生成，**不要手工编辑**；修改协议时先改 `SPEC.md`，再改生成器并重新运行：

```powershell
python protocol/tools/gen_vectors.py
```

生成是确定性的：重复运行不应产生任何差异。

## 双向校验规则

两侧（C++ doctest、Java JUnit）都必须读取本目录下的**全部** `*.json` 文件，对每条向量：

1. 有 `fields`：按 `fields` 编码，结果必须与 `hex` **逐字节相等**；解码 `hex`，结果必须与 `fields` **逐字段相等**，且恰好消耗全部字节。
2. 有 `error`：在 `channel` + `direction` 上下文中解码 `hex`，必须以该解码错误失败（检查顺序见 SPEC 3.3 节）。

## 文件格式

每个消息族一个文件（`handshake.json`、`input.json`、`media-config.json`、`device.json`、`maintenance.json`、`events.json`、`bulk.json`、`companion.json`、`media.json`、`malformed.json`）：

```json
{
  "format": "gamescrcpy.protocol-vectors",
  "protocolVersion": 1,
  "family": "input",
  "vectors": [
    {
      "name": "touch-frame-single-down",
      "message": "TouchFrame",
      "channel": "control",
      "direction": "toServer",
      "fields": { "videoSession": 1, "points": [ { "action": "down", "pointerId": 0, "x": 32768, "y": 32768, "pressure": 65535 } ] },
      "hex": "01010000000b0001010000800080..."
    }
  ]
}
```

## 字段取值约定

| 类型 | JSON 表示 |
|---|---|
| `u8 / u16 / u32 / i16` | 数字 |
| `u64 / i64` | **十进制字符串**（避免 JSON 数字精度丢失） |
| `bool` | `true` / `false` |
| 枚举 | SPEC 中的小写驼峰名称（如 `down`、`h264`、`virtualDisplay`、`versionMismatch`） |
| `bytes[N]`、`blob8 / blob16 / blob32` | 小写十六进制字符串 |
| `str8 / str16 / str32` | JSON 字符串 |
| 嵌套结构 / 列表 | 对象 / 数组 |
| 位标志字段 | 按 SPEC 中的位名展开为布尔值（如 `StartApp.forceStop` / `onVideoDisplay`、`ForegroundAppChanged.game`、`AppList` 条目的 `system` / `enabled` / `game`、`CursorState.visible` / `pressed`、`MediaFrame.keyFrame`） |

- `channel`：`handshake`、`control`、`bulk`、`companion`（伴侣连接，SPEC 第 11 章）、`video`、`audio`；`direction`：`toServer`、`toClient`（伴侣连接里电脑 → 伴侣为 `toServer`）。
- 消息帧向量的 `hex` 是完整的帧（6 字节帧头 + 负载）；媒体向量的 `hex` 是完整的媒体包（16 字节包头 + 负载）。
- 同名消息区分方向：客户端的 `Goodbye`（0x0402）与服务端的 `ServerGoodbye`（0x8402）。
- 媒体向量的消息名：`VideoSession`、`AudioSession`（kind 0）、`MediaConfig`（kind 1）、`MediaFrame`（kind 2）。
