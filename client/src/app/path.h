#pragma once

// 路径工具类 / Path Utility Class
// 获取应用程序当前运行目录 / Get the application's current working directory
class Path {
public:
    // 获取当前可执行文件路径 / Get current executable path
    static const char* GetCurrentPath();
};
