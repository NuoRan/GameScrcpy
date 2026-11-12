# 键位清单（key-list）

简体中文 · [English](README.en.md)

一对转换器：把一种自造的纯文本键位格式（`.keys`）导入成方案，也能把方案导出成这种格式。它演示 `contributes.converters` 与脚本导出的 `convert` 函数，不要任何权限。

## 格式

```text
# Arena basics
J 0.82 0.71 Attack
Space 0.94 0.80 Dodge
1 0.40 0.92 Potion
```

- 第一行以 `#` 开头时是方案名；之后以 `#` 开头的行是注释。
- 每行一个键：`键 x y 名字`。键是一个字母、一个数字，或 `Space`、`F1`、`ShiftLeft` 这样的键名；`x`、`y` 是画面上的位置，0～1 的比例；名字可以省掉。
- 导入时每行变成一个单击控件；导出时只写只绑一个键、有位置的控件，其余的在最后一行注释里写数量。方案不是“拉伸”缩放时，位置按 16:9 的画面估算。

## 文件

| 文件 | 内容 |
|:--|:--|
| `manifest.json` | 两个转换器：`keys` → `profile`（认 `.keys` 文件）与 `profile` → `keys`，共用一个脚本 |
| `src/main.ts` | `convert(input)`：按 `input.to` 分开导入与导出 |
| `samples/arena.keys` | 一份可以导入的键位清单 |
| `tsconfig.json` | 给 `gs check` 和编辑器用 |

## 试一试

1. `gs validate examples\extensions\key-list`，`gs check examples\extensions\key-list`。
2. `gs pack examples\extensions\key-list -o key-list.gsext`，在扩展页“从文件安装…”，打开它。
3. 方案页点“导入”，文件类型里多了“键位清单 (*.keys)”，选 `samples\arena.keys`：多出一个叫“Arena basics”的方案，五个单击控件。把 `.keys` 文件拖到投屏窗口上也能导入。
4. 方案卡片的菜单里多了“导出为 键位清单…”，选一个方案导出，得到同样格式的文本。

## 怎么做的

- 两个转换器的 `script` 都是 `src/main.ts`。GameScrcpy 每转换一个文件就在逻辑的沙箱里载入它、调一次 `convert`，没有手机。
- 导入：`input.text` 是文件按 UTF-8 读出的文字，`profileFrom()` 一行一行读，返回 `{ name, json }`；`json` 是 `gamescrcpy.profile` 第 3 版的方案，`name` 给新方案起名。GameScrcpy 像检查别的导入文件一样检查它。
- 导出：`input.json()` 是方案的 JSON，`textFrom()` 写出文字，直接返回 `{ text }`。
- 读不懂的行抛出错误，信息就是用户看到的提示。
