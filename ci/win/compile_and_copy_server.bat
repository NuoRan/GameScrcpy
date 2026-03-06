@echo off
setlocal
chcp 65001 >nul

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%..\.."
cd /d "%ROOT_DIR%"

echo [1/2] 编译 server Java...
call .\gradlew.bat :server:compileReleaseJavaWithJavac
if errorlevel 1 (
    echo [错误] 编译失败
    exit /b 1
)

set "SRC=%ROOT_DIR%\server\build\outputs\apk\release\server-release-unsigned.apk"
set "DST=%ROOT_DIR%\client\env\scrcpy-server"

if not exist "%SRC%" (
    echo [提示] 未找到 APK，补充执行 assembleRelease 生成产物...
    call .\gradlew.bat :server:assembleRelease
    if errorlevel 1 (
        echo [错误] assembleRelease 失败
        exit /b 1
    )
)

if not exist "%SRC%" (
    echo [错误] 仍未找到产物: %SRC%
    exit /b 1
)

echo [2/2] 复制到客户端...
copy /y "%SRC%" "%DST%" >nul
if errorlevel 1 (
    echo [错误] 复制失败
    exit /b 1
)

echo [完成] 已替换: %DST%
exit /b 0
