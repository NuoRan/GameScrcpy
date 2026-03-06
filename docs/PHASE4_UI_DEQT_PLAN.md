# Phase 4: UI 层去 Qt 非界面依赖执行计划

> **目标**: 将 UI 层(`client/src/ui/`)中所有非界面功能的 Qt 依赖替换为 C++17 标准库 + Win32 API
> **约束**: 保留所有 QWidget/QDialog/QGraphicsView/QPropertyAnimation 等界面功能
> **起始状态**: Phase 3 (Steps 30-54) 已完成，非 UI 层已零 Qt 依赖

---

## 审计结果概要

| 类别 | 可直接移除(未使用) | 可替换为 C++ | 保留(Widget层) |
|------|-------------------|-------------|---------------|
| QFile/QFileInfo | 3 处 | 8 处 | 1 处(QRC资源) |
| QDir | 2 处 | 9 处 | — |
| QJsonDocument/Object/Array | — | 2 处(影响大量代码) | — |
| QDesktopServices/QUrl | 1 处 | 5 处 | — |
| QDateTime | 1 处 | 3 处 | — |
| QRegularExpression | — | 2 处 | 1 处(语法高亮) |
| QSettings | — | 1 处 | — |
| QStandardPaths | — | 2 处 | — |
| QTextStream | 1 处 | — | — |
| QCoreApplication::applicationDirPath | — | 6 处 | — |
| QImage | 1 处 | — | 2 处 |
| QSvgRenderer | 1 处 | — | 2 处 |
| **合计** | **9 处** | **38 处** | **6 处** |

---

## 执行顺序

```
Step 55: 清除未使用的 Qt includes (9处，零风险)
Step 56: QCoreApplication::applicationDirPath → strutil::appDirPath (6处)
Step 57: QDateTime → std::chrono (3处)
Step 58: QStandardPaths → std::filesystem::temp_directory_path (2处)
Step 59: QRegularExpression → std::regex (2处，非语法高亮)
Step 60: QDesktopServices/QUrl → ShellExecuteW (5处)
Step 61: QSettings → ConfigCenter (1处)
Step 62: 3个Manager类 QFile/QDir → std::filesystem (3文件)
Step 63: QFile/QDir → std::filesystem (UI主文件8处)
Step 64: QJsonDocument/Object/Array → nlohmann::json (videoform + KeyMapBase + 子类)
Step 65: 编译验证 + 最终审计
```

---

## Step 55: 清除未使用的 Qt includes

> **风险**: ⭐ (零) | **预计耗时**: 5min

| 文件 | 移除的 include |
|------|---------------|
| videoform.cpp | `#include <QFileInfo>`, `#include <QDateTime>` |
| KeyMapItems.h | `#include <QFile>`, `#include <QDir>`, `#include <QTextStream>`, `#include <QDesktopServices>`, `#include <QUrl>` |
| imagecapturedialog.h | `#include <QDir>` |
| scripteditordialog.h | `#include <QImage>` |
| HelpDialog.h | `#include <QSvgRenderer>` |

---

## Step 56: QCoreApplication::applicationDirPath → strutil::appDirPath

> **风险**: ⭐ | **预计耗时**: 15min

项目已有 `StringUtils.h` 中的 `strutil::appDirPath()` 返回 `std::string`。
UI 层需要 QString，使用 `strutil::toQ(strutil::appDirPath())` 替代。

| 文件 | 行号 | 当前代码 | 替换为 |
|------|------|---------|--------|
| MainWindow.cpp | L68 | `QCoreApplication::applicationDirPath() + "/keymap"` | `strutil::toQ(strutil::appDirPath() + "/keymap")` |
| MainWindow.cpp | L929 | `QCoreApplication::applicationDirPath() + "/scrcpy-server"` | `strutil::toQ(strutil::appDirPath() + "/scrcpy-server")` |
| dialog.cpp | L46 | `QCoreApplication::applicationDirPath() + "/keymap"` | `strutil::toQ(strutil::appDirPath() + "/keymap")` |
| dialog.cpp | L1177 | `QCoreApplication::applicationDirPath() + "/scrcpy-server"` | `strutil::toQ(strutil::appDirPath() + "/scrcpy-server")` |
| scriptbuttonmanager.h | L62,66 | `QCoreApplication::applicationDirPath() + "/keymap/..."` | `strutil::toQ(strutil::appDirPath() + "/keymap/...")` |
| scriptswipemanager.h | L70,74 | 同上 | 同上 |
| selectionregionmanager.h | L71,76 | 同上 | 同上 |
| scripteditordialog.h | L1005 | 同上 | 同上 |

---

## Step 57: QDateTime → std::chrono

> **风险**: ⭐ | **预计耗时**: 10min

| 文件 | 用法 | 替换 |
|------|------|------|
| ScriptTipWidget.cpp | `QDateTime::currentMSecsSinceEpoch()` (4处) | `std::chrono::steady_clock::now()` 毫秒计算 |
| PerformanceDialog.cpp | `QDateTime::currentMSecsSinceEpoch()` (1处) | 同上 |
| ActivityLog.h/cpp | `QDateTime::currentDateTime()` (显示时间) | `std::chrono::system_clock::now()` + 格式化 |

---

## Step 58: QStandardPaths → std::filesystem

> **风险**: ⭐ | **预计耗时**: 5min

| 文件 | 用法 | 替换 |
|------|------|------|
| dialog.cpp | `QStandardPaths::writableLocation(TempLocation)` | `std::filesystem::temp_directory_path().string()` |
| MainWindow.cpp | 同上 | 同上 |

---

## Step 59: QRegularExpression → std::regex

> **风险**: ⭐⭐ | **预计耗时**: 10min

| 文件 | 用法 | 替换 |
|------|------|------|
| dialog.cpp | IP 地址正则验证 | `std::regex` + `std::regex_match` |
| MainWindow.cpp | IP 地址正则验证 (同样代码) | 同上 |

注: scripteditordialog.h 中的 QRegularExpression 保留（与 QSyntaxHighlighter 深度绑定）

---

## Step 60: QDesktopServices/QUrl → ShellExecuteW

> **风险**: ⭐⭐ | **预计耗时**: 15min

创建 `common/PlatformUtils.h` 中的 `openFolder()` 函数:
```cpp
namespace platform {
    inline void openFolder(const std::string& path) {
        ShellExecuteW(nullptr, L"open", strutil::toWide(path).c_str(), nullptr, nullptr, SW_SHOW);
    }
}
```

替换 5 处 `QDesktopServices::openUrl(QUrl::fromLocalFile(...))` 调用。

---

## Step 61: QSettings → ConfigCenter

> **风险**: ⭐ | **预计耗时**: 5min

ScriptTipWidget 中使用 `QSettings("QtScrcpy","ScriptTip")` 保存窗口位置，改为使用项目已有的 `ConfigCenter`。

---

## Step 62: 3个Manager类 QFile/QDir → std::filesystem

> **风险**: ⭐⭐ | **预计耗时**: 20min

scriptbuttonmanager.h, scriptswipemanager.h, selectionregionmanager.h:
- `QFile` → `std::ifstream`/`std::ofstream`
- `QDir().mkpath()` → `std::filesystem::create_directories()`
- 移除 `#include <QFile>`, `#include <QDir>`, `#include <QCoreApplication>`

---

## Step 63: 其余UI文件 QFile/QDir → std::filesystem

> **风险**: ⭐⭐ | **预计耗时**: 30min

| 文件 | 改动 |
|------|------|
| videoform.cpp | `QFile file("keymap/...")` → `std::ifstream` |
| toolform.cpp | `QFile::exists()`, `QFile file(...)`, `QDir dir(...)` → `std::filesystem` |
| DeviceDetailPage.cpp | `QDir dir("keymap")`, `QFile f(...)` → `std::filesystem` |
| KeyMapSidePanel.cpp | `QDir dir("keymap")`, `QFile file(...)` → `std::filesystem` |
| dialog.cpp | `QFileInfo`, `QFile::exists()`, `QFile` → `std::filesystem` + `std::fstream` |
| MainWindow.cpp | 同 dialog.cpp (提取 scrcpy-server) |
| selectioneditordialog.h | `QDir d(dir)` → `std::filesystem::create_directories()` |
| scripteditordialog.h | `QDir dir(path)` → `std::filesystem::create_directories()` |

注: ThemeManager.cpp 中的 `QFile f(":/...")` 保留（读 QRC 资源需要 QFile）

---

## Step 64: QJsonDocument/Object/Array → nlohmann::json

> **风险**: ⭐⭐⭐⭐ | **预计耗时**: 2h

最大改动。影响:
- videoform.h/cpp — `QJsonObject m_currentConfigBase` 成员，JSON 解析/序列化
- KeyMapBase.h — `virtual QJsonObject toJson()` / `virtual void fromJson(const QJsonObject&)` 虚接口
- KeyMapItems.h — 5 个子类的 toJson/fromJson 实现
- KeyMapEditView.cpp — JSON 序列化
- toolform.cpp — JSON 写入
- DeviceDetailPage.cpp — JSON 写入
- KeyMapSidePanel.cpp — JSON 写入

全部改用 `nlohmann::json`（项目已有此依赖）。

---

## Step 65: 编译验证 + 最终审计

> **风险**: ⭐ | **预计耗时**: 15min

1. 全量编译验证 0 errors
2. grep 扫描 ui/ 目录残留的非界面 Qt includes
3. 更新本文档进度

---

## 执行进度追踪

| Step | 内容 | 状态 | 完成日期 |
|------|------|------|----------|
| 55 | 清除未使用 Qt includes (9处) | ✅ | 2025-01 |
| 56 | applicationDirPath → appDirPath (6处) | ✅ | 2025-01 |
| 57 | QDateTime → std::chrono (3处) | ✅ | 2025-01 |
| 58 | QStandardPaths → std::filesystem (2处) | ✅ | 2025-01 |
| 59 | QRegularExpression → std::regex (2处) | ✅ | 2025-01 |
| 60 | QDesktopServices → ShellExecuteW (5处) | ✅ | 2025-01 |
| 61 | QSettings → ConfigCenter (1处) | ✅ | 2025-01 |
| 62 | 3个Manager QFile/QDir → std::filesystem | ✅ | 2025-01 |
| 63 | UI主文件 QFile/QDir → std::filesystem | ✅ | 2025-01 |
| 64 | QJson → nlohmann::json (大改动) | ✅ | 2025-01 |
| 65 | 编译验证 + 最终审计 | ✅ | 2025-01 |

---

## 最终审计结果

**编译**: 10/10 编译单元 + 链接 ✅ 零错误

**残留 Qt 非 widget includes (合理保留)**:
| 文件 | include | 保留原因 |
|------|---------|----------|
| MainWindow.cpp | `<QFile>` | QRC 资源访问 `QFile(":/scrcpy-server")` |
| dialog.cpp | `<QFile>` | QRC 资源访问 `QFile(":/scrcpy-server")` |
| ThemeManager.cpp | `<QFile>` | QRC 资源访问 `QFile(":/theme/fluent.qss")` |
| selectioneditordialog.h | `<QSvgRenderer>` | Qt SVG 渲染组件 |
| HomePage.cpp | `<QSvgRenderer>` | Qt SVG 渲染组件 |
| NavigationView.cpp | `<QSvgRenderer>` | Qt SVG 渲染组件 |
| scripteditordialog.h | `<QRegularExpression>` | QSyntaxHighlighter 语法高亮 |

**CMake Qt 依赖**: `Qt::Widgets, Qt::Gui, Qt::Svg, Qt::SvgWidgets` (纯界面功能)
