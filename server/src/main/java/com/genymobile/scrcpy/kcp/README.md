# KCP 协议实现 (全新重构版本)

## 概述

这是基于 [skywind3000/kcp](https://github.com/skywind3000/kcp) 的 **全新重构** 实现。

严格按照 `kcp-master/test.cpp` 和 `README.md` 的使用方式实现，完全抛弃旧框架。

KCP 是一个快速可靠的 ARQ 协议，以比 TCP 多消耗 10%-20% 带宽的代价，
换取平均延迟降低 30%-40%，且最大延迟降低三倍的传输效果。

## 文件结构

### 服务端 (Java/Android)

```
server/src/main/java/com/genymobile/scrcpy/kcp/
├── KcpCore.java        # KCP 协议核心实现 (严格按照 ikcp.c)
├── KcpTransport.java   # UDP 传输层 + KCP 定时更新
├── KcpVideoSender.java # 视频流发送器
└── KcpControlChannel.java  # 控制命令通道
```

### 客户端 (C++/Qt)

```
QtScrcpy/QtScrcpyCore/src/kcp/
├── ikcp.h/c           # 原始 KCP 实现 (来自 skywind3000/kcp)
├── KcpCore.h/cpp      # KCP 核心封装 (对 ikcp 的直接封装)
├── KcpTransport.h/cpp # UDP 传输层 + 定时更新
└── KcpClient.h/cpp    # 客户端接口 (视频接收/控制通道)
```

## 基本使用 (参考 test.cpp)

### C++ 客户端

```cpp
// 创建传输层 (参考 test.cpp: ikcp_create)
KcpTransport transport(0x11223344);  // conv 必须与服务端相同

// 配置快速模式 (参考 test.cpp mode=2)
transport.setFastMode();
transport.setWindowSize(128, 128);

// 连接到远端
transport.connectTo(QHostAddress("192.168.1.100"), 27185);

// 发送数据 (参考 test.cpp: ikcp_send)
transport.send(data, len);

// 接收数据 (参考 test.cpp: ikcp_recv)
connect(&transport, &KcpTransport::dataReady, [&]() {
    QByteArray data = transport.recv();
    processData(data);
});
```

### Java 服务端

```java
// 创建传输层
KcpTransport transport = new KcpTransport(0x11223344);
transport.setFastMode();

// 连接
transport.connect("192.168.1.100", 27185);

// 发送数据
transport.send(data);

// 接收数据 (通过 Listener)
transport.setListener(new KcpTransport.Listener() {
    @Override
    public void onReceive(byte[] data, int offset, int len) {
        processData(data, offset, len);
    }
});
```

## 配置参数 (参考 test.cpp)

### 快速模式 (mode=2, 推荐用于游戏/投屏)

```cpp
// 参考 test.cpp:
// ikcp_nodelay(kcp1, 2, 10, 2, 1);
// kcp1->rx_minrto = 10;
// kcp1->fastresend = 1;
// ikcp_wndsize(kcp1, 128, 128);

transport.setFastMode();  // 内部调用上述配置
```

配置说明:
- `nodelay=2`: 激进模式，RTO 不翻倍，改为 x1.5
- `interval=10`: 10ms 更新间隔
- `resend=2`: 2次 ACK 跨越直接重传
- `nc=1`: 关闭拥塞控制
- `rx_minrto=10`: 最小 RTO 10ms
- `window=128x128`: 发送/接收窗口

### 普通模式 (mode=1)

```cpp
// 参考 test.cpp:
// ikcp_nodelay(kcp1, 0, 10, 0, 1);

transport.setNormalMode();
```

### 默认模式 (mode=0, 类似 TCP)

```cpp
// 参考 test.cpp:
// ikcp_nodelay(kcp1, 0, 10, 0, 0);

transport.setDefaultMode();
```

## 会话 ID (conv)

通信双方必须使用相同的 conv 值：

- 视频通道: `0x11223344` (KcpTransport.CONV_VIDEO)
- 控制通道: `0x22334455` (KcpTransport.CONV_CONTROL)

## 性能对比 (来自 test.cpp 测试结果)

| 模式 | 平均 RTT | 最大 RTT | 总耗时 |
|------|---------|---------|--------|
| default (TCP-like) | 740ms | 1507ms | 20917ms |
| normal (关闭流控) | 156ms | 571ms | 20131ms |
| **fast (推荐)** | **138ms** | **392ms** | 20207ms |

## 核心API (参考 kcp-master/README.md)

```cpp
// 创建/释放
ikcpcb* ikcp_create(conv, user);
void ikcp_release(kcp);

// 数据收发
int ikcp_send(kcp, data, len);
int ikcp_recv(kcp, buffer, len);

// 底层协议交互
int ikcp_input(kcp, data, size);
void ikcp_update(kcp, current_ms);

// 配置
void ikcp_nodelay(kcp, nodelay, interval, resend, nc);
void ikcp_wndsize(kcp, sndwnd, rcvwnd);
int ikcp_setmtu(kcp, mtu);

// 状态
int ikcp_peeksize(kcp);
int ikcp_waitsnd(kcp);
```

## 参考资料

- [KCP 协议文档](https://github.com/skywind3000/kcp/blob/master/README.md)
- [KCP 最佳实践](https://github.com/skywind3000/kcp/wiki/KCP-Best-Practice)
- [test.cpp](../../../../kcp-master/test.cpp) - 测试用例参考
- [ikcp.h](../../../../kcp-master/ikcp.h) - API 定义
