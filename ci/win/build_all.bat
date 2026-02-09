@echo off
:: ============================================================
:: GameScrcpy 一键构建脚本
:: 自动检测环境、构建客户端和服务端、打包发布
:: ============================================================
setlocal enabledelayedexpansion
chcp 65001 >nul

echo.
echo ╔══════════════════════════════════════════════════════════╗
echo ║           GameScrcpy 一键构建脚本 v1.0                     ║
echo ╚══════════════════════════════════════════════════════════╝
echo.

:: 获取脚本路径
set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%..\.."
cd /d "%ROOT_DIR%"

:: 默认参数
set "BUILD_MODE=Release"
set "BUILD_CLIENT=1"
set "BUILD_SERVER=0"
set "PACK_RELEASE=1"
set "QT_PATH="
set "ERRNO=0"

:: 解析命令行参数
:parse_args
if "%~1"=="" goto :check_env
if /i "%~1"=="--debug" set "BUILD_MODE=Debug" & shift & goto :parse_args
if /i "%~1"=="--release" set "BUILD_MODE=Release" & shift & goto :parse_args
if /i "%~1"=="--server" set "BUILD_SERVER=1" & shift & goto :parse_args
if /i "%~1"=="--no-pack" set "PACK_RELEASE=0" & shift & goto :parse_args
if /i "%~1"=="--qt" set "QT_PATH=%~2" & shift & shift & goto :parse_args
if /i "%~1"=="--help" goto :show_help
shift
goto :parse_args

:show_help
echo 用法: build_all.bat [选项]
echo.
echo 选项:
echo   --debug       构建 Debug 版本 (默认: Release)
echo   --release     构建 Release 版本
echo   --server      同时构建服务端
echo   --no-pack     不打包发布
echo   --qt PATH     指定 Qt 安装路径 (如: C:\Qt\6.5.0)
echo   --help        显示此帮助
echo.
echo 示例:
echo   build_all.bat                          # 默认构建 Release x64
echo   build_all.bat --debug                  # 构建 Debug 版本
echo   build_all.bat --server                 # 同时构建服务端
echo   build_all.bat --qt C:\Qt\6.8.0         # 指定 Qt 路径
goto :eof

:: ============================================================
:: 检查环境
:: ============================================================
:check_env
echo [1/5] 检查构建环境...
echo.

:: 检测 Qt 路径
if not "%QT_PATH%"=="" (
    echo     使用指定的 Qt 路径: %QT_PATH%
) else if not "%ENV_QT_PATH%"=="" (
    set "QT_PATH=%ENV_QT_PATH%"
    echo     从环境变量获取 Qt 路径: %QT_PATH%
) else (
    :: 尝试自动检测 Qt 安装路径
    for %%P in (
        "C:\Qt\6.8.0"
        "C:\Qt\6.7.0"
        "C:\Qt\6.6.0"
        "C:\Qt\6.5.0"
        "D:\Qt\6.8.0"
        "D:\Qt\6.7.0"
        "D:\Qt\6.6.0"
        "D:\Qt\6.5.0"
    ) do (
        if exist "%%~P\msvc2022_64" (
            set "QT_PATH=%%~P"
            echo     自动检测到 Qt 路径: %%~P
            goto :qt_found
        )
    )
    echo [错误] 未找到 Qt 安装路径！
    echo        请使用 --qt 参数指定，或设置 ENV_QT_PATH 环境变量
    set "ERRNO=1"
    goto :end
)
:qt_found

:: 设置 Qt 编译器路径
set "QT_MSVC_PATH=%QT_PATH%\msvc2022_64"

if not exist "%QT_MSVC_PATH%" (
    echo [错误] Qt MSVC 目录不存在: %QT_MSVC_PATH%
    set "ERRNO=1"
    goto :end
)

:: 检测 CMake
where cmake >nul 2>&1
if %errorlevel% neq 0 (
    echo [错误] 未找到 CMake！请确保已安装并添加到 PATH
    set "ERRNO=1"
    goto :end
)
echo     CMake: OK

:: 检测 Visual Studio 2022
set "VS_FOUND=0"
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARSALL=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
    set "VS_FOUND=1"
    set "VS_VERSION=Visual Studio 17 2022"
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARSALL=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvarsall.bat"
    set "VS_FOUND=1"
    set "VS_VERSION=Visual Studio 17 2022"
)
if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat" (
    set "VCVARSALL=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvarsall.bat"
    set "VS_FOUND=1"
    set "VS_VERSION=Visual Studio 17 2022"
)

if "%VS_FOUND%"=="0" (
    echo [错误] 未找到 Visual Studio 2022！请安装 VS 2022
    set "ERRNO=1"
    goto :end
)
echo     Visual Studio: OK (%VS_VERSION%)

echo.
echo     构建配置:
echo       - 模式: %BUILD_MODE%
echo       - 架构: x64
echo       - Qt: %QT_MSVC_PATH%
echo.

:: ============================================================
:: 构建客户端
:: ============================================================
:build_client
if "%BUILD_CLIENT%"=="0" goto :build_server

echo [2/5] 构建客户端...
echo.

:: 创建构建目录
set "CLIENT_BUILD_DIR=%ROOT_DIR%\client\build\%BUILD_MODE%_x64"
if exist "%CLIENT_BUILD_DIR%" rmdir /s /q "%CLIENT_BUILD_DIR%"
mkdir "%CLIENT_BUILD_DIR%"
cd /d "%CLIENT_BUILD_DIR%"

echo     配置 CMake...
cmake -G "%VS_VERSION%" -A x64 ^
    -DCMAKE_PREFIX_PATH="%QT_MSVC_PATH%" ^
    -DCMAKE_BUILD_TYPE=%BUILD_MODE% ^
    "%ROOT_DIR%\client"

if %errorlevel% neq 0 (
    echo [错误] CMake 配置失败！
    set "ERRNO=1"
    goto :end
)

echo     编译中...
cmake --build . --config %BUILD_MODE% -j%NUMBER_OF_PROCESSORS%

if %errorlevel% neq 0 (
    echo [错误] 编译失败！
    set "ERRNO=1"
    goto :end
)

echo     客户端构建完成！
echo.

:: ============================================================
:: 构建服务端
:: ============================================================
:build_server
if "%BUILD_SERVER%"=="0" goto :pack_release

echo [3/5] 构建服务端...
echo.

cd /d "%ROOT_DIR%\server"

:: 检测 Gradle
where gradle >nul 2>&1
if %errorlevel% neq 0 (
    :: 使用 gradlew
    if exist "gradlew.bat" (
        echo     使用 gradlew 构建...
        call gradlew.bat assembleRelease
    ) else (
        echo [警告] 未找到 Gradle，跳过服务端构建
        goto :pack_release
    )
) else (
    gradle assembleRelease
)

if %errorlevel% neq 0 (
    echo [错误] 服务端构建失败！
    set "ERRNO=1"
    goto :end
)

:: 复制服务端到 env 目录
if exist "%ROOT_DIR%\server\build\outputs\apk\release\server-release-unsigned.apk" (
    echo     复制服务端到 client/env/...
    copy /y "%ROOT_DIR%\server\build\outputs\apk\release\server-release-unsigned.apk" "%ROOT_DIR%\client\env\scrcpy-server"
)

echo     服务端构建完成！
echo.

:: ============================================================
:: 打包发布
:: ============================================================
:pack_release
if "%PACK_RELEASE%"=="0" goto :done

echo [4/5] 打包发布...
echo.

:: 设置路径
set "OUTPUT_DIR=%ROOT_DIR%\output\GameScrcpy-Windows-x64"
set "BUILD_OUTPUT=%CLIENT_BUILD_DIR%\%BUILD_MODE%"

if exist "%OUTPUT_DIR%" rmdir /s /q "%OUTPUT_DIR%"
mkdir "%OUTPUT_DIR%"

:: 复制主程序
echo     复制主程序...
copy /y "%BUILD_OUTPUT%\GameScrcpy.exe" "%OUTPUT_DIR%\"

:: 复制 ADB
echo     复制 ADB...
copy /y "%ROOT_DIR%\client\env\adb\win\*.*" "%OUTPUT_DIR%\"

:: 复制 scrcpy-server
echo     复制 scrcpy-server...
copy /y "%ROOT_DIR%\client\env\scrcpy-server" "%OUTPUT_DIR%\"

:: 复制 FFmpeg DLL
echo     复制 FFmpeg DLL...
copy /y "%ROOT_DIR%\client\env\ffmpeg\bin\*.dll" "%OUTPUT_DIR%\"

:: 复制 OpenCV DLL (如果存在)
if exist "%ROOT_DIR%\client\env\opencv\build\x64\vc16\bin\*.dll" (
    echo     复制 OpenCV DLL...
    copy /y "%ROOT_DIR%\client\env\opencv\build\x64\vc16\bin\opencv_world*.dll" "%OUTPUT_DIR%\"
)

:: 复制 keymap 目录
echo     复制 keymap...
xcopy /e /i /y "%ROOT_DIR%\keymap" "%OUTPUT_DIR%\keymap\"

:: 复制 config 目录
echo     复制 config...
xcopy /e /i /y "%ROOT_DIR%\config" "%OUTPUT_DIR%\config\"
:: 删除 userdata.ini (用户数据不应该发布)
if exist "%OUTPUT_DIR%\config\userdata.ini" del "%OUTPUT_DIR%\config\userdata.ini"

:: 使用 windeployqt 部署 Qt 依赖
echo     部署 Qt 依赖...
cd /d "%OUTPUT_DIR%"
"%QT_MSVC_PATH%\bin\windeployqt.exe" --release --no-translations GameScrcpy.exe

:: 清理不需要的文件
echo     清理冗余文件...
if exist "%OUTPUT_DIR%\iconengines" rmdir /s /q "%OUTPUT_DIR%\iconengines"
if exist "%OUTPUT_DIR%\translations" rmdir /s /q "%OUTPUT_DIR%\translations"
:: 保留必要的 imageformats
if exist "%OUTPUT_DIR%\imageformats\qgif.dll" del "%OUTPUT_DIR%\imageformats\qgif.dll"
if exist "%OUTPUT_DIR%\imageformats\qicns.dll" del "%OUTPUT_DIR%\imageformats\qicns.dll"
if exist "%OUTPUT_DIR%\imageformats\qico.dll" del "%OUTPUT_DIR%\imageformats\qico.dll"
if exist "%OUTPUT_DIR%\imageformats\qsvg.dll" del "%OUTPUT_DIR%\imageformats\qsvg.dll"
if exist "%OUTPUT_DIR%\imageformats\qtga.dll" del "%OUTPUT_DIR%\imageformats\qtga.dll"
if exist "%OUTPUT_DIR%\imageformats\qtiff.dll" del "%OUTPUT_DIR%\imageformats\qtiff.dll"
if exist "%OUTPUT_DIR%\imageformats\qwbmp.dll" del "%OUTPUT_DIR%\imageformats\qwbmp.dll"
if exist "%OUTPUT_DIR%\imageformats\qwebp.dll" del "%OUTPUT_DIR%\imageformats\qwebp.dll"

:: 删除 vc_redist
if exist "%OUTPUT_DIR%\vc_redist.x64.exe" del "%OUTPUT_DIR%\vc_redist.x64.exe"
if exist "%OUTPUT_DIR%\vc_redist.x86.exe" del "%OUTPUT_DIR%\vc_redist.x86.exe"

echo.
echo     发布包已生成: %OUTPUT_DIR%
echo.

:: ============================================================
:: 完成
:: ============================================================
:done
echo [5/5] 构建完成！
echo.
echo ╔══════════════════════════════════════════════════════════╗
echo ║                    构建成功！                            ║
echo ╚══════════════════════════════════════════════════════════╝
echo.
echo 输出目录:
if "%BUILD_CLIENT%"=="1" echo   客户端: %CLIENT_BUILD_DIR%\%BUILD_MODE%
if "%PACK_RELEASE%"=="1" echo   发布包: %OUTPUT_DIR%
echo.

:end
cd /d "%ROOT_DIR%"
endlocal & exit /b %ERRNO%
