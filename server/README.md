# 服务端构建指南

服务端 (scrcpy-server) 是运行在 Android 设备上的 Java 应用，负责屏幕捕获和事件注入。

> 💡 **提示**：项目已内置预编译的服务端 (`client/env/scrcpy-server`)，版本为 v3.3.4。通常无需自己构建。

---

## 何时需要自行构建

- 需要修改服务端代码
- 需要使用不同的 Android SDK 版本
- 需要调试服务端

---

## 环境准备

### 1. 安装 Android SDK

推荐使用 Android Studio 安装 SDK：

1. 下载安装 [Android Studio](https://developer.android.com/studio)
2. 打开 SDK Manager (Tools → SDK Manager)
3. 安装以下组件：
   - **SDK Platforms**: Android 14.0 (API 36) 或更高
   - **SDK Tools**:
     - Android SDK Build-Tools 36.0.0
     - Android SDK Command-line Tools

### 2. 配置环境变量

```powershell
# Windows
$env:ANDROID_HOME = "C:\Users\$env:USERNAME\AppData\Local\Android\Sdk"

# 或者永久设置 (系统环境变量)
[Environment]::SetEnvironmentVariable("ANDROID_HOME", "C:\Users\$env:USERNAME\AppData\Local\Android\Sdk", "User")
```

验证安装：
```powershell
# 检查 SDK 路径
ls $env:ANDROID_HOME\platforms
ls $env:ANDROID_HOME\build-tools
```

### 3. 安装 JDK (可选)

如果要使用无 Gradle 构建，需要安装 JDK 8+：

```powershell
# 检查 Java 版本
java -version
javac -version
```

---

## 构建方式

### 方式一：Gradle 构建 (推荐)

最简单的方式，自动处理所有依赖。

```powershell
cd server

# Windows
.\gradlew.bat assembleRelease

# 构建产物
dir build\outputs\apk\release\
```

构建产物：`build/outputs/apk/release/server-release-unsigned.apk`

### 方式二：无 Gradle 构建

不需要安装 Gradle，直接使用 Android SDK 工具。

```powershell
cd server

# 设置 Android SDK 路径
$env:ANDROID_HOME = "C:\Users\xxx\AppData\Local\Android\Sdk"

# 运行构建脚本
.\build_without_gradle.bat
```

构建产物：`build_manual/scrcpy-server`

### 方式三：使用一键脚本

```powershell
cd ci\win
.\build_all.bat --server
```

---

## 部署服务端

将构建好的服务端复制到客户端 env 目录：

```powershell
# Gradle 构建的
copy server\build\outputs\apk\release\server-release-unsigned.apk client\env\scrcpy-server

# 无 Gradle 构建的
copy server\build_manual\scrcpy-server client\env\scrcpy-server
```

---

## 项目结构

```
server/
├── build.gradle           # Gradle 构建配置
├── gradlew.bat            # Gradle 包装器 (Windows)
├── gradlew                # Gradle 包装器 (Linux/macOS)
├── build_without_gradle.bat  # 无 Gradle 构建脚本 (Windows)
├── build_without_gradle.sh   # 无 Gradle 构建脚本 (Linux/macOS)
├── proguard-rules.pro     # ProGuard 混淆规则
└── src/
    └── main/
        ├── AndroidManifest.xml
        ├── aidl/          # AIDL 接口定义
        │   └── android/
        │       ├── content/
        │       └── view/
        └── java/          # Java 源码
            └── com/genymobile/scrcpy/
                ├── Server.java          # 主入口
                ├── ScreenCapture.java   # 屏幕捕获
                ├── Controller.java      # 事件注入
                ├── audio/               # 音频捕获
                ├── control/             # 控制命令
                ├── device/              # 设备信息
                ├── video/               # 视频编码
                └── wrappers/            # Android API 封装
```

---

## 版本对应

| 服务端版本 | scrcpy 版本 | 说明 |
|-----------|-------------|------|
| 3.3.4 | v3.0+ | 当前版本 |

---

## 常见问题

### Q: Gradle 下载失败

**解决方案**：

1. 检查网络连接
2. 配置 Gradle 代理：
   ```properties
   # gradle.properties
   systemProp.http.proxyHost=127.0.0.1
   systemProp.http.proxyPort=7890
   systemProp.https.proxyHost=127.0.0.1
   systemProp.https.proxyPort=7890
   ```

### Q: SDK location not found

**解决方案**：

创建 `server/local.properties` 文件：
```properties
sdk.dir=C:\\Users\\xxx\\AppData\\Local\\Android\\Sdk
```

### Q: 编译错误 "找不到符号"

**解决方案**：

确保安装了正确版本的 Android SDK Platform：
```powershell
# 检查已安装的平台
ls $env:ANDROID_HOME\platforms
```

### Q: d8.bat 找不到

**解决方案**：

安装 Android SDK Build-Tools：

1. 打开 Android Studio
2. Tools → SDK Manager
3. SDK Tools → 勾选 "Android SDK Build-Tools"
4. 选择版本 36.0.0 或更高

---

## 调试服务端

### 启用详细日志

修改 `Server.java` 中的日志级别：

```java
Ln.initLogLevel(Ln.Level.VERBOSE);
```

### 查看服务端日志

```powershell
adb logcat -s scrcpy
```

---

如有其他问题，请提交 [Issue](../../issues)。
