# GameScrcpy 脚本 API 参考

**[English](SCRIPT_API_EN.md)** | 中文

> **版本**: 1.2.0
> **运行环境**: QJSEngine (ES6)
> **内置对象**: `mapi`

所有 API 通过全局对象 `mapi` 调用。脚本在独立线程中运行，每个按键绑定拥有独立的沙箱。

---

## 目录

- [触摸操作](#触摸操作)
  - [click](#click) — 单击
  - [holdpress](#holdpress) — 长按
  - [release](#release) — 释放触摸
  - [releaseAll](#releaseall) — 释放所有触摸
  - [slide](#slide) — 滑动
  - [pinch](#pinch) — 双指缩放
- [按键操作](#按键操作)
  - [key](#key) — 模拟按键
- [视角控制](#视角控制)
  - [shotmode](#shotmode) — 切换光标/游戏模式
  - [resetview](#resetview) — 重置视角
  - [resetwheel](#resetwheel) — 重置轮盘
  - [setRadialParam](#setradialparam) — 设置轮盘偏移系数
  - [setKeyUIPos](#setkeyuipos) — 设置按键 UI 位置
- [状态查询](#状态查询)
  - [isPress](#ispress) — 检查按下状态
  - [getmousepos](#getmousepos) — 获取鼠标位置
  - [getkeypos](#getkeypos) — 获取按键位置
  - [getKeyState](#getkeystate) — 获取按键状态
  - [getbuttonpos](#getbuttonpos) — 获取虚拟按钮位置
- [预定义滑动](#预定义滑动)
  - [swipeById](#swipebyid) — 按编号执行滑动
- [图像识别](#图像识别)
  - [findImage](#findimage) — 区域找图
  - [findImageByRegion](#findimagebyregion) — 按选区找图
- [流程控制](#流程控制)
  - [sleep](#sleep) — 延时
  - [isInterrupted](#isinterrupted) — 检查中断
  - [stop](#stop) — 停止脚本
- [消息与日志](#消息与日志)
  - [toast](#toast) — 弹窗提示
  - [log](#log) — 日志输出
- [全局状态](#全局状态)
  - [setGlobal](#setglobal) — 设置全局变量
  - [getGlobal](#getglobal) — 获取全局变量
- [模块加载](#模块加载)
  - [loadModule](#loadmodule) — 导入模块

---

## 触摸操作

### click

单击指定位置。

```js
mapi.click(x, y)
```

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `x` | `double` | `-1` | 归一化 X 坐标 (0.0 ~ 1.0) |
| `y` | `double` | `-1` | 归一化 Y 坐标 (0.0 ~ 1.0) |

- 省略参数时使用按键锚点位置
- 仅在 `isPress() == true` 时执行
- 内部发送 DOWN + UP 事件

```js
// 点击屏幕中心
mapi.click(0.5, 0.5);

// 使用按键锚点位置
mapi.click();
```

---

### holdpress

长按（按下阶段）。

```js
mapi.holdpress(x, y)
```

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `x` | `double` | `-1` | 归一化 X 坐标 |
| `y` | `double` | `-1` | 归一化 Y 坐标 |

- 按下时发送 DOWN 事件并记录触摸序列
- 松开时自动释放对应触摸点
- 受 `maxTouchPoints`（默认 10）限制

```js
// 按下时长按，松开时自动释放
mapi.holdpress(0.3, 0.7);
```

---

### release

释放当前按键的触摸点。

```js
mapi.release()
```

发送一个 UP 事件。通常在松开按键时调用。

---

### releaseAll

释放当前按键关联的所有触摸点。

```js
mapi.releaseAll()
```

遍历当前按键绑定的所有触摸序列，逐一发送 UP 事件。适用于多点触控时批量释放。

---

### slide

模拟滑动操作。

```js
mapi.slide(sx, sy, ex, ey, delayMs, num)
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `sx` | `double` | 起点 X |
| `sy` | `double` | 起点 Y |
| `ex` | `double` | 终点 X |
| `ey` | `double` | 终点 Y |
| `delayMs` | `int` | 滑动总时长（毫秒） |
| `num` | `int` | 步数（最少 10） |

- 使用平滑曲线路径，带随机弯曲（防检测）
- 受 `slideCurve` 配置项影响

```js
// 从左往右滑动，200ms，10步
mapi.slide(0.2, 0.5, 0.8, 0.5, 200, 10);
```

---

### pinch

双指缩放操作。

```js
mapi.pinch(centerX, centerY, scale, durationMs, steps)
```

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `centerX` | `double` | — | 中心点 X |
| `centerY` | `double` | — | 中心点 Y |
| `scale` | `double` | — | 缩放比例（>1 放大，<1 缩小） |
| `durationMs` | `int` | `300` | 持续时间（毫秒） |
| `steps` | `int` | `10` | 动画步数 |

```js
// 在屏幕中心放大 2 倍
mapi.pinch(0.5, 0.5, 2.0, 300, 10);

// 缩小到 0.5 倍
mapi.pinch(0.5, 0.5, 0.5, 300, 10);
```

---

## 按键操作

### key

模拟按键映射中的按键。

```js
mapi.key(keyName, durationMs)
```

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `keyName` | `string` | — | 按键名称 |
| `durationMs` | `int` | `50` | 按键持续时间（毫秒） |

会触发对应按键的宏脚本。

**支持的按键名称**：`A`-`Z`、`0`-`9`、`SPACE`、`ENTER`/`RETURN`、`ESC`/`ESCAPE`、`TAB`、`BACKSPACE`、`SHIFT`、`CTRL`/`CONTROL`、`ALT`、`UP`/`DOWN`/`LEFT`/`RIGHT`、`F1`-`F12`，以及符号键 `` ` ``、`-`、`=`、`[`、`]`、`\`、`;`、`'`、`,`、`.`、`/`

```js
// 按下 W 键 50ms
mapi.key("W", 50);

// 按下 Tab 键 100ms
mapi.key("TAB", 100);
```

---

## 视角控制

### shotmode

切换光标模式和游戏模式。

```js
mapi.shotmode(gameMode)
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `gameMode` | `bool` | `true` = 游戏模式（隐藏光标），`false` = 光标模式 |

```js
mapi.shotmode(false);  // 显示光标
mapi.shotmode(true);   // 游戏模式
```

---

### resetview

重置鼠标视角控制。

```js
mapi.resetview()
```

适用于 FPS 游戏视角归位。

---

### resetwheel

重置轮盘状态。

```js
mapi.resetwheel()
```

用于场景切换后轮盘重同步（如跑步时按 F 进入车辆）。

---

### setRadialParam

临时设置轮盘四方向偏移系数。

```js
mapi.setRadialParam(up, down, left, right)
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `up` | `double` | 上方向系数 |
| `down` | `double` | 下方向系数 |
| `left` | `double` | 左方向系数 |
| `right` | `double` | 右方向系数 |

- 实际偏移 = 原始值 × 系数
- 默认值 `(1, 1, 1, 1)` 表示不变
- 脚本结束时自动恢复为默认值

```js
// 上方向偏移加倍
mapi.setRadialParam(2, 1, 1, 1);
```

---

### setKeyUIPos

动态更新按键的 UI 显示位置。

```js
mapi.setKeyUIPos(keyName, x, y, xoffset, yoffset)
```

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `keyName` | `string` | — | 按键名称 |
| `x` | `double` | — | 基础 X 坐标 |
| `y` | `double` | — | 基础 Y 坐标 |
| `xoffset` | `double` | `0` | X 偏移量 |
| `yoffset` | `double` | `0` | Y 偏移量 |

最终位置 = `(x + xoffset, y + yoffset)`。用于多功能按键的位置指示。

```js
// 将 J 键 UI 移动到屏幕中心
mapi.setKeyUIPos("J", 0.5, 0.5);

// 带偏移量
mapi.setKeyUIPos("J", 0.5, 0.5, 0.02, -0.03);
```

---

## 状态查询

### isPress

检查当前触发状态。

```js
mapi.isPress()
```

**返回值**: `bool` — `true` = 按下，`false` = 松开

```js
if (mapi.isPress()) {
    mapi.holdpress(0.5, 0.5);
} else {
    mapi.release();
}
```

---

### getmousepos

获取当前鼠标位置。

```js
mapi.getmousepos()
```

**返回值**: `{x: double, y: double}` — 归一化坐标，精度 4 位小数

```js
var pos = mapi.getmousepos();
mapi.toast("x=" + pos.x + ", y=" + pos.y);
```

---

### getkeypos

获取指定按键映射的位置。

```js
mapi.getkeypos(keyName)
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `keyName` | `string` | 按键显示名称（如 `LMB`、`Tab`、`=`） |

**返回值**: `{x: double, y: double, valid: bool}`

```js
var pos = mapi.getkeypos("LMB");
if (pos.valid) {
    mapi.click(pos.x, pos.y);
}
```

---

### getKeyState

获取指定按键的按压状态。

```js
mapi.getKeyState(keyName)
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `keyName` | `string` | 按键名称 |

**返回值**: `int` — `0` = 未按下，`1` = 按下中

```js
if (mapi.getKeyState("W")) {
    mapi.toast("W 键按下中");
}
```

---

### getbuttonpos

获取预定义虚拟按钮的位置。虚拟按钮通过选区编辑器的「新建按钮」功能创建，保存在 `keymap/buttons.json` 中。

```js
mapi.getbuttonpos(buttonId)
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `buttonId` | `int` | 按钮编号（在选区编辑器中创建时自动分配） |

**返回值**: `{x: double, y: double, valid: bool, name: string}`

- `x`, `y`: 按钮的归一化坐标
- `valid`: 是否找到该编号的按钮
- `name`: 按钮名称

```js
// 获取编号为 1 的虚拟按钮位置
var btn = mapi.getbuttonpos(1);
if (btn.valid) {
    mapi.click(btn.x, btn.y);
    mapi.toast("点击了: " + btn.name);
}
```

---

## 预定义滑动

### swipeById

按预定义编号执行滑动路径。滑动路径通过选区编辑器的「新建滑动」功能创建（两次点击设置起点→终点），保存在 `keymap/swipes.json` 中。

```js
mapi.swipeById(swipeId, durationMs, steps)
```

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `swipeId` | `int` | — | 滑动路径编号 |
| `durationMs` | `int` | `200` | 滑动持续时间 (ms) |
| `steps` | `int` | `10` | 滑动步数 |

内部查找对应编号的滑动路径，然后委托给 `slide()` 执行，带有拟人化曲线路径。

```js
// 执行编号为 1 的预定义滑动
mapi.swipeById(1, 200, 10);

// 使用默认参数
mapi.swipeById(2);
```

---

## 图像识别

> 需要编译时启用 `ENABLE_IMAGE_MATCHING`，依赖 OpenCV。

### findImage

在指定归一化区域内搜索模板图片。

```js
mapi.findImage(imageName, x1, y1, x2, y2, threshold)
```

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `imageName` | `string` | — | 模板图片名称（不含扩展名） |
| `x1` | `double` | `0` | 搜索区域左上 X |
| `y1` | `double` | `0` | 搜索区域左上 Y |
| `x2` | `double` | `1` | 搜索区域右下 X |
| `y2` | `double` | `1` | 搜索区域右下 Y |
| `threshold` | `double` | `0.8` | 匹配置信度阈值 (0.0 ~ 1.0) |

**返回值**: `{found: bool, x: double, y: double, confidence: double}`

- `found`: 是否找到匹配
- `x`, `y`: 匹配位置的归一化中心坐标
- `confidence`: 匹配置信度

模板图片存放在 `keymap/images/` 目录下。

```js
var result = mapi.findImage("确认按钮", 0, 0, 1, 1, 0.8);
if (result.found) {
    mapi.click(result.x, result.y);
    mapi.toast("置信度: " + result.confidence.toFixed(2));
}
```

---

### findImageByRegion

使用预定义选区编号搜索模板图片。

```js
mapi.findImageByRegion(imageName, regionId, threshold)
```

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `imageName` | `string` | — | 模板图片名称 |
| `regionId` | `int` | — | 选区编号 |
| `threshold` | `double` | `0.8` | 匹配阈值 |

**返回值**: 同 `findImage`

选区通过「获取工具」中的自定义选区管理器创建，存储在 `keymap/regions.json`。

```js
// 在选区 #1 中搜索
var result = mapi.findImageByRegion("敌人图标", 1, 0.8);
if (result.found) {
    mapi.click(result.x, result.y);
}
```

---

## 流程控制

### sleep

暂停脚本执行。

```js
mapi.sleep(ms)
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `ms` | `int` | 暂停毫秒数 |

- 每 50ms 检查一次中断标志，可被 `stop()` 中断
- 同时喂看门狗防止超时

```js
mapi.sleep(100);  // 暂停 100ms
```

---

### isInterrupted

检查脚本是否被请求停止。

```js
mapi.isInterrupted()
```

**返回值**: `bool` — `true` = 已被中断

用于长循环中提前退出：

```js
while (true) {
    if (mapi.isInterrupted()) return;
    // ... 循环逻辑 ...
    mapi.sleep(100);
}
```

---

### stop

主动停止当前脚本执行。

```js
mapi.stop()
```

设置中断标志，后续 `isInterrupted()` 将返回 `true`，`sleep()` 将立即返回。

---

## 消息与日志

### toast

显示浮动提示信息。

```js
mapi.toast(msg, durationMs)
```

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `msg` | `string` | — | 消息内容 |
| `durationMs` | `int` | `3000` | 显示时长（毫秒，最小 1ms） |

同一按键的消息会更新而非新增；不同按键的消息堆叠显示。

```js
mapi.toast("操作完成", 3000);
```

---

### log

输出日志到控制台。

```js
mapi.log(msg)
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `msg` | `string` | 日志内容 |

输出格式: `[Sandbox Script] msg`

```js
mapi.log("调试信息");
```

---

## 全局状态

### setGlobal

设置跨脚本共享的全局变量（线程安全）。

```js
mapi.setGlobal(key, value)
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `key` | `string` | 键名 |
| `value` | `any` | 值 |

- 仅在 `isPress() == true` 时生效
- 所有沙箱可访问

```js
mapi.setGlobal("模式", "攻击");
```

---

### getGlobal

获取全局共享变量。

```js
mapi.getGlobal(key)
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `key` | `string` | 键名 |

**返回值**: 存储的值，不存在则返回 `undefined`

```js
var mode = mapi.getGlobal("模式");
if (mode === "攻击") {
    // ...
}
```

---

## 模块加载

### loadModule

从脚本目录加载 JavaScript 模块。

```js
mapi.loadModule(modulePath)
```

| 参数 | 类型 | 说明 |
|------|------|------|
| `modulePath` | `string` | 模块文件路径（相对于 `keymap/scripts/`） |

- 自动补 `.js` 后缀
- 有模块缓存，同一模块只加载一次

**函数式导出**:

```js
// utils.js
function helper() { return 42; }

// 主脚本
var m = mapi.loadModule("utils.js");
m.helper();  // 42
```

**对象式导出**:

```js
// mymodule.js
function create() {
    return {
        doSomething: function() { /* ... */ }
    };
}

// 主脚本
var m = mapi.loadModule("mymodule.js");
var obj = new m.create();
obj.doSomething();
```

---

## 坐标系说明

GameScrcpy 统一使用 **归一化坐标**：

- `x`: 0.0（左边缘）→ 1.0（右边缘）
- `y`: 0.0（上边缘）→ 1.0（下边缘）
- 与设备分辨率无关

可通过选区编辑器的「获取位置」功能在视频帧上点击获取坐标。

## 防检测特性

脚本执行包含内置的防检测机制：

| 配置项 | 范围 | 说明 |
|--------|------|------|
| `randomOffset` | 0-100 | 触摸坐标随机偏移量 |
| `slideCurve` | 0-100 | 滑动路径弯曲程度 |
| `steerWheelSmooth` | 0-100 | 轮盘操作平滑度 |
| `steerWheelCurve` | 0-100 | 轮盘路径弯曲度 |

这些参数在 `config.ini` 或设置界面中配置。

## 文件目录结构

```
keymap/
├── images/         # 模板图片 (.png/.jpg/.bmp)
├── scripts/        # JS 模块文件
├── regions.json    # 自定义选区配置
├── buttons.json    # 虚拟按钮配置 (getbuttonpos 用)
├── swipes.json     # 滑动路径配置 (swipeById 用)
└── *.json          # 按键映射配置
```

## 选区编辑器

脚本编辑器中的「获取工具」按钮可打开选区编辑器，提供以下功能：

| 功能 | 说明 |
|------|------|
| **获取位置** | 点击视频帧获取归一化坐标，可复制或生成 `mapi.click()`/`mapi.holdpress()` 代码 |
| **新建按钮** | 点击放置虚拟按钮标记，保存到 `buttons.json`，配合 `mapi.getbuttonpos()` 使用 |
| **新建滑动** | 两次点击设置起点→终点，保存到 `swipes.json`，配合 `mapi.swipeById()` 使用 |
| **新建图片** | 框选区域截取模板图片，保存到 `keymap/images/`，配合 `mapi.findImage()` 使用 |
| **新建选区** | 框选矩形搜索区域，保存到 `regions.json`，配合 `mapi.findImageByRegion()` 使用 |

所有元素支持拖拽编辑、右键菜单重命名/删除、右键生成代码片段插入到脚本编辑器。
