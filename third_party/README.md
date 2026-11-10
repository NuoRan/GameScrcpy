# third_party — 第三方依赖清单

所有依赖都以固定版本纳入仓库：配置阶段不联网，离线可构建，升级有完整的差异可审查。除 FFmpeg、SDL3 与 libusb 外都是源码；FFmpeg、SDL3 与 libusb 是 `tools/build_ffmpeg.ps1`、`tools/build_sdl3.ps1`、`tools/build_libusb.ps1` 从固定源码编译出的产物。
升级依赖必须同时更新本表，并在提交说明里写清新版本与校验和。

| 依赖 | 版本 | 许可 | 用途 | 引入方式 |
|---|---|---|---|---|
| doctest | v2.5.3 | MIT | C++ 单元测试 | 单头文件 |
| tl::expected | v1.1.0 | CC0-1.0 | C++20 下的 `Expected` 实现 | 单头文件 |
| nlohmann/json | v3.12.0 | MIT | 核心层读写方案与设置（仅 PRIVATE 链接、只在 .cpp 中包含、无异常解析）；测试读取协议金标准向量 | 单头文件 |
| QWindowKit | 1.5.0（提交 `35e88f3655720ed0537c7dd1dde243bf4a70c94c`） | Apache-2.0 | 无边框窗口、Snap Layout | 源码（已删除 examples、docs） |
| └ qmsetup | 提交 `85c6c3c783be8af8d3f2fa492748a82da8ec9bad` | MIT | QWindowKit 的构建辅助工具 | 源码（QWindowKit 子模块） |
| └ syscmdline | 提交 `5a67673ff96acbfd894ea653fbaca872fded758a` | MIT | qmsetup 的命令行解析 | 源码（qmsetup 子模块，已删除 tests、examples） |
| FFmpeg（avcodec、avformat、avutil、swresample） | 9.0.2 | LGPL-2.1-or-later | 视频与音频解码，D3D11VA；avformat 只开 mp4 封装与文件协议，给录像导出 MP4 | MSVC 编译的共享库 + 头文件 + 导入库；配置与校验和见 `ffmpeg/BUILDINFO.md` |
| └ dav1d | 1.5.4 | BSD-2-Clause | AV1 软解，静态链入 avcodec | 随 FFmpeg 构建 |
| Lucide 图标（`lucide-static`） | 1.54.0 | ISC（源自 Feather 的图标另为 MIT） | 界面线性图标 | 只收用到的 SVG + `LICENSE`；`tools/gen_icons.py` 转成 QML 路径；来源包校验和见 `lucide/BUILDINFO.md` |
| QR Code generator（Nayuki，C++ 版） | v1.8.0 | MIT | 二维码配对的二维码 | 源码两个文件 + `LICENSE`，原样未改；校验和见 `qrcodegen/BUILDINFO.md` |
| miniz | 3.1.2 | MIT | 扩展包（`.gsext`）的读写：流式解压、按实际输出计量 | 发布包里的 `miniz.c`、`miniz.h` 与 `LICENSE`，原样未改；以 `MINIZ_NO_ZLIB_COMPATIBLE_NAMES` 编译成静态库；校验和见下表 |
| SDL3 | 3.4.18 | Zlib | 读取 Xbox、PS4、PS5、Switch Pro 手柄 | MSVC 编译的只含手柄与 HIDAPI 的共享库 + 头文件 + 导入库；配置与校验和见 `sdl3/BUILDINFO.md` |
| libusb | 1.0.30 | LGPL-2.1-or-later | AOA 触控方式：经 USB 控制传输在手机上建 HID 触摸屏、鼠标与键盘 | MSVC 用发布包自带的 `msvc/libusb_dll.vcxproj` 编译的共享库（约 163 KB，动态链接，只从程序目录加载）+ 头文件 + 导入库；源码包的发布签名已用发布公钥 `C68187379B23DE9EFC46651E2C80FF56C6830A0E` 核对；校验和见 `libusb/BUILDINFO.md` |
| QuickJS-ng | v0.17.0（提交 `6d46d07d04041b40f4f49eaa7fdebe44c314c699`） | MIT | 脚本引擎：每会话一个运行时、ES 模块、中断与内存上限 | 发布源码包里引擎本身的源文件与 `LICENSE`，不带 quickjs-libc；`quickjs.c` 有一处本地补丁（中断检查挪到回跳之前，让看门狗报出死循环所在的行），其余原样未改；编译成静态库 `gs::quickjs`；补丁说明与校验和见 `quickjs-ng/BUILDINFO.md` |
| TypeScript | 7.0.2 | Apache-2.0 | 脚本的类型检查：`gs check` 与脚本工作台调用的原生编译器，写脚本的人不用装 Node | npm 包 `@typescript/typescript-win32-x64` 的 `tsc.exe`、标准库类型声明与 `LICENSE`、`NOTICE.txt`，原样未改；构建时复制到 `gs.exe` 旁边的 `typescript/`；校验和见 `typescript/BUILDINFO.md` |

## 构建说明

- qmsetup 是配置阶段使用的主机工具，只构建一次并安装到 `build/_shared/qmsetup`，所有预设共用；删除 `build/` 后首次配置会重新构建（约 10 秒）。
- QWindowKit 以静态库形式随工程一起编译（`add_subdirectory(... SYSTEM)`，不受本项目警告策略约束）。

## 文件校验（单头文件）

| 文件 | 来源 | SHA256 |
|---|---|---|
| `doctest/doctest/doctest.h` | https://raw.githubusercontent.com/doctest/doctest/v2.5.3/doctest/doctest.h | `cfd518a3ef90f67e1f3ba514df23fb3627437de1a2feeba78cf5062a40021421` |
| `doctest/LICENSE.txt` | https://raw.githubusercontent.com/doctest/doctest/v2.5.3/LICENSE.txt | `0fe0b331fa1513dcce8604ff1fa925f32d1cea17d8aeb1c2471fad40d291adc5` |
| `tl-expected/include/tl/expected.hpp` | https://raw.githubusercontent.com/TartanLlama/expected/v1.1.0/include/tl/expected.hpp | `5a6a46da2e46ee3360bd8fbd759affefc46b8bfd6295043262ec867acac1fa14` |
| `tl-expected/COPYING` | https://raw.githubusercontent.com/TartanLlama/expected/v1.1.0/COPYING | `a2010f343487d3f7618affe54f789f5487602331c0a8d03f49e9a7c547cf0499` |
| `nlohmann-json/include/nlohmann/json.hpp` | https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp | `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63` |
| `nlohmann-json/LICENSE.MIT` | https://raw.githubusercontent.com/nlohmann/json/v3.12.0/LICENSE.MIT | `46a65cffd1ea955132d95a8dd921640714a8d6b537d2e4e482d31145ae95b603` |
| （miniz 发布包） | https://github.com/richgel999/miniz/releases/download/3.1.2/miniz-3.1.2.zip | `f0446d863f9c19926ad9483c523fdc42e42b8d4a6a431d27e09d49c79a140d9a` |
| `miniz/miniz.c` | 上述发布包 | `e2c1aeb66eef9191d8c3feb164db2def2335a61d039bf04ed849f6b042433b30` |
| `miniz/miniz.h` | 上述发布包 | `b53b62ed122e559b8f679e3cb787a0b0035fe87a58f909da0e44931678f4e85f` |
| `miniz/LICENSE` | 上述发布包 | `0115478d567121238cf6cc1c0c361926cf07a49d9e4c9e66da97fac6a01646b3` |

## 升级 QWindowKit 的步骤

1. `git clone --recursive --branch <tag> https://github.com/stdware/qwindowkit.git` 到临时目录。
2. 用 `robocopy <临时目录> third_party\qwindowkit /MIR /XD .git examples docs tests /XF .git .gitmodules` 覆盖。
3. 更新上表中的版本与三个提交号，删除 `build/_shared/qmsetup` 后重新配置并跑全部测试。

## 重建或升级 FFmpeg 的步骤

1. 一次性准备：MSYS2（`GS_MSYS2_ROOT`，缺省找 `C:\msys64`）并 `pacman -S --needed make diffutils pkgconf nasm`；Python 3（脚本自建 `deps/ffmpeg/venv` 安装固定版本的 meson）。
2. 升级时改 `tools/build_ffmpeg.ps1` 里的版本与 SHA-256。FFmpeg 只发布签名：先用发布公钥 `FCF986EA15E6E293A5644F10B4322F04D67658D8` 验证 `.asc`，再记下源码包的 SHA-256。
3. `. .\tools\dev-env.ps1` 后运行 `.\tools\build_ffmpeg.ps1`：下载并校验源码、编译 dav1d 与 FFmpeg、重写 `third_party/ffmpeg`（PDB 留在 `deps/ffmpeg/pdb`）。
4. 重新配置并跑全部测试（`gs_media_tests` 检查许可、解码器与 D3D11VA），更新上表后提交。

## 重建或升级 SDL3 的步骤

1. 升级时改 `tools/build_sdl3.ps1` 里的版本与 SHA-256；源码包从 GitHub 发布页与 libsdl.org 各取一份，两份一致再记下 SHA-256。
2. `. .\tools\dev-env.ps1` 后运行 `.\tools\build_sdl3.ps1`：下载并校验、只开手柄与 HIDAPI 编译、重写 `third_party/sdl3`。
3. 重新配置并跑全部测试（`gs_platform_tests` 会在采集线程上初始化与关闭 SDL），更新上表后提交。

## 重建或升级 libusb 的步骤

1. 升级时先用发布公钥核对新源码包的 `.asc` 签名（`SIGNING_KEYS.txt` 写着公钥编号；用临时的 `--homedir`，不动自己的钥匙圈），再把版本与 SHA-256 写进 `tools/build_libusb.ps1`。
2. `. .\tools\dev-env.ps1` 后运行 `.\tools\build_libusb.ps1`：下载并校验（不符就删掉下载的文件并报错）、用 MSBuild 编译发布包自带的 `msvc/libusb_dll.vcxproj`（Release、x64、v143、不开 Windows 热插拔）、重写 `third_party/libusb`。`.\tools\build_libusb.ps1 -SelfTest` 只检查下载校验本身（CTest `gs_libusb_build_selftest`）。
3. 重新配置并跑全部测试（`gs_app_tests` 核对 DLL 与 `BUILDINFO.md` 的校验和、运行时报出的版本），更新上表后提交。
