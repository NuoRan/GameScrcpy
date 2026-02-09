@echo off
:: ============================================================
:: GameScrcpy 服务端构建脚本 (Windows, 无需 Gradle)
::
:: 使用方法:
::   set ANDROID_HOME=C:\Users\xxx\AppData\Local\Android\Sdk
::   build_without_gradle.bat
:: ============================================================
setlocal enabledelayedexpansion
chcp 65001 >nul

echo.
echo ╔══════════════════════════════════════════════════════════╗
echo ║       GameScrcpy Server 构建脚本 (无 Gradle)               ║
echo ╚══════════════════════════════════════════════════════════╝
echo.

:: 配置
set "SCRCPY_DEBUG=false"
set "SCRCPY_VERSION_NAME=3.3.4"
set "PLATFORM=36"
set "BUILD_TOOLS=36.0.0"

:: 检查 ANDROID_HOME
if "%ANDROID_HOME%"=="" (
    echo [错误] 未设置 ANDROID_HOME 环境变量
    echo        请设置为 Android SDK 路径，例如:
    echo        set ANDROID_HOME=C:\Users\xxx\AppData\Local\Android\Sdk
    exit /b 1
)

echo 配置:
echo   ANDROID_HOME: %ANDROID_HOME%
echo   Platform: android-%PLATFORM%
echo   Build-tools: %BUILD_TOOLS%
echo.

:: 设置路径
set "SCRIPT_DIR=%~dp0"
set "BUILD_DIR=%SCRIPT_DIR%build_manual"
set "CLASSES_DIR=%BUILD_DIR%\classes"
set "GEN_DIR=%BUILD_DIR%\gen"
set "PLATFORM_TOOLS=%ANDROID_HOME%\platforms\android-%PLATFORM%"
set "BUILD_TOOLS_DIR=%ANDROID_HOME%\build-tools\%BUILD_TOOLS%"
set "ANDROID_JAR=%PLATFORM_TOOLS%\android.jar"
set "ANDROID_AIDL=%PLATFORM_TOOLS%\framework.aidl"
set "LAMBDA_JAR=%BUILD_TOOLS_DIR%\core-lambda-stubs.jar"
set "SERVER_BINARY=scrcpy-server"

:: 检查 Android SDK 组件
if not exist "%ANDROID_JAR%" (
    echo [错误] 找不到 Android 平台: %ANDROID_JAR%
    echo        请安装 Android SDK Platform %PLATFORM%
    exit /b 1
)

if not exist "%BUILD_TOOLS_DIR%\d8.bat" (
    echo [错误] 找不到 Build Tools: %BUILD_TOOLS_DIR%
    echo        请安装 Android SDK Build-Tools %BUILD_TOOLS%
    exit /b 1
)

:: 清理旧构建
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
mkdir "%CLASSES_DIR%"
mkdir "%GEN_DIR%\com\genymobile\scrcpy"

:: 生成 BuildConfig.java
echo [1/4] 生成 BuildConfig...
(
echo package com.genymobile.scrcpy;
echo.
echo public final class BuildConfig {
echo   public static final boolean DEBUG = %SCRCPY_DEBUG%;
echo   public static final String VERSION_NAME = "%SCRCPY_VERSION_NAME%";
echo }
) > "%GEN_DIR%\com\genymobile\scrcpy\BuildConfig.java"

:: 生成 AIDL
echo [2/4] 处理 AIDL 文件...
cd /d "%SCRIPT_DIR%src\main\aidl"

"%BUILD_TOOLS_DIR%\aidl.exe" -o"%GEN_DIR%" -I. ^
    android\content\IOnPrimaryClipChangedListener.aidl

if %errorlevel% neq 0 (
    echo [错误] AIDL 处理失败
    exit /b 1
)

"%BUILD_TOOLS_DIR%\aidl.exe" -o"%GEN_DIR%" -I. -p "%ANDROID_AIDL%" ^
    android\view\IDisplayWindowListener.aidl

if %errorlevel% neq 0 (
    echo [错误] AIDL 处理失败
    exit /b 1
)

:: 编译 Java 源码
echo [3/4] 编译 Java 源码...
cd /d "%SCRIPT_DIR%src\main\java"

:: 收集所有 Java 文件
set "JAVA_FILES="
for /r %%f in (*.java) do (
    set "JAVA_FILES=!JAVA_FILES! "%%f""
)

:: 收集生成的 Java 文件
cd /d "%GEN_DIR%"
for /r %%f in (*.java) do (
    set "JAVA_FILES=!JAVA_FILES! "%%f""
)

:: 编译
javac -encoding UTF-8 ^
    -bootclasspath "%ANDROID_JAR%" ^
    -cp "%LAMBDA_JAR%;%GEN_DIR%" ^
    -d "%CLASSES_DIR%" ^
    -source 1.8 -target 1.8 ^
    %JAVA_FILES%

if %errorlevel% neq 0 (
    echo [错误] Java 编译失败
    exit /b 1
)

:: DEX 化
echo [4/4] 生成 DEX...
cd /d "%CLASSES_DIR%"

:: 收集所有 class 文件
set "CLASS_FILES="
for /r %%f in (*.class) do (
    set "CLASS_FILES=!CLASS_FILES! "%%f""
)

:: 使用 d8 生成 DEX
call "%BUILD_TOOLS_DIR%\d8.bat" ^
    --classpath "%ANDROID_JAR%" ^
    --output "%BUILD_DIR%\classes.zip" ^
    %CLASS_FILES%

if %errorlevel% neq 0 (
    echo [错误] DEX 生成失败
    exit /b 1
)

:: 重命名为 scrcpy-server
cd /d "%BUILD_DIR%"
move /y classes.zip "%SERVER_BINARY%" >nul

:: 清理临时文件
rmdir /s /q "%CLASSES_DIR%" 2>nul
rmdir /s /q "%GEN_DIR%" 2>nul

echo.
echo ╔══════════════════════════════════════════════════════════╗
echo ║                    构建成功！                            ║
echo ╚══════════════════════════════════════════════════════════╝
echo.
echo 服务端文件: %BUILD_DIR%\%SERVER_BINARY%
echo.
echo 要使用此服务端，请复制到 client\env\ 目录:
echo   copy "%BUILD_DIR%\%SERVER_BINARY%" "client\env\scrcpy-server"
echo.

endlocal
exit /b 0
