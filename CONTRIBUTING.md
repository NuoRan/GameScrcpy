# 参与 GameScrcpy 开发

简体中文 · [English](CONTRIBUTING.en.md)

感谢你愿意参与！这份指南说明怎么报告问题、怎么搭开发环境、怎么构建与测试，以及提交代码前要注意的约定。

## 报告问题与提建议

- 先在 [Issues](https://github.com/NuoRan/GameScrcpy/issues) 里搜一下有没有人提过。
- 报告问题请用"问题反馈"模板，写清版本、Windows 版本、手机型号与 Android 版本、连接方式和复现步骤，并附上程序里"设置 > 关于 > 导出诊断包"生成的文件。
- 安全问题不要公开发 Issue，按 [SECURITY.md](SECURITY.md) 私下报告。

## 提交改动的流程

1. Fork 仓库，从 `main` 拉一个分支。
2. 改动尽量小而完整：一个合并请求只做一件事，附带相应的测试。
3. 本地构建并跑相关测试（见下文），QML 静态检查保持 0 个警告。
4. 提交说明用英文，第一行概括做了什么（不超过 72 个字符），需要时空一行再写原因。
5. 发起合并请求，按模板填写改了什么、怎么验证的。CI 通过后会有人审阅。

较大的改动（新功能、改协议、改方案格式）请先开 Issue 讨论，避免白做。

## 仓库结构

| 路径 | 内容 |
|:--|:--|
| `src/` | Windows 端：C++20 + Qt 6 / Qt Quick 的程序本体，以及命令行工具 `gs`（`src/cli`）；模块划分见 [docs/architecture.md](docs/architecture.md) |
| `android/` | 手机端：`protocol`（协议）、`server`（服务端，纯 Java）、`companion`（伴侣应用，Kotlin） |
| `protocol/` | 协议规范 `SPEC.md`、ESP32 协议，以及两端共用的金标准测试向量 |
| `sdk/` | 由 `gs api write` 生成的脚本接口类型声明与文档，以及原生扩展的 C 头文件 |
| `firmware/esp32/` | ESP32 触控盒固件（ESP-IDF）与编好的镜像 |
| `tests/` | 单元测试、集成测试、界面测试与测试数据 |
| `examples/` | 示例组件、任务与扩展 |
| `community/` | 社区扩展样例（不随程序发布） |
| `tools/` | 开发与发布脚本：开发环境、打包、源码规则检查、生成器等 |
| `packaging/` | 安装包脚本与随包 adb 的固定版本 |
| `third_party/` | 随仓库提供的第三方依赖，清单见 [third_party/README.md](third_party/README.md) |

## 开发环境

| 工具 | 版本 |
|:--|:--|
| 操作系统 | Windows 10 / 11 x64 |
| Visual Studio 2022 或更新（MSVC） | 安装"使用 C++ 的桌面开发"工作负载 |
| Qt | 6.10，msvc2022_64 套件（含 Qt Quick、Qt Quick Controls 2、Qt Shader Tools、Qt Test） |
| CMake | 3.28 或更新，生成器为 Ninja |
| Python | 3.10 或更新 |
| JDK | 17 |
| Android SDK | platforms 35 / 36，build-tools 35 / 36 |

所有依赖都放在 `third_party/` 里，配置阶段不联网，请不要引入 FetchContent 之类的下载。FFmpeg、SDL3 与 libusb 是从固定源码编译好的产物，平时不需要重新编译；重建步骤见 [third_party/README.md](third_party/README.md)。

## 构建 Windows 端

先把 `GS_QT_DIR` 设为你的 Qt 套件位置，再导入开发环境（每个新终端执行一次）：

```powershell
$env:GS_QT_DIR = 'C:\Qt\6.10.1\msvc2022_64'
. .\tools\dev-env.ps1
cmake --workflow --preset msvc-debug
```

`tools/dev-env.ps1` 会导入 MSVC x64 环境、把 Qt 加进 PATH，找到 JDK 17 时设置 `JAVA_HOME`。`cmake --workflow` 依次配置、构建并运行测试。可用的预设：

| 预设 | 用途 |
|:--|:--|
| `core-only` | 只构建和测试不依赖 Qt 的核心库 |
| `msvc-debug` | 完整的 Debug 构建 |
| `msvc-release` | RelWithDebInfo，开启链接时优化 |
| `msvc-asan` | Debug，开启 AddressSanitizer |
| `ci` | 与持续集成相同：Release（不开链接时优化），警告视为错误，跳过要显卡、桌面或真机的测试 |

产物在 `build/<预设>/bin`，包括 `GameScrcpy.exe`、`gs.exe` 和各个测试程序。

## 构建 Android 端

先把 `ANDROID_HOME` 设为 Android SDK 的位置，然后：

```powershell
. .\tools\dev-env.ps1
cd android
.\gradlew.bat build
```

`gradlew build` 编译 protocol、server 和 companion 三个模块，运行 JUnit 测试和 lint（lint 警告视为错误）。服务端必须保持纯 Java，不引入 Kotlin。依赖版本统一写在 `android/gradle/libs.versions.toml`。

在真机上投屏还需要服务端 APK：运行 `.\gradlew.bat :server:assembleRelease`，再构建一次 Windows 端，它会以 `gs-server.apk` 的名字放到 `GameScrcpy.exe` 旁边。

## 测试

- 每个模块有一个 doctest 测试程序（`gs_base_tests`、`gs_protocol_tests`、`gs_input_tests`、`gs_session_tests`、`gs_media_tests`、`gs_script_tests`、`gs_blueprint_tests`、`gs_app_tests` 等），界面部分用 Qt Quick Test（`gs_controls_tests`、`gs_blueprint_ui_tests`、`gs_tour_tests`），都经 CTest 运行：`ctest --preset msvc-debug`，只跑某几项用 `-R <正则>`。
- QML 静态检查必须是 0 个警告：`cmake --build --preset msvc-debug --target all_qmllint`。
- 协议测试读取 `protocol/vectors/` 里的全部金标准向量，C++ 与 Java 两边都必须逐字节通过。
- `gs_app_smoke`、`gs_video_render_*` 等少数测试会打开窗口、需要显卡；需要手机的测试（`gs_adb_device_tests`、`gs_session_device_tests`）只在设置了 `GS_ADB_TEST_SERIAL`（设备序列号）时运行，否则显示为跳过。
- 投屏窗口不接手机也能看：`GameScrcpy.exe --demo-device tests\data\video\emulator_h265.media` 会多出一台循环播放录制画面的演示设备。
- 测试名里的 `ADR-NNNN` 与 `XXX-NNN` 是项目设计记录与验收条目的编号。

## 修改协议

1. 修改 `protocol/SPEC.md`。
2. 修改 `protocol/tools/gen_vectors.py`，运行 `python protocol/tools/gen_vectors.py` 重新生成向量。
3. 让 C++（`src/protocol`）与 Java（`android/protocol`）两边的测试都通过。

不要手工修改生成的向量文件。

## 修改脚本接口

修改 `src/script/src/Api.cpp` 里的接口表之后，在仓库根目录用刚构建的 `gs.exe` 运行 `gs api write sdk`，重新生成 `sdk/`。CTest 里的 `gs_script_api_current` 会检查 `sdk/` 与程序是否一致。

## 界面文字与翻译

- 界面文字在代码里写英文（`tr()` / `qsTr()`），中文只放在 `src/app/i18n/gamescrcpy_zh_CN.ts` 里。
- 加了或改了界面文字后运行 `cmake --build --preset msvc-debug --target update_translations` 更新译文文件，再用 Qt Linguist 或文本编辑器补上中文。每一条都必须翻译完，`gs_source_rules` 会检查。
- 程序内帮助在 `src/app/help/`：`index.json` 写明文章的顺序、名称、图标和中英文标题，`en/` 与 `zh_CN/` 下各放一份 Markdown，新文章要加进 `src/app/CMakeLists.txt` 的资源列表。`gs_app_tests` 会检查两种语言是否齐全、`help:` 链接是否都指向存在的文章。

## 源码规则

`python tools/check_source_rules.py`（也是 CTest 里的 `gs_source_rules`）会检查：

- 产品代码不运行、不解析 `dumpsys`、`wm` 这类面向人的命令输出（R5）。
- 不用固定延时来同步：不 sleep、不用带延时的单发定时器、不用 `postDelayed`（R9）。延时本身就是功能时，在同一行写上 `source-rule-allow R9: <理由>`。
- 不用 `CopyFromScreen` 截屏。
- 面向用户的文字都有译文，源文字是英文。
- `src/ui/controls` 里的控件只从主题取颜色、尺寸和时长。
- 每个 QML `Text` 都写明 `textFormat`。

## 代码约定

- C++20，格式遵循 `.clang-format` 与 `.editorconfig`；标识符与提交说明用英文。
- 让代码自己说清楚：本项目的代码里基本不写注释，文件开头介绍这个文件的说明除外；需要解释的写进合并请求或文档。上面的 `source-rule-allow` 标记是唯一要求写在行内的说明。
- 界面的颜色、字号、间距、圆角与动效都取自主题模块 `src/ui/theme`，不要写裸数值。
- 新功能要有测试；修缺陷时先写一个会失败的测试。

## 打包发布

先构建服务端 APK 与 `msvc-release` 预设，再运行：

```powershell
.\tools\package.ps1 -Smoke
```

它从 `build/msvc-release/bin` 收集发布用的文件（不收测试程序和 PDB），加上 Visual Studio 自带的 VC++ 运行库，以及 `packaging/adb.json` 固定版本的 adb（第一次运行时从 Google 下载到 `deps/adb`，核对 SHA-256 与 Google 的签名），然后在 `dist/` 写出安装包、便携版与符号包。写出前会检查包里每个程序和 DLL 依赖的文件都在包里或 Windows 里；`-Smoke` 还会从包里启动一次 `GameScrcpy.exe --smoke-test`（会弹出窗口）。

安装包由 Inno Setup 6 或 7 编译 `packaging/GameScrcpy.iss`；脚本按 `GS_ISCC`、PATH 与常见安装位置找 `ISCC.exe`，没装时加 `-NoSetup` 只出便携版与符号包。

## 许可证

提交代码即表示你同意以 [Apache License 2.0](LICENSE) 发布你的贡献。参与社区请遵守[行为准则](CODE_OF_CONDUCT.md)。
