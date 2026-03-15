# 构建指南

**[English](BUILD_EN.md)** | 中文

本文档详细说明如何从源码构建 GameScrcpy。

---

## 目录

- [系统要求](#系统要求)
- [依赖项](#依赖项)
- [客户端构建](#客户端构建)
  - [方式一：Qt Creator (推荐)](#方式一qt-creator-推荐)
  - [方式二：Visual Studio](#方式二visual-studio)
  - [方式三：命令行](#方式三命令行)
  - [方式四：一键构建脚本](#方式四一键构建脚本)
- [服务端构建](#服务端构建)
- [Companion App 构建](#companion-app-构建)
- [打包发布](#打包发布)
- [常见问题](#常见问题)

---

## 系统要求

| 工具 | 最低版本 | 推荐版本 |
|------|----------|----------|
| 操作系统 | Windows 10 | Windows 11 |
| Qt | 6.5 | 6.10+ |
| CMake | 3.19 | 3.25+ |
| Visual Studio | 2022 | 2022 |
| C++ 标准 | C++17 | C++17 |

---

## 依赖项

### 必需依赖

| 依赖 | 版本 | 用途 |
|------|------|------|
| Qt | 6.5+ | GUI 框架 |
| MSVC | 2022 | C++ 编译器 |
| CMake | 3.19+ | 构建系统 |

### 已内置依赖

以下依赖已包含在仓库中：

| 依赖 | 用途 | 包含内容 |
|------|------|----------|
| ADB | Android 调试桥 | 完整 |
| scrcpy-server | Android 服务端 | 完整 |
| FFmpeg | 视频解码 | include + lib |
| OpenCV | 图像识别 | include + lib |
| QuickJS | JavaScript 脚本引擎 | 源码 (env/quickjs/) |
| libusb | AOA USB HID 触控 | include + lib (env/libusb/) |

### 需要下载的依赖 (DLL)

仓库已包含头文件和静态库，**只需下载 DLL 文件**：

#### FFmpeg DLL (必需)

| 项目 | 说明 |
|------|------|
| **版本** | 8.0.1 |
| **下载** | https://www.gyan.dev/ffmpeg/builds/ |
| **文件** | `ffmpeg-8.0.1-full_build-shared.7z` |

下载后将 `bin/` 目录中的 DLL 复制到 `client/env/ffmpeg/bin/`：
```
client/env/ffmpeg/bin/
├── avcodec-62.dll
├── avformat-62.dll
├── avutil-60.dll
├── swresample-6.dll
└── swscale-9.dll
```

#### OpenCV DLL (可选，图像识别需要)

| 项目 | 说明 |
|------|------|
| **版本** | 4.12.0 |
| **下载** | https://opencv.org/releases/ |
| **文件** | `opencv-4.12.0-windows.exe` |

运行自解压程序，将 DLL 复制到对应目录：
```
client/env/opencv/build/x64/vc16/bin/
└── opencv_world4120.dll
```

> ⚠️ 如果不需要图像识别功能，可以跳过 OpenCV，编译时会自动禁用。

---

## 客户端构建

### 方式一：Qt Creator (推荐)

这是最简单的构建方式，适合日常开发。

#### 1. 安装 Qt

1. 下载 [Qt Online Installer](https://www.qt.io/download-qt-installer)
2. 运行安装程序，登录 Qt 账号
3. 选择安装组件：
   - Qt 6.5.x (或更高版本)
     - ✅ MSVC 2022 64-bit
   - Developer and Designer Tools
     - ✅ CMake
     - ✅ Ninja

#### 2. 打开项目

1. 启动 Qt Creator
2. 文件 → 打开文件或项目
3. 选择 `client/CMakeLists.txt`
4. 选择构建套件：**Desktop Qt 6.x MSVC2022 64bit**
5. 点击「配置项目」

#### 3. 构建运行

1. 在左下角选择 **Release** 构建类型
2. 点击 🔨 构建按钮 (Ctrl+B)
3. 点击 ▶️ 运行按钮 (Ctrl+R)

构建产物位于：`client/build/Desktop_Qt_xxx_MSVC2022_64bit-Release/`

---

### 方式二：Visual Studio

适合习惯使用 Visual Studio 的开发者。

#### 1. 安装必要组件

1. 安装 [Visual Studio 2022](https://visualstudio.microsoft.com/)
2. 在安装程序中选择：
   - ✅ 使用 C++ 的桌面开发
   - ✅ 适用于 Windows 的 C++ CMake 工具

#### 2. 安装 Qt

同上，安装 Qt 6.5+ 的 MSVC 2022 64-bit 组件。

#### 3. 打开项目

1. 启动 Visual Studio 2022
2. 文件 → 打开 → CMake
3. 选择 `client/CMakeLists.txt`

#### 4. 配置 Qt 路径

如果 CMake 找不到 Qt，在 CMakeSettings.json 中添加：

```json
{
  "cmakeCommandArgs": "-DCMAKE_PREFIX_PATH=C:/Qt/6.5.0/msvc2022_64"
}
```

#### 5. 构建

1. 选择 **x64-Release** 配置
2. 生成 → 全部生成

---

### 方式三：命令行

适合 CI/CD 或熟悉命令行的用户。

```powershell
# 1. 设置 Qt 路径 (根据实际安装路径修改)
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.5.0\msvc2022_64"

# 2. 进入项目目录
cd D:\GameScrcpy\client

# 3. 创建构建目录
mkdir build
cd build

# 4. 配置 CMake
cmake -G "Visual Studio 17 2022" -A x64 ..

# 5. 构建 Release 版本
cmake --build . --config Release

# 6. 构建产物在 output\x64\Release\ 目录
dir ..\..\output\x64\Release\
```

---

### 方式四：一键构建脚本

项目提供了一键构建脚本，自动检测环境、构建并打包。

```powershell
# 进入脚本目录
cd ci\win

# 默认构建 (Release x64)
.\build_all.bat

# 指定 Qt 路径
.\build_all.bat --qt C:\Qt\6.5.0

# 构建 Debug 版本
.\build_all.bat --debug

# 同时构建服务端
.\build_all.bat --server

# 查看帮助
.\build_all.bat --help
```

**脚本参数：**

| 参数 | 说明 |
|------|------|
| `--debug` | 构建 Debug 版本 |
| `--release` | 构建 Release 版本 (默认) |
| `--server` | 同时构建服务端 |
| `--no-pack` | 不打包发布 |
| `--qt PATH` | 指定 Qt 安装路径 |
| `--help` | 显示帮助信息 |

脚本会自动：
1. 检测 Qt 和 Visual Studio 安装路径
2. 配置 CMake 并编译客户端
3. 部署 Qt 依赖 (windeployqt)
4. 复制 FFmpeg、ADB、配置文件
5. 生成发布包到 `output/GameScrcpy-Windows-x64/`

---

## 服务端构建

服务端 (scrcpy-server) 运行在 Android 设备上，负责屏幕捕获和事件注入。

> 💡 **注意**：项目已内置预编译的服务端 (`client/env/scrcpy-server`)，通常无需自己构建。

### 使用 Gradle 构建

#### 1. 安装 Android SDK

确保设置了 `ANDROID_HOME` 环境变量指向 Android SDK 目录。

```powershell
# 检查环境变量
echo $env:ANDROID_HOME
# 应该输出类似: C:\Users\xxx\AppData\Local\Android\Sdk
```

#### 2. 构建服务端

```powershell
cd server
..\gradlew.bat assembleRelease

# 或使用全局 Gradle
gradle assembleRelease
```

#### 3. 构建产物

```
server/build/outputs/apk/release/server-release-unsigned.apk
```

将此文件复制到 `client/env/scrcpy-server`（去掉扩展名）。

### 不使用 Gradle 构建

如果不想安装 Gradle，可以使用 Android SDK 的命令行工具：

```powershell
cd server
.\build_without_gradle.sh   # 需要 bash 环境
```

或者手动构建：

```powershell
# 1. 编译 Java 源码
$ANDROID_HOME/build-tools/34.0.0/d8 --release --output . `
    (Get-ChildItem -Recurse src/main/java/*.java)

# 2. 打包 DEX
$ANDROID_HOME/build-tools/34.0.0/aapt package -f `
    -M src/main/AndroidManifest.xml `
    -F server.apk

# 3. 添加 classes.dex 到 APK
zip -j server.apk classes.dex
```

### 服务端版本说明

| 版本 | 对应 scrcpy | 说明 |
|------|-------------|------|
| 3.3.4 | v3.0+ | 当前使用版本 |

---

## Companion App 构建

Companion App 是运行在 Android 手机端的辅助应用，提供浮动光标覆盖层和远程截屏功能。

### 环境要求

| 工具 | 要求 |
|------|------|
| Android SDK | API 34+ |
| Java | JDK 17+ |
| Gradle | 8.x (项目自带 Wrapper) |

### 构建步骤

```powershell
# 在项目根目录执行
.\gradlew.bat :companion:assembleRelease
```

### 构建产物

```
companion_app/app/build/outputs/apk/release/companion-release.apk
```

### 安装与使用

1. **安装 APK**
   ```powershell
   adb install companion-release.apk
   ```

2. **授予悬浮窗权限**
   - 打开手机「设置」→「应用」→「GameScrcpy Companion」→「悬浮窗」→ 允许

3. **启动服务**
   - 打开 Companion App
   - 点击「光标服务」按钮启动浮动光标（TCP 端口 26758）
   - 点击「截屏服务」按钮启动远程截屏（TCP 端口 26759），需授予屏幕录制权限

4. **PC 端连接**
   - PC 客户端会通过 ADB 端口转发自动连接 Companion App
   - 鼠标移动时手机屏幕上会显示对应位置的浮动光标

### 注意事项

- Android 14+ 设备需要前台服务特殊权限，应用已内置相关声明
- 截屏服务使用完毕后会自动停止，无需手动关闭
- 光标位置使用归一化坐标传输，自动适配横屏/竖屏旋转

---

## 打包发布

### 自动打包 (推荐)

使用一键构建脚本会自动完成打包：

```powershell
cd ci\win
.\build_all.bat
```

### 手动打包

#### 1. 部署 Qt 依赖

```powershell
# 进入构建输出目录
cd client\build\Release

# 使用 windeployqt 部署依赖
& "C:\Qt\6.5.0\msvc2022_64\bin\windeployqt.exe" --release GameScrcpy.exe
```

#### 2. 复制必要文件

```powershell
$OUTPUT = "D:\Release\GameScrcpy-Windows-x64"

# 复制主程序和 Qt 依赖
Copy-Item "client\build\Release\*" $OUTPUT -Recurse

# 复制 ADB
Copy-Item "client\env\adb\win\*" $OUTPUT

# 复制服务端
Copy-Item "client\env\scrcpy-server" $OUTPUT

# 复制 FFmpeg DLL
Copy-Item "client\env\ffmpeg\bin\*.dll" $OUTPUT

# 复制 OpenCV DLL (如果有)
Copy-Item "client\env\opencv\build\x64\vc16\bin\opencv_world*.dll" $OUTPUT

# 复制配置目录
Copy-Item "keymap" "$OUTPUT\keymap" -Recurse
Copy-Item "config" "$OUTPUT\config" -Recurse
```

### 发布包结构

```
GameScrcpy-Windows-x64/
├── GameScrcpy.exe           # 主程序
├── Qt6Core.dll            # Qt 核心库
├── Qt6Gui.dll             # Qt GUI 库
├── Qt6Widgets.dll         # Qt 控件库
├── Qt6Svg.dll             # Qt SVG 库
├── avcodec-62.dll         # FFmpeg 编解码
├── avformat-62.dll        # FFmpeg 格式
├── avutil-60.dll          # FFmpeg 工具
├── swresample-6.dll       # FFmpeg 重采样
├── swscale-9.dll          # FFmpeg 缩放
├── opencv_world4120.dll   # OpenCV (可选)
├── adb.exe                # ADB 工具
├── AdbWinApi.dll          # ADB 依赖
├── AdbWinUsbApi.dll       # ADB USB 依赖
├── scrcpy-server          # Android 服务端
├── keymap/                # 键位配置
│   ├── default.json
│   └── ...
├── config/                # 应用配置
│   └── config.ini
├── platforms/             # Qt 平台插件
│   └── qwindows.dll
├── styles/                # Qt 样式插件
│   └── qmodernwindowsstyle.dll
└── imageformats/          # Qt 图片插件
    ├── qjpeg.dll
    └── qpng.dll
```

### 发布清单

打包前确保包含以下文件：

- [ ] GameScrcpy.exe
- [ ] Qt DLL (由 windeployqt 生成)
- [ ] FFmpeg DLL (`avcodec-*.dll`, `avformat-*.dll` 等)
- [ ] OpenCV DLL (如启用图像匹配)
- [ ] adb.exe + AdbWinApi.dll + AdbWinUsbApi.dll
- [ ] scrcpy-server
- [ ] keymap/ 目录
- [ ] config/config.ini (不要包含 userdata.ini)
- [ ] platforms/qwindows.dll
- [ ] companion-release.apk (Companion App，可选)

---

## 常见问题

### Q: CMake 找不到 Qt

**症状**：
```
Could not find a package configuration file provided by "Qt6"
```

**解决方案**：设置 `CMAKE_PREFIX_PATH` 环境变量

```powershell
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.5.0\msvc2022_64"
```

或在 CMake 命令中指定：
```powershell
cmake .. -DCMAKE_PREFIX_PATH="C:\Qt\6.5.0\msvc2022_64"
```

---

### Q: 运行时缺少 DLL

**症状**：
```
由于找不到 Qt6Core.dll，无法继续执行代码
```

**解决方案**：

1. 使用 windeployqt 部署依赖
2. 或将 Qt 的 bin 目录添加到 PATH

```powershell
# 方法 1: 部署依赖
& "C:\Qt\6.5.0\msvc2022_64\bin\windeployqt.exe" GameScrcpy.exe

# 方法 2: 添加到 PATH (临时)
$env:PATH += ";C:\Qt\6.5.0\msvc2022_64\bin"
```

---

### Q: FFmpeg 相关错误

**症状**：
```
无法找到 avcodec-62.dll
```

**解决方案**：

确保 `client/env/ffmpeg/bin/` 下的 DLL 文件被复制到可执行文件目录：

```powershell
Copy-Item "client\env\ffmpeg\bin\*.dll" "client\build\Release\"
```

---

### Q: ADB 连接失败

**解决方案**：

1. 确保 USB 调试已启用
2. 安装 ADB 驱动 (可从设备制造商官网下载)
3. 重启 ADB 服务：
   ```powershell
   adb kill-server
   adb start-server
   ```

---

### Q: 编译 OpenCV 相关错误

**解决方案**：

如果不需要图像匹配功能，可以将 `client/env/opencv/` 目录移走或删除，编译时会自动禁用 OpenCV：

```powershell
# 删除 OpenCV 目录后重新构建，会自动禁用图像识别
Remove-Item -Recurse client\env\opencv
cmake ..
```

---

### Q: Gradle 构建服务端失败

**症状**：
```
SDK location not found
```

**解决方案**：

1. 确保已安装 Android SDK
2. 设置 `ANDROID_HOME` 环境变量：
   ```powershell
   $env:ANDROID_HOME = "C:\Users\$env:USERNAME\AppData\Local\Android\Sdk"
   ```
3. 或在 `server/local.properties` 中指定：
   ```
   sdk.dir=C:\\Users\\xxx\\AppData\\Local\\Android\\Sdk
   ```

---

### Q: 一键脚本找不到 Qt

**解决方案**：

使用 `--qt` 参数指定 Qt 路径：

```powershell
.\build_all.bat --qt "C:\Qt\6.5.0"
```

或设置环境变量：
```powershell
$env:ENV_QT_PATH = "C:\Qt\6.5.0"
.\build_all.bat
```

---

## 调试构建

如需调试，使用 Debug 配置：

```powershell
cmake --build . --config Debug
```

Debug 版本会启用更多日志输出，便于排查问题。

---

如有其他问题，请提交 [Issue](../../issues)。
