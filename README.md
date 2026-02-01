# QtScrcpy

<p align="center">
  <img src="client/src/ui/resources/icons/logo.png" alt="QtScrcpy Logo" width="128">
</p>

<p align="center">
  <strong>基于 Qt 的 Android 设备投屏控制工具</strong>
</p>

<p align="center">
  <a href="#功能特性">功能特性</a> •
  <a href="#快速开始">快速开始</a> •
  <a href="#构建指南">构建指南</a> •
  <a href="#使用说明">使用说明</a> •
  <a href="#许可证">许可证</a>
</p>

---

## 📖 简介

QtScrcpy 是一个跨平台的 Android 设备投屏控制工具，基于 [scrcpy](https://github.com/Genymobile/scrcpy) 协议实现，使用 Qt 框架开发。它允许你通过 USB 或 WiFi 连接 Android 设备，在电脑上实时显示和控制手机屏幕。

### 主要特性

- 🖥️ **跨平台支持** - Windows、macOS、Linux
- 📱 **USB/WiFi 连接** - 支持有线和无线两种连接方式
- 🎮 **键鼠映射** - 自定义键盘鼠标映射，适配手游操作
- 🚀 **KCP 传输** - 支持 KCP 协议，优化网络传输性能
- 📹 **高清低延迟** - H.264/H.265 硬件解码，延迟低于 100ms
- 🎯 **图像匹配** - 支持 OpenCV 图像识别（可选）
- 📋 **剪贴板同步** - 电脑与手机剪贴板双向同步

## 🚀 快速开始

### 系统要求

- **Android 设备**: Android 5.0 (API 21) 或更高版本
- **电脑系统**: Windows 10+、macOS 10.15+、Ubuntu 20.04+
- **开发环境** (仅构建需要):
  - Qt 6.2+ 或 Qt 5.15+
  - CMake 3.19+
  - C++17 编译器

### 预编译版本

从 [Releases](../../releases) 页面下载对应平台的预编译版本。

### 使用步骤

1. **启用 USB 调试**
   - 在 Android 设备上打开 `设置` → `关于手机`
   - 连续点击 `版本号` 7 次启用开发者选项
   - 进入 `开发者选项`，启用 `USB 调试`

2. **连接设备**
   - USB 连接：直接用数据线连接电脑和手机
   - WiFi 连接：确保设备和电脑在同一网络

3. **运行 QtScrcpy**
   - 启动程序后，点击 `刷新设备` 按钮
   - 选择设备，点击 `启动投屏`

## 🔧 构建指南

### 依赖项

| 依赖 | 版本 | 说明 |
|------|------|------|
| Qt | 6.2+ / 5.15+ | GUI 框架 |
| CMake | 3.19+ | 构建系统 |
| FFmpeg | 4.x | 视频解码 |
| OpenCV | 4.x | 图像匹配 (可选) |
| Android SDK | - | 构建服务端 |

### Windows 构建

```powershell
# 1. 克隆仓库
git clone https://github.com/YOUR_USERNAME/QtScrcpy.git
cd QtScrcpy

# 2. 创建构建目录
mkdir build && cd build

# 3. 配置 CMake (使用 Qt Creator 或命令行)
cmake -G "Visual Studio 17 2022" -A x64 ../client

# 4. 构建
cmake --build . --config Release

# 5. 构建服务端 (需要 Android SDK)
cd ../server
./gradlew assembleRelease
```

### macOS 构建

```bash
# 1. 安装依赖
brew install qt cmake ffmpeg

# 2. 克隆并构建
git clone https://github.com/YOUR_USERNAME/QtScrcpy.git
cd QtScrcpy/client

mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
make -j$(sysctl -n hw.ncpu)
```

### Linux 构建

```bash
# 1. 安装依赖 (Ubuntu/Debian)
sudo apt update
sudo apt install -y qt6-base-dev qt6-multimedia-dev cmake \
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev

# 2. 克隆并构建
git clone https://github.com/YOUR_USERNAME/QtScrcpy.git
cd QtScrcpy/client

mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 服务端构建

```bash
cd server

# 使用 Gradle 构建
./gradlew assembleRelease

# 或不使用 Gradle
./build_without_gradle.sh
```

构建产物位于 `server/build/outputs/apk/release/`

## 📁 项目结构

```
QtScrcpy/
├── client/                 # 客户端 (Qt/C++)
│   ├── src/
│   │   ├── app/           # 应用入口
│   │   ├── ui/            # 用户界面
│   │   ├── control/       # 控制模块
│   │   ├── transport/     # 传输模块 (TCP/KCP)
│   │   ├── decoder/       # 视频解码
│   │   ├── render/        # 渲染模块
│   │   └── common/        # 公共模块
│   ├── env/               # 预编译依赖
│   │   ├── ffmpeg/        # FFmpeg 库
│   │   ├── adb/           # ADB 工具
│   │   └── opencv/        # OpenCV 库
│   └── keymap/            # 键盘映射配置
│
├── server/                 # 服务端 (Android/Java)
│   └── src/main/java/
│       └── com/genymobile/scrcpy/
│
├── config/                 # 配置文件
└── ci/                     # CI/CD 脚本
```

## ⌨️ 键盘映射

QtScrcpy 支持自定义键盘映射，配置文件位于 `client/keymap/` 目录。

### 配置示例

```json
{
  "switchKey": "~",
  "mouseMoveMap": {
    "startPos": { "x": 0.5, "y": 0.5 },
    "speedRatio": 1.0
  },
  "keyMapNodes": [
    {
      "type": "click",
      "key": "W",
      "pos": { "x": 0.3, "y": 0.7 }
    }
  ]
}
```

### 内置配置

- `default.json` - 默认配置
- `gameforpeace.json` - 和平精英
- `identityv.json` - 第五人格

## 🔌 传输协议

### TCP 模式 (默认)

通过 ADB 端口转发建立 TCP 连接，稳定可靠。

### KCP 模式

使用 KCP 协议进行 UDP 传输，适合网络不稳定的场景：
- 更低的延迟
- 更好的丢包恢复
- 适合 WiFi 连接

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！

请参阅 [CONTRIBUTING.md](CONTRIBUTING.md) 了解贡献指南。

## 📄 许可证

本项目基于 [Apache License 2.0](LICENSE) 开源。

## 🙏 致谢

- [scrcpy](https://github.com/Genymobile/scrcpy) - 原始协议实现
- [FFmpeg](https://ffmpeg.org/) - 视频解码
- [Qt](https://www.qt.io/) - GUI 框架
- [KCP](https://github.com/skywind3000/kcp) - 可靠 UDP 传输

---

<p align="center">
  如果这个项目对你有帮助，请给一个 ⭐ Star！
</p>
