# Qt Dependency Audit — UI Layer (`client/src/ui/`)

> **日期**: 2026-03-05
> **范围**: `client/src/ui/` 下所有 `.h` / `.cpp` 文件 (92 个文件)
> **目标**: 找出所有可用 C++17 / Win32 替代的非 Widget Qt 依赖

---

## 1. 分类说明

| 类别 | 含义 | 处理 |
|------|------|------|
| **WIDGET** | QWidget/QDialog/QFrame/QLabel/QPushButton/QComboBox/QListWidget/… | ✅ KEEP |
| **ANIMATION** | QPropertyAnimation/QEasingCurve/QGraphicsOpacityEffect/QParallelAnimationGroup | ✅ KEEP |
| **GRAPHICS** | QGraphicsView/QGraphicsScene/QGraphicsObject/QGraphicsSceneMouseEvent | ✅ KEEP |
| **SYNTAX** | QSyntaxHighlighter/QCompleter + 其配套 QRegularExpression | ✅ KEEP |
| **PAINT** | QPainter/QPainterPath/QPen/QColor/QFont/QPixmap/QIcon/QSvgRenderer | ✅ KEEP |
| **EVENT** | QMouseEvent/QKeyEvent/QWheelEvent/QResizeEvent/QFocusEvent/QShowEvent… | ✅ KEEP |
| **CORE-WIDGET** | QTimer/QPointer/QVariant/QPointF/QRectF/QMap/QList/QSet/QString/QStringList | ✅ KEEP (深度绑定信号槽/Widget) |
| **RESOURCE** | QFile 读 `:/` 资源路径 (Qt Resource System) | ✅ KEEP |
| 🔴 **REPLACEABLE** | QFile(非资源)/QDir/QFileInfo/QTextStream/QJsonDocument/QJsonObject/QJsonArray/QSettings/QDesktopServices/QUrl/QCoreApplication::applicationDirPath/QStandardPaths/QClipboard/QDateTime/QRegularExpression(非syntax) | ❌ 可替换 |

---

## 2. 可替换依赖汇总表

### 2.1 文件 I/O: `QFile` / `QDir` / `QFileInfo` / `QTextStream`

| 文件 | Qt 类 | 用途 | 建议替代 |
|------|-------|------|----------|
| videoform.h:23 | `QFile` | 读写 keymap JSON 文件 | `std::ifstream` / `std::ofstream` |
| videoform.cpp:19 | `QFileInfo` | 检查文件路径 | `std::filesystem::path` |
| videoform.cpp:733 | `QFile` | 读写 `keymap/*.json` | `std::ifstream` / `std::ofstream` |
| toolform.cpp:11 | `QDir` | 目录列表、创建 `keymap/` | `std::filesystem::directory_iterator` + `create_directories` |
| toolform.cpp:13 | `QFile` | 创建/检查 keymap JSON 文件 | `std::ofstream` / `std::filesystem::exists` |
| dialog.cpp:2 | `QFile` | 读写文件、检查 scrcpy-server 存在 | `std::ifstream` / `std::ofstream` / `std::filesystem::exists` |
| dialog.cpp:9 | `QDir` | 目录操作 | `std::filesystem` |
| MainWindow.cpp:46 | `QFile` | 读写文件、检查 scrcpy-server | `std::ifstream` / `std::ofstream` |
| MainWindow.cpp:47 | `QFileInfo` | 文件路径信息 | `std::filesystem::path` |
| scriptbuttonmanager.h:6 | `QFile` | 读写 `buttons.json` | `std::ifstream` / `std::ofstream` |
| scriptbuttonmanager.h:7 | `QDir` | 创建 keymap 目录 | `std::filesystem::create_directories` |
| scriptswipemanager.h:6 | `QFile` | 读写 `swipes.json` | `std::ifstream` / `std::ofstream` |
| scriptswipemanager.h:7 | `QDir` | 创建 keymap 目录 | `std::filesystem::create_directories` |
| selectionregionmanager.h:6 | `QFile` | 读写 `regions.json` | `std::ifstream` / `std::ofstream` |
| selectionregionmanager.h:7 | `QDir` | 创建 keymap 目录 | `std::filesystem::create_directories` |
| KeyMapItems.h:27 | `QDir` | 脚本目录操作 | `std::filesystem` |
| KeyMapItems.h:28 | `QFile` | 读写文件 | `std::ifstream` / `std::ofstream` |
| KeyMapItems.h:31 | `QTextStream` | 文本流写入 | `std::ofstream` |
| scripteditordialog.h:13 | `QDir` | 脚本目录创建/检查 | `std::filesystem` |
| selectioneditordialog.h:29 | `QDir` | 模板目录创建 | `std::filesystem::create_directories` |
| imagecapturedialog.h:17 | `QDir` | 截图保存目录 | `std::filesystem` |
| KeyMapSidePanel.cpp:24 | `QDir` | 列出 keymap 目录 | `std::filesystem::directory_iterator` |
| KeyMapSidePanel.cpp:25 | `QFile` | 读写 keymap 文件 | `std::ifstream` / `std::ofstream` |
| DeviceDetailPage.cpp:22 | `QDir` | keymap 目录列表/创建 | `std::filesystem` |
| DeviceDetailPage.cpp:25 | `QFile` | 创建 keymap JSON | `std::ofstream` |
| ThemeManager.cpp:10 | `QFile` | 读取 `:/theme/fluent.qss` | ✅ **KEEP** — Qt 资源系统路径 |

### 2.2 JSON: `QJsonDocument` / `QJsonObject` / `QJsonArray`

| 文件 | Qt 类 | 用途 | 建议替代 |
|------|-------|------|----------|
| videoform.h:20-22 | `QJsonDocument` / `QJsonObject` / `QJsonArray` | keymap 配置读写 (解析/序列化) | `nlohmann::json` (项目已引入) |
| videoform.cpp:765 | `QJsonDocument::fromJson()` | 解析 keymap JSON | `nlohmann::json::parse()` |
| videoform.h:153 | `QJsonObject m_currentConfigBase` 成员 | 存储当前配置 | `nlohmann::json` |
| KeyMapBase.h:8 | `QJsonObject` | `toJson()` / `fromJson()` 虚接口 | `nlohmann::json` (需同步修改所有子类) |
| KeyMapItems.h (多处) | `QJsonObject` | 所有 KeyMapItem 子类的序列化接口 | `nlohmann::json` (约 15 处 toJson/fromJson) |

> ⚠️ **注意**: `KeyMapBase::toJson()` / `fromJson()` 是虚接口，修改影响 `KeyMapItemSteerWheel`、`KeyMapItemClick`、`KeyMapItemDrag`、`KeyMapItemScript` 等全部子类。建议统一迁移到 `nlohmann::json`，与三个 Manager 类保持一致。

### 2.3 应用路径: `QCoreApplication::applicationDirPath()` / `QStandardPaths`

| 文件 | Qt API | 用途 | 建议替代 |
|------|--------|------|----------|
| scriptbuttonmanager.h:62 | `QCoreApplication::applicationDirPath()` | 拼接 `buttons.json` 路径 | Win32 `GetModuleFileNameW` + `std::filesystem::path::parent_path()` |
| scriptswipemanager.h:70 | `QCoreApplication::applicationDirPath()` | 拼接 `swipes.json` 路径 | 同上 |
| selectionregionmanager.h:71 | `QCoreApplication::applicationDirPath()` | 拼接 `regions.json` 路径 | 同上 |
| scripteditordialog.h:1005 | `QCoreApplication::applicationDirPath()` | 拼接 `scripts/` 路径 | 同上 |
| dialog.cpp:46 | `QCoreApplication::applicationDirPath()` | keymap 路径、scrcpy-server 路径 | 同上 |
| MainWindow.cpp:68 | `QCoreApplication::applicationDirPath()` | keymap 路径、scrcpy-server 路径 | 同上 |
| dialog.cpp:8 | `QStandardPaths::writableLocation(TempLocation)` | 临时目录 (提取 scrcpy-server) | `std::filesystem::temp_directory_path()` |
| MainWindow.cpp:49 | `QStandardPaths::writableLocation(TempLocation)` | 临时目录 | `std::filesystem::temp_directory_path()` |

### 2.4 桌面服务: `QDesktopServices` / `QUrl`

| 文件 | Qt API | 用途 | 建议替代 |
|------|--------|------|----------|
| toolform.cpp:19-20 | `QDesktopServices::openUrl(QUrl::fromLocalFile(...))` | 打开 keymap 文件夹 | Win32 `ShellExecuteW(nullptr, L"open", path, ...)` |
| scripteditordialog.h:14-15 | `QDesktopServices::openUrl(QUrl::fromLocalFile(...))` | 打开脚本目录 | `ShellExecuteW` |
| selectioneditordialog.h:20-21 | `QDesktopServices::openUrl(QUrl::fromLocalFile(...))` | 打开模板目录 | `ShellExecuteW` |
| KeyMapItems.h:29-30 | `QDesktopServices::openUrl(QUrl::fromLocalFile(...))` | 打开目录 | `ShellExecuteW` |
| KeyMapSidePanel.cpp:28-29 | `QDesktopServices::openUrl(QUrl::fromLocalFile(...))` | 打开 keymap 文件夹 | `ShellExecuteW` |
| DeviceDetailPage.cpp:23-24 | `QDesktopServices::openUrl(QUrl::fromLocalFile(...))` | 打开 keymap 文件夹 | `ShellExecuteW` |

### 2.5 时间: `QDateTime`

| 文件 | Qt API | 用途 | 建议替代 |
|------|--------|------|----------|
| ScriptTipWidget.cpp:3 | `QDateTime::currentMSecsSinceEpoch()` | 消息创建时间戳 | `std::chrono::steady_clock::now()` |
| PerformanceDialog.cpp:7 | `QDateTime::currentMSecsSinceEpoch()` | 性能指标时间戳 | `std::chrono::steady_clock::now()` |
| ActivityLog.h:12 | `QDateTime` 成员 (`Entry::time`) | 日志条目时间显示 | `std::chrono::system_clock::time_point` + `std::format` |
| videoform.cpp:27 | `#include <QDateTime>` | **未使用 (死代码)** | 直接删除 |

### 2.6 正则表达式: `QRegularExpression`

| 文件 | Qt API | 用途 | 建议替代 |
|------|--------|------|----------|
| dialog.cpp:18 | `QRegularExpression` | IP 地址验证 | `std::regex` |
| MainWindow.cpp:55 | `QRegularExpression` | IP 地址验证 | `std::regex` |
| scripteditordialog.h:23 | `QRegularExpression` | QSyntaxHighlighter 规则 | ✅ **KEEP** — 与 QSyntaxHighlighter 配套使用 |

### 2.7 设置: `QSettings`

| 文件 | Qt API | 用途 | 建议替代 |
|------|--------|------|----------|
| ScriptTipWidget.h:13 | `QSettings` | 保存/读取 ScriptTip 窗口位置 | Win32 Registry API 或 `std::fstream` + INI/JSON |
| ScriptTipWidget.cpp:515 | `QSettings("QtScrcpy", "ScriptTip")` | 写入位置设置 | 同上 |
| ScriptTipWidget.cpp:522 | `QSettings("QtScrcpy", "ScriptTip")` | 读取位置设置 | 同上 |

### 2.8 剪贴板: `QClipboard`

| 文件 | Qt API | 用途 | 建议替代 |
|------|--------|------|----------|
| imagecapturedialog.h:22 | `QClipboard` | 复制截图到剪贴板 | Win32 `OpenClipboard` + `SetClipboardData(CF_DIB, ...)` |
| selectioneditordialog.h:25 | `QClipboard` | 复制裁剪图片到剪贴板 | 同上 |

---

## 3. 三大 Manager 类专项分析

### 3.1 `ScriptButtonManager` — scriptbuttonmanager.h

| 依赖 | 状态 | 说明 |
|------|------|------|
| `nlohmann::json` | ✅ 已迁移 | `toJson()` / `fromJson()` 使用 nlohmann |
| `std::shared_mutex` | ✅ 已迁移 | 线程安全用 `<shared_mutex>` |
| `QObject` 继承 | ❌ 未继承 | 纯数据管理类，无信号槽 |
| 🔴 `QFile` | 待替换 | `load()` / `save()` 用于读写 `buttons.json` |
| 🔴 `QDir` | 待替换 | `save()` 中创建目录 |
| 🔴 `QCoreApplication::applicationDirPath()` | 待替换 | `configPath()` / `configDir()` |
| 🔴 `QString` / `QVector` | 深度耦合 | 全类使用，短期不改 (可后续迁移到 `std::string` / `std::vector`) |

### 3.2 `ScriptSwipeManager` — scriptswipemanager.h

| 依赖 | 状态 | 说明 |
|------|------|------|
| `nlohmann::json` | ✅ 已迁移 | |
| `std::shared_mutex` | ✅ 已迁移 | |
| `QObject` 继承 | ❌ 未继承 | |
| 🔴 `QFile` | 待替换 | `load()` / `save()` |
| 🔴 `QDir` | 待替换 | `save()` 中创建目录 |
| 🔴 `QCoreApplication::applicationDirPath()` | 待替换 | `configPath()` / `configDir()` |

### 3.3 `SelectionRegionManager` — selectionregionmanager.h

| 依赖 | 状态 | 说明 |
|------|------|------|
| `nlohmann::json` | ✅ 已迁移 | |
| `std::shared_mutex` | ✅ 已迁移 | |
| `QObject` 继承 | ❌ 未继承 | |
| 🔴 `QFile` | 待替换 | `load()` / `loadFromFile()` / `save()` |
| 🔴 `QDir` | 待替换 | `save()` 中创建目录 |
| 🔴 `QCoreApplication::applicationDirPath()` | 待替换 | `configPath()` / `configDir()` |
| 🔴 `QRectF` | 深度耦合 | UI 几何类型，与 QWidget 配合使用 — 暂 KEEP |

---

## 4. 可替换依赖出现频次统计

| Qt 类 | 出现文件数 | 替代方案 |
|-------|-----------|----------|
| `QFile` (非资源) | **14** | `std::ifstream` / `std::ofstream` / `std::filesystem::exists` |
| `QDir` | **12** | `std::filesystem::directory_iterator` / `create_directories` / `path` |
| `QJsonObject` / `QJsonDocument` / `QJsonArray` | **3** (videoform, KeyMapBase, KeyMapItems) | `nlohmann::json` (已在项目中) |
| `QCoreApplication::applicationDirPath()` | **6** | Win32 `GetModuleFileNameW` + `std::filesystem::path::parent_path()` |
| `QDesktopServices` + `QUrl` | **6** | Win32 `ShellExecuteW(nullptr, L"open", ...)` |
| `QDateTime` | **4** (1处死代码) | `std::chrono::steady_clock` / `system_clock` |
| `QRegularExpression` (非syntax) | **2** | `std::regex` |
| `QStandardPaths` | **2** | `std::filesystem::temp_directory_path()` |
| `QSettings` | **1** | Win32 Registry 或 `std::fstream` + INI |
| `QClipboard` | **2** | Win32 Clipboard API |
| `QFileInfo` | **3** | `std::filesystem::path` / `exists()` / `file_size()` |
| `QTextStream` | **1** | `std::ofstream` / `std::ostringstream` |

---

## 5. 不可替换 (KEEP) 汇总 — 确认为 Widget/动画/绘制/事件绑定

以下 Qt 依赖遍布 UI 层，**全部保留**：

- **Widget 系**: `QWidget`, `QDialog`, `QFrame`, `QLabel`, `QPushButton`, `QComboBox`, `QLineEdit`, `QTextEdit`, `QPlainTextEdit`, `QCheckBox`, `QSpinBox`, `QGroupBox`, `QListWidget`, `QStackedWidget`, `QScrollArea`, `QScrollBar`, `QSlider`, `QSplitter`, `QInputDialog`, `QMessageBox`, `QFileDialog`, `QSystemTrayIcon`, `QMenu`, `QAction`, `QToolButton`, `QTextBrowser`, `QProgressBar`, `QRubberBand`
- **动画系**: `QPropertyAnimation`, `QParallelAnimationGroup`, `QEasingCurve`, `QGraphicsOpacityEffect`, `QGraphicsDropShadowEffect`
- **绘制系**: `QPainter`, `QPainterPath`, `QPen`, `QColor`, `QFont`, `QFontDatabase`, `QPixmap`, `QIcon`, `QSvgRenderer`, `QPaintEvent`, `QStyleOption`
- **图形视图**: `QGraphicsView`, `QGraphicsScene`, `QGraphicsObject`, `QGraphicsSceneMouseEvent`
- **语法高亮**: `QSyntaxHighlighter` + `QRegularExpression` (scripteditordialog.h 中)
- **补全**: `QCompleter`, `QStringListModel`, `QAbstractItemView`
- **事件**: `QMouseEvent`, `QKeyEvent`, `QWheelEvent`, `QResizeEvent`, `QMoveEvent`, `QFocusEvent`, `QShowEvent`, `QCloseEvent`, `QDragEnterEvent`, `QDropEvent`
- **Core-Widget 绑定**: `QTimer`, `QPointer`, `QVariant`, `QPointF`, `QRectF`, `QMap`, `QList`, `QSet`, `QHash`, `QString`, `QStringList`, `QByteArray`, `QMetaEnum`, `QEventLoop`, `QThread`, `QShortcut`, `QCursor`, `QKeySequence`, `QScreen`, `QRandomGenerator`, `QDebug`, `QMimeData`, `QDrag`, `QUndoStack`, `QUndoCommand`, `QSharedPointer`, `QVector`, `QIntValidator`, `QDoubleValidator`, `QApplication`
- **QImage (显示上下文)**: imagecapturedialog.h / selectioneditordialog.h / scripteditordialog.h / videoform.cpp — 所有 QImage 用途均与 UI 渲染 (QPainter/QLabel) 或 QPixmap 转换直接关联 → **KEEP**
- **QFile (资源路径)**: ThemeManager.cpp 读取 `:/theme/fluent.qss` → **KEEP** (Qt Resource System)

---

## 6. 推荐迁移优先级

### P0 — 三大 Manager 类 (最简单，无 Widget 耦合)
- `ScriptButtonManager` / `ScriptSwipeManager` / `SelectionRegionManager`
- 已完成: nlohmann::json ✅, std::shared_mutex ✅
- **待做**: `QFile` → `std::ifstream`/`std::ofstream`, `QDir` → `std::filesystem`, `applicationDirPath()` → `GetModuleFileNameW`
- 工作量: 每类约 3 处 file I/O + 2 处路径

### P1 — `QDesktopServices::openUrl` (6 处, 统一替换)
- 提取公共函数: `void openFolderInExplorer(const std::filesystem::path& p)`
- 实现: `ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL)`

### P2 — `QDateTime` → `std::chrono` (3 处有效 + 1 处死代码)
- ScriptTipWidget.cpp: `currentMSecsSinceEpoch()` → `steady_clock`
- PerformanceDialog.cpp: 同上
- ActivityLog.h/cpp: `QDateTime` 成员 → `system_clock::time_point`
- videoform.cpp: 删除死包含

### P3 — `QRegularExpression` (非syntax) → `std::regex` (2 处)
- dialog.cpp / MainWindow.cpp: IP 验证

### P4 — `QSettings` → Win32 Registry 或 INI (1 处)
- ScriptTipWidget.cpp: 窗口位置存取

### P5 — `QStandardPaths` → `std::filesystem::temp_directory_path()` (2 处)

### P6 — `QClipboard` → Win32 Clipboard API (2 处)
- 注意: 需将 QImage → HBITMAP/CF_DIB 转换

### P7 — QJson 系 (videoform + KeyMapBase/KeyMapItems) — 最大工作量
- `KeyMapBase::toJson()/fromJson()` 是虚接口，涉及 4+ 子类约 15 处
- 需与 P0 manager 类对齐到 `nlohmann::json`

---

## 7. 无可替换依赖的文件 (纯 Widget 文件)

以下文件仅包含 Widget/Animation/Paint/Event 类型的 Qt 依赖，**无需修改**：

| 文件 | 说明 |
|------|------|
| pages/HomePage.h/.cpp | 纯 Widget 布局 |
| pages/SettingsPage.h/.cpp | 纯 Widget 布局 |
| pages/TerminalPage.h/.cpp | 纯 Widget 布局 |
| settingsdialog.h/.cpp | 纯 Widget 布局 |
| terminaldialog.h/.cpp | 纯 Widget 布局 |
| ConnectionProgressWidget.h/.cpp | Widget + 自定义 ElapsedTimer (已无 QElapsedTimer) |
| KeyMapOverlay.h/.cpp | Widget + Paint |
| KeyMapEditView.h/.cpp | GraphicsView + UndoStack |
| keymap/KeyMapPropertyPanel.h/.cpp | Widget + Animation |
| keymap/KeyConflictIndicator.h/.cpp | Widget + Animation |
| components/FluentButton.h/.cpp | Widget |
| components/FluentBadge.h/.cpp | Widget + Paint |
| components/FluentCard.h/.cpp | Widget |
| components/FluentComboBox.h/.cpp | Widget + Animation |
| components/FluentDialog.h/.cpp | Widget + Animation |
| components/FluentInfoBar.h/.cpp | Widget + Animation |
| components/FluentInput.h/.cpp | Widget + Animation |
| components/FluentProgressRing.h/.cpp | Widget + Animation |
| components/FluentSlider.h/.cpp | Widget + Animation |
| components/FluentToggle.h/.cpp | Widget + Animation |
| components/FluentToolWindow.h/.cpp | Widget + Animation |
| components/DeviceCard.h/.cpp | Widget |
| components/HelpDialog.h/.cpp | Widget |
| components/NavigationView.h/.cpp | Widget + Animation |
| components/OnboardingOverlay.h/.cpp | Widget + Animation |
| components/SettingRow.h/.cpp | Widget |
| components/VideoBottomBar.h/.cpp | Widget + Paint |
| components/VideoSettingsPopup.h/.cpp | Widget + Animation |
| widgets/iconhelper.h/.cpp | Widget + Font |
| widgets/keepratiowidget.h/.cpp | Widget |
| widgets/magneticwidget.h/.cpp | Widget |
| theme/DesignTokens.h | QColor + QString (设计令牌) |
| theme/ThemeManager.h/.cpp | QObject + QColor + QFile(资源) (主题管理) |
| theme/MotionTokens.h | Animation helpers |
