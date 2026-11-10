# 手感样例（community.feel-samples）

这是 GameScrcpy 的**社区扩展**样例，不是官方扩展：它不随 GameScrcpy 一起发布，不进安装包和便携版，要用的人自己构建、打包、安装。装上之后它和其他人做的原生扩展一样，在单独的 `gs-exthost.exe` 进程里运行，并经过安装时的安全检查。

许可：Apache-2.0。作者：GameScrcpy 社区。

## 里面有什么

九个**附加**（挂在任何键位组件上，改进来的按键和摇杆数值，或改发出去的触点）：

| 名字 | 标题 | 适用的组件 | 参数 |
|---|---|---|---|
| `randomLanding` | 随机落点 | 全部 | `radius`：半径，画面短边的百分比 |
| `joystickGlide` | 轮盘滑行 | `wasd`、`padStick`、`rightWalk` | `time`：滑行时间（毫秒） |
| `joystickWander` | 轮盘拟人偏移 | `wasd`、`padStick`、`rightWalk` | `amount`：漂移幅度，画面短边的百分比 |
| `curvedSwipe` | 弯曲滑动 | `swipe` | `bend`：弯曲程度（-1～1）；`length`：滑动长度，画面短边的百分比 |
| `smoothing` | 平滑 | 全部 | `strength`：平滑强度（0～0.95） |
| `adaptiveSmoothing` | 自适应平滑（One Euro 滤波） | 全部 | `minCutoff`：最低截止频率（Hz）；`beta`：速度系数 |
| `responseCurve` | 响应曲线 | `padStick`、`padCamera`、`behavior` | `curve`：曲线（横轴推了多少，纵轴送出去多少） |
| `deadZone` | 死区 | `padStick`、`padCamera`、`behavior` | `size`：被忽略的行程比例 |
| `holdTime` | 按住时长 | 全部 | `minimum`：最短按住（毫秒）；`extra`：随机加长（毫秒） |

一个**按法**（行为，组件库"扩展"一组里的新组件种类）：

| 名字 | 标题 | 输入槽 | 参数 |
|---|---|---|---|
| `circle` | 画圈 | `hold`（按键） | `radius`：半径，画面短边的百分比；`period`：一圈用时（毫秒）；`clockwise`：顺时针 |

所有随机数都来自 GameScrcpy 传进来的种子，同样的输入和种子总得到同样的输出；所有入口都不等待，时间随调用传入。

## 构建

只依赖 GameScrcpy 仓库里的 `sdk/include/gamescrcpy_ext.h`（由 `gs api write` 生成）。需要 CMake 3.28 以上和支持 C++20 的编译器（Windows 上用 MSVC 2022，64 位）。

单独构建：

```powershell
cmake -S community/feel-samples -B build/feel-samples -DGS_SDK_INCLUDE=<GameScrcpy 仓库>/sdk/include
cmake --build build/feel-samples --config Release
```

在 GameScrcpy 仓库里构建时，`GS_SDK_INCLUDE` 可以省略（默认就是仓库里的 `sdk/include`）。主仓库的构建只在选项 `GS_BUILD_COMMUNITY` 打开时（默认打开）顺带编它，供测试把它当别人的扩展装上用。

构建结果在构建目录的 `community/feel-samples/` 下：`feel-samples.dll` 和 `manifest.json`，这个文件夹就是扩展包的内容。

## 打包

用 GameScrcpy 带的命令行工具 `gs` 把这个文件夹打成 `.gsext`：

```powershell
gs validate build/feel-samples/community/feel-samples
gs pack build/feel-samples/community/feel-samples -o feel-samples-1.0.0.gsext
```

不要加 `--official`：这是社区扩展，id 不用 `gamescrcpy.` 前缀。

## 安装与使用

1. 在 GameScrcpy 的"扩展"页点"安装扩展"，选 `feel-samples-1.0.0.gsext`（也可以把文件拖进窗口）。
2. 安装框会列出安全检查的结果：作者是否经过签名验证、Windows 安全中心的扫描、DLL 用到的系统功能。确认后点"信任并安装"。
3. 装好后默认关着，在"扩展"页把它打开。
4. 编辑键位：选中一个组件，在属性面板的"附加"一组里点"添加"，挑一个附加并就地改参数；要用"画圈"，从组件库的"扩展"一组把它拖到画面上，再给"按住"绑一个键。

某个附加或按法在 10 秒内出错 3 次，GameScrcpy 会关掉整个扩展并提示，到"扩展"页重新打开即可。

## 改成自己的扩展

复制这个文件夹，改 `manifest.json` 里的 `id`（如 `yourname.my-feel`）、名字和作者，在 `src/feel_samples.cpp` 里增删附加与按法；清单里声明的名字、输入槽、手指数和是否要每帧，要与 DLL 在 `gs_ext_entry` 里给出的一致，否则扩展页会提示"声明与 DLL 不符"。参数只在清单里声明，DLL 在 `start` 时收到一个 JSON 对象，里面有每个参数的值，以及 `"@position"`（组件位置）和 `"@unit"`（画面短边一个单位在横竖方向上的比例）。
