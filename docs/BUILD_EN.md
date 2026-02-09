# Build Guide

中文 | **English**

This document provides detailed instructions for building GameScrcpy from source.

---

## Table of Contents

- [System Requirements](#system-requirements)
- [Dependencies](#dependencies)
- [Client Build](#client-build)
  - [Method 1: Qt Creator (Recommended)](#method-1-qt-creator-recommended)
  - [Method 2: Visual Studio](#method-2-visual-studio)
  - [Method 3: Command Line](#method-3-command-line)
  - [Method 4: One-Click Build Script](#method-4-one-click-build-script)
- [Server Build](#server-build)
- [Packaging for Release](#packaging-for-release)
- [FAQ](#faq)

---

## System Requirements

| Tool | Minimum | Recommended |
|------|---------|-------------|
| OS | Windows 10 | Windows 11 |
| Qt | 6.5 | 6.10+ |
| CMake | 3.19 | 3.25+ |
| Visual Studio | 2022 | 2022 |
| C++ Standard | C++17 | C++17 |

---

## Dependencies

### Required

| Dependency | Version | Purpose |
|------------|---------|---------|
| Qt | 6.5+ | GUI framework, multimedia |
| MSVC | 2022 | C++ compiler |
| CMake | 3.19+ | Build system |

### Bundled Dependencies

The following dependencies are already included in the repository:

| Dependency | Purpose | Contents |
|------------|---------|----------|
| ADB | Android Debug Bridge | Complete |
| scrcpy-server | Android server | Complete |
| FFmpeg | Video decoding | include + lib |
| OpenCV | Image recognition | include + lib |

### Required Downloads (DLLs)

The repository includes headers and static libraries. **You only need to download the DLL files**:

#### FFmpeg DLLs (Required)

| Item | Details |
|------|---------|
| **Version** | 7.1 |
| **Download** | https://github.com/BtbN/FFmpeg-Builds/releases |
| **File** | `ffmpeg-n7.1-latest-win64-lgpl-shared-7.1.zip` |

After downloading, copy the DLLs from the `bin/` directory to `client/env/ffmpeg/bin/`:
```
client/env/ffmpeg/bin/
├── avcodec-62.dll
├── avformat-62.dll
├── avutil-60.dll
├── swresample-6.dll
└── swscale-9.dll
```

#### OpenCV DLL (Optional, required for image recognition)

| Item | Details |
|------|---------|
| **Version** | 4.10.0 |
| **Download** | https://opencv.org/releases/ |
| **File** | `opencv-4.10.0-windows.exe` |

Run the self-extracting program and copy the DLL to the appropriate directory:
```
client/env/opencv/build/x64/vc16/bin/
└── opencv_world4100.dll
```

> ⚠️ If you don't need image recognition, you can skip OpenCV. It will be automatically disabled during compilation.

---

## Client Build

### Method 1: Qt Creator (Recommended)

The simplest build method, suitable for daily development.

#### 1. Install Qt

1. Download the [Qt Online Installer](https://www.qt.io/download-qt-installer)
2. Run the installer and sign in to your Qt account
3. Select components:
   - Qt 6.5.x (or higher)
     - ✅ MSVC 2022 64-bit
     - ✅ Qt Multimedia
     - ✅ Qt 5 Compatibility Module
   - Developer and Designer Tools
     - ✅ CMake
     - ✅ Ninja

#### 2. Open Project

1. Launch Qt Creator
2. File → Open File or Project
3. Select `client/CMakeLists.txt`
4. Choose kit: **Desktop Qt 6.x MSVC2022 64bit**
5. Click "Configure Project"

#### 3. Build & Run

1. Select **Release** build type in the bottom-left corner
2. Click 🔨 Build (Ctrl+B)
3. Click ▶️ Run (Ctrl+R)

Build output will be in: `client/build/Desktop_Qt_xxx_MSVC2022_64bit-Release/`

---

### Method 2: Visual Studio

For developers who prefer Visual Studio.

#### 1. Install Required Components

1. Install [Visual Studio 2022](https://visualstudio.microsoft.com/)
2. In the installer, select:
   - ✅ Desktop development with C++
   - ✅ C++ CMake tools for Windows

#### 2. Install Qt

Same as above — install the MSVC 2022 64-bit component for Qt 6.5+.

#### 3. Open Project

1. Launch Visual Studio 2022
2. File → Open → CMake
3. Select `client/CMakeLists.txt`

#### 4. Configure Qt Path

If CMake can't find Qt, add this to CMakeSettings.json:

```json
{
  "cmakeCommandArgs": "-DCMAKE_PREFIX_PATH=C:/Qt/6.5.0/msvc2022_64"
}
```

#### 5. Build

1. Select the **x64-Release** configuration
2. Build → Build All

---

### Method 3: Command Line

For CI/CD or command-line users.

```powershell
# 1. Set Qt path (adjust to your installation)
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.5.0\msvc2022_64"

# 2. Navigate to project directory
cd D:\GameScrcpy\client

# 3. Create build directory
mkdir build
cd build

# 4. Configure CMake
cmake -G "Visual Studio 17 2022" -A x64 ..

# 5. Build Release
cmake --build . --config Release

# 6. Output is in Release/ directory
dir Release\
```

---

### Method 4: One-Click Build Script

The project provides a one-click build script that automatically detects the environment, builds, and packages.

```powershell
# Navigate to script directory
cd ci\win

# Default build (Release x64)
.\build_all.bat

# Specify Qt path
.\build_all.bat --qt C:\Qt\6.5.0

# Build Debug version
.\build_all.bat --debug

# Also build server
.\build_all.bat --server

# Show help
.\build_all.bat --help
```

**Script Parameters:**

| Parameter | Description |
|-----------|-------------|
| `--debug` | Build Debug version |
| `--release` | Build Release version (default) |
| `--server` | Also build the server |
| `--no-pack` | Don't create release package |
| `--qt PATH` | Specify Qt installation path |

The script automatically:
1. Detects Qt and Visual Studio installation paths
2. Configures CMake and compiles the client
3. Deploys Qt dependencies (windeployqt)
4. Copies FFmpeg, ADB, and config files
5. Generates release package at `output/GameScrcpy-Windows-x64/`

---

## Server Build

The server (scrcpy-server) runs on the Android device, handling screen capture and event injection.

> 💡 **Note**: A prebuilt server is included (`client/env/scrcpy-server`). You usually don't need to build it yourself.

### Build with Gradle

#### 1. Install Android SDK

Ensure the `ANDROID_HOME` environment variable points to your Android SDK directory.

```powershell
# Check environment variable
echo $env:ANDROID_HOME
# Should output something like: C:\Users\xxx\AppData\Local\Android\Sdk
```

#### 2. Build Server

```powershell
cd server

# Windows
.\gradlew.bat assembleRelease

# Or use global Gradle
gradle assembleRelease
```

#### 3. Build Output

```
server/build/outputs/apk/release/server-release-unsigned.apk
```

Copy this file to `client/env/scrcpy-server` (remove the extension).

### Build without Gradle

If you don't want to install Gradle, you can use Android SDK command-line tools:

```powershell
cd server
.\build_without_gradle.sh   # Requires bash environment
```

Or build manually:

```powershell
# 1. Compile Java source
$ANDROID_HOME/build-tools/34.0.0/d8 --release --output . `
    (Get-ChildItem -Recurse src/main/java/*.java)

# 2. Package DEX
$ANDROID_HOME/build-tools/34.0.0/aapt package -f `
    -M src/main/AndroidManifest.xml `
    -F server.apk

# 3. Add classes.dex to APK
zip -j server.apk classes.dex
```

### Server Version Info

| Version | Corresponding scrcpy | Notes |
|---------|---------------------|-------|
| 3.3.4 | v3.0+ | Currently used version |

---

## Packaging for Release

### Automated Packaging (Recommended)

The one-click build script handles packaging automatically:

```powershell
cd ci\win
.\build_all.bat
```

### Manual Packaging

#### 1. Deploy Qt Dependencies

```powershell
# Navigate to build output directory
cd client\build\Release

# Deploy dependencies with windeployqt
& "C:\Qt\6.5.0\msvc2022_64\bin\windeployqt.exe" --release GameScrcpy.exe
```

#### 2. Copy Required Files

```powershell
$OUTPUT = "D:\Release\GameScrcpy-Windows-x64"

# Copy main program and Qt dependencies
Copy-Item "client\build\Release\*" $OUTPUT -Recurse

# Copy ADB
Copy-Item "client\env\adb\win\*" $OUTPUT

# Copy server
Copy-Item "client\env\scrcpy-server" $OUTPUT

# Copy FFmpeg DLLs
Copy-Item "client\env\ffmpeg\bin\*.dll" $OUTPUT

# Copy OpenCV DLL (if available)
Copy-Item "client\env\opencv\build\x64\vc16\bin\opencv_world*.dll" $OUTPUT

# Copy config directories
Copy-Item "keymap" "$OUTPUT\keymap" -Recurse
Copy-Item "config" "$OUTPUT\config" -Recurse
```

### Release Package Structure

```
GameScrcpy-Windows-x64/
├── GameScrcpy.exe           # Main application
├── Qt6Core.dll            # Qt core library
├── Qt6Gui.dll             # Qt GUI library
├── Qt6Widgets.dll         # Qt widgets library
├── Qt6Multimedia.dll      # Qt multimedia library
├── avcodec-60.dll         # FFmpeg codec
├── avformat-60.dll        # FFmpeg format
├── avutil-58.dll          # FFmpeg utility
├── swresample-4.dll       # FFmpeg resampler
├── swscale-7.dll          # FFmpeg scaler
├── opencv_world490.dll    # OpenCV (optional)
├── adb.exe                # ADB tool
├── AdbWinApi.dll          # ADB dependency
├── AdbWinUsbApi.dll       # ADB USB dependency
├── scrcpy-server          # Android server
├── keymap/                # Key mapping configs
│   ├── default.json
│   └── ...
├── config/                # Application config
│   └── config.ini
├── platforms/             # Qt platform plugins
│   └── qwindows.dll
├── multimedia/            # Qt multimedia plugins
│   └── ...
└── imageformats/          # Qt image plugins
    ├── qjpeg.dll
    └── qpng.dll
```

### Release Checklist

Ensure the following files are included before packaging:

- [ ] GameScrcpy.exe
- [ ] Qt DLLs (generated by windeployqt)
- [ ] FFmpeg DLLs (`avcodec-*.dll`, `avformat-*.dll`, etc.)
- [ ] OpenCV DLL (if image matching is enabled)
- [ ] adb.exe + AdbWinApi.dll + AdbWinUsbApi.dll
- [ ] scrcpy-server
- [ ] keymap/ directory
- [ ] config/config.ini (do NOT include userdata.ini)
- [ ] platforms/qwindows.dll

---

## FAQ

### Q: CMake can't find Qt

**Symptom**:
```
Could not find a package configuration file provided by "Qt6"
```

**Solution**: Set the `CMAKE_PREFIX_PATH` environment variable

```powershell
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.5.0\msvc2022_64"
```

Or specify it in the CMake command:
```powershell
cmake .. -DCMAKE_PREFIX_PATH="C:\Qt\6.5.0\msvc2022_64"
```

---

### Q: Missing DLLs at runtime

**Symptom**:
```
The code execution cannot proceed because Qt6Core.dll was not found
```

**Solution**:

1. Deploy dependencies with windeployqt
2. Or add Qt's bin directory to PATH

```powershell
# Method 1: Deploy dependencies
& "C:\Qt\6.5.0\msvc2022_64\bin\windeployqt.exe" GameScrcpy.exe

# Method 2: Add to PATH (temporary)
$env:PATH += ";C:\Qt\6.5.0\msvc2022_64\bin"
```

---

### Q: FFmpeg-related errors

**Symptom**:
```
avcodec-60.dll was not found
```

**Solution**:

Ensure the DLL files from `client/env/ffmpeg/bin/` are copied to the executable directory:

```powershell
Copy-Item "client\env\ffmpeg\bin\*.dll" "client\build\Release\"
```

---

### Q: ADB connection failure

**Solution**:

1. Ensure USB debugging is enabled
2. Install ADB drivers (available from device manufacturer's website)
3. Restart ADB service:
   ```powershell
   adb kill-server
   adb start-server
   ```

---

### Q: OpenCV compilation errors

**Solution**:

If you don't need image matching, you can disable OpenCV:

```powershell
cmake .. -DENABLE_OPENCV=OFF
```

---

### Q: Gradle server build failure

**Symptom**:
```
SDK location not found
```

**Solution**:

1. Ensure Android SDK is installed
2. Set the `ANDROID_HOME` environment variable:
   ```powershell
   $env:ANDROID_HOME = "C:\Users\$env:USERNAME\AppData\Local\Android\Sdk"
   ```
3. Or specify it in `server/local.properties`:
   ```
   sdk.dir=C:\\Users\\xxx\\AppData\\Local\\Android\\Sdk
   ```

---

### Q: One-click script can't find Qt

**Solution**:

Use the `--qt` parameter to specify the Qt path:

```powershell
.\build_all.bat --qt "C:\Qt\6.5.0"
```

Or set the environment variable:
```powershell
$env:ENV_QT_PATH = "C:\Qt\6.5.0"
.\build_all.bat
```

---

## Debug Build

For debugging, use the Debug configuration:

```powershell
cmake --build . --config Debug
```

Debug builds enable additional logging output for troubleshooting.

---

If you have other questions, please submit an [Issue](../../issues).
