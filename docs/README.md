# GameScrcpy 文档 / Documentation

## 使用帮助 / User guide

下面是程序里按 F1 打开的同一套帮助，在 GitHub 上也能直接看（个别图片与程序内的跳转链接只在程序里有效）。
These are the same pages as F1 in the app; a few pictures and in-app links only work inside the app.

| 中文 | English |
|:--|:--|
| [连接手机](../src/app/help/zh_CN/connecting.md) | [Connecting a phone](../src/app/help/en/connecting.md) |
| [投屏窗口与工具栏](../src/app/help/zh_CN/mirror-window.md) | [The mirror window](../src/app/help/en/mirror-window.md) |
| [键位编辑器](../src/app/help/zh_CN/keymap-editor.md) | [The key mapping editor](../src/app/help/en/keymap-editor.md) |
| [方案与分享](../src/app/help/zh_CN/profiles.md) | [Profiles and sharing](../src/app/help/en/profiles.md) |
| [脚本入门](../src/app/help/zh_CN/scripts.md) | [Getting started with scripts](../src/app/help/en/scripts.md) |
| [跟着做的新手引导](../src/app/help/zh_CN/tours.md) | [Guided tours](../src/app/help/en/tours.md) |
| [AOA 触控与 WinUSB 驱动](../src/app/help/zh_CN/aoa-driver.md) | [AOA touch and the WinUSB driver](../src/app/help/en/aoa-driver.md) |
| [触控盒](../src/app/help/zh_CN/touch-box.md) | [Touch boxes](../src/app/help/en/touch-box.md) |
| [采集卡](../src/app/help/zh_CN/capture-card.md) | [Capture cards](../src/app/help/en/capture-card.md) |
| [常见问题](../src/app/help/zh_CN/faq.md) | [Questions and troubleshooting](../src/app/help/en/faq.md) |

## 写逻辑与扩展 / Scripting and extensions

- [扩展开发指南](extensions/README.md) / [Writing extensions](extensions/README.en.md)：扩展能做什么，清单、逻辑与原生接口，测试、打包与发布。
- [脚本接口参考 / Script API reference](../sdk/docs/README.md)：由 `gs api write sdk` 从程序生成，类型声明在 [`sdk/types`](../sdk/types/gamescrcpy.d.ts)。
- [原生扩展的 C 接口 / C interface for native extensions](../sdk/include/gamescrcpy_ext.h)。
- [示例 / Examples](../examples)：组件、任务与扩展（主题、画面滤镜、原生 DLL、按键桥、语音命令、事件记录、引导与帮助）。

## 技术文档 / Technical documents

- [架构概览 / Architecture](architecture.md)
- [通信协议 / Protocol](../protocol/SPEC.md)
- [第三方依赖 / Third-party dependencies](../third_party/README.md)
- [参与开发 / Contributing](../CONTRIBUTING.md)
