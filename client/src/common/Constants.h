/**
 * @file Constants.h
 * @brief 全局常量定义 / Global Constants Definition
 *
 * Copyright (C) 2019-2026 Rankun
 * Licensed under the Apache License, Version 2.0
 *
 * 定义了项目中使用的各类常量 / Defines various constants used in the project:
 * - 网络参数 (端口、超时、缓冲区大小) / Network params (ports, timeouts, buffer sizes)
 * - 视频参数 (码率、帧率、分辨率) / Video params (bitrate, FPS, resolution)
 * - 控制参数 (触摸点数、消息大小) / Control params (touch points, message size)
 * - UI 参数 (窗口尺寸、动画时长) / UI params (window size, animation duration)
 * - 脚本参数 (超时、缓存大小) / Script params (timeout, cache size)
 */

#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <cstdint>

namespace qsc {
namespace constants {

// =============================================================================
// 网络相关常量
// =============================================================================
namespace network {
    // 默认端口
    constexpr uint16_t DEFAULT_LOCAL_PORT = 27183;
    constexpr uint16_t DEFAULT_LOCAL_PORT_CTRL = 27184;

    // Socket 缓冲区大小
    constexpr int SOCKET_SEND_BUFFER_SIZE = 8 * 1024;       // 8KB
    constexpr int SOCKET_RECV_BUFFER_SIZE = 64 * 1024;      // 64KB

    // 控制消息缓冲区
    constexpr int CONTROL_MAX_PENDING_BYTES = 4 * 1024;     // 4KB 最大积压
    constexpr int CONTROL_WARN_PENDING_BYTES = 2 * 1024;    // 2KB 警告阈值
    constexpr int CONTROL_MAX_QUEUE_SIZE = 2048;            // 最大队列长度

    // 连接超时 (毫秒)
    constexpr int CONNECT_TIMEOUT_MS = 30000;               // 30秒连接超时
    constexpr int SOCKET_READ_TIMEOUT_MS = 5000;            // 5秒读取超时
    constexpr int HANDSHAKE_TIMEOUT_MS = 10000;             // 10秒握手超时

    // 重连参数
    constexpr int RECONNECT_INITIAL_DELAY_MS = 1000;        // 初始重连延迟 1秒
    constexpr int RECONNECT_MAX_DELAY_MS = 30000;           // 最大重连延迟 30秒
    constexpr float RECONNECT_BACKOFF_MULTIPLIER = 1.5f;    // 退避倍数
    constexpr int RECONNECT_MAX_ATTEMPTS = 10;              // 最大重连次数

    // 心跳参数
    constexpr int HEARTBEAT_INTERVAL_MS = 5000;             // 心跳间隔 5秒
    constexpr int HEARTBEAT_TIMEOUT_MS = 15000;             // 心跳超时 15秒
}

// =============================================================================
// 视频相关常量
// =============================================================================
namespace video {
    // 默认参数
    constexpr int DEFAULT_MAX_SIZE = 0;                     // 0表示不限制
    constexpr int DEFAULT_BIT_RATE = 8000000;               // 8Mbps
    constexpr int DEFAULT_MAX_FPS = 60;
    constexpr int DEFAULT_I_FRAME_INTERVAL = 5;             // 5秒 (原为10秒，优化后)

    // 分辨率档位
    constexpr int MAX_SIZE_FALLBACK[] = {2560, 1920, 1600, 1280, 1024, 800};

    // 缓冲区
    constexpr int VIDEO_BUFFER_COUNT = 3;                   // 三缓冲
    constexpr int FRAME_SKIP_THRESHOLD = 3;                 // 跳帧阈值

    // PBO 双缓冲
    constexpr int PBO_COUNT = 2;

    // 纹理上传
    constexpr int TEXTURE_UPLOAD_TIMEOUT_MS = 16;           // 约60fps的帧时间
}

// =============================================================================
// 控制相关常量
// =============================================================================
namespace control {
    // 触摸点
    constexpr int MAX_TOUCH_POINTS = 10;
    constexpr int FAST_TOUCH_MAX_SEQ_ID = 256;

    // 消息大小
    constexpr int CONTROL_MSG_MAX_SIZE = (1 << 18);         // 256KB
    constexpr int CONTROL_MSG_TEXT_MAX_LENGTH = 300;

    // 发送定时器
    constexpr int CONTROL_FLUSH_INTERVAL_MS = 2;            // 2ms 刷新间隔

    // 鼠标移动
    constexpr int MOUSE_MOVE_SEND_INTERVAL_MS = 1;          // 8ms (约125fps)
    constexpr int CURSOR_POS_CHECK_INTERVAL = 50;
}

// =============================================================================
// ADB 相关常量
// =============================================================================
namespace adb {
    // 路径
    constexpr const char* DEFAULT_SERVER_PATH = "/data/local/tmp/scrcpy-server.jar";
    constexpr const char* SOCKET_NAME_PREFIX = "scrcpy";

    // 超时
    constexpr int ADB_COMMAND_TIMEOUT_MS = 10000;           // 10秒命令超时
    constexpr int SERVER_START_TIMEOUT_MS = 30000;          // 30秒服务启动超时

    // 重试
    constexpr int MAX_CONNECT_ATTEMPTS = 30;
    constexpr int MAX_RESTART_COUNT = 1;

    // 设备名长度
    constexpr int DEVICE_NAME_FIELD_LENGTH = 64;
}

// =============================================================================
// UI 相关常量
// =============================================================================
namespace ui {
    // 窗口
    constexpr int MIN_WINDOW_WIDTH = 100;
    constexpr int MIN_WINDOW_HEIGHT = 100;
    constexpr float DEFAULT_WIDTH_HEIGHT_RATIO = 0.5f;

    // FPS 显示
    constexpr int FPS_UPDATE_INTERVAL_MS = 1000;

    // 动画
    constexpr int ANIMATION_DURATION_MS = 200;
    constexpr int FADE_DURATION_MS = 150;

    // 键位编辑
    constexpr int GRID_SNAP_SIZE = 10;                      // 网格吸附大小
    constexpr int KEYMAP_ITEM_MIN_SIZE = 20;
    constexpr int UNDO_STACK_LIMIT = 50;                    // 撤销栈最大深度

    // 历史记录
    constexpr int MAX_IP_HISTORY = 10;
    constexpr int MAX_PORT_HISTORY = 5;
}

// =============================================================================
// 脚本相关常量
// =============================================================================
namespace script {
    // 执行
    constexpr int SCRIPT_EVAL_TIMEOUT_MS = 1000;            // 脚本执行超时
    constexpr int MAX_SCRIPT_CACHE_SIZE = 100;              // 最大缓存脚本数

    // 图像识别
    constexpr double DEFAULT_MATCH_THRESHOLD = 0.8;
    constexpr int MAX_TEMPLATE_CACHE_SIZE = 50;
}

// =============================================================================
// 版本信息
// =============================================================================
namespace version {
    constexpr const char* SCRCPY_SERVER_VERSION = "3.3.4";
    constexpr int PROTOCOL_VERSION = 1;
}

} // namespace constants
} // namespace qsc

#endif // CONSTANTS_H
