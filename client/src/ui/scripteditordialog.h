#ifndef SCRIPTEDITORDIALOG_H
#define SCRIPTEDITORDIALOG_H

#include <QDialog>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QLabel>
#include <QCoreApplication>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QGroupBox>
#include <QGridLayout>
#include <QToolButton>
#include <QMenu>
#include <QScrollArea>
#include <QImage>
#include <functional>

#include "imagecapturedialog.h"

// 帧获取回调类型
using FrameGrabFunc = std::function<QImage()>;

// ---------------------------------------------------------
// 脚本编辑对话框
// 用于编写或导入 KeyMap 脚本 (JavaScript)
// 带有快捷指令面板，方便插入常用代码
// ---------------------------------------------------------
class ScriptEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ScriptEditorDialog(const QString& script, QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle("脚本编辑器");
        resize(850, 550);

        // 初始化图像截取对话框
        m_imageCaptureDialog = new ImageCaptureDialog(this);
        connect(m_imageCaptureDialog, &ImageCaptureDialog::codeGenerated, this, &ScriptEditorDialog::insertCode);

        QHBoxLayout* mainLayout = new QHBoxLayout(this);

        // =========================================================
        // 左侧：快捷指令面板
        // =========================================================
        QWidget* snippetPanel = createSnippetPanel();
        mainLayout->addWidget(snippetPanel);

        // =========================================================
        // 右侧：代码编辑区
        // =========================================================
        QVBoxLayout* editorLayout = new QVBoxLayout();
        editorLayout->addWidget(new QLabel("JavaScript 脚本 (mapi 为内置对象):", this));

        m_editor = new QPlainTextEdit(this);
        m_editor->setPlainText(script);
        m_editor->setPlaceholderText(
            "// === 示例脚本 ===\n"
            "// 单击指定位置\n"
            "mapi.click(0.5, 0.5);\n\n"
            "// 长按（按下时触发）\n"
            "mapi.holdpress(0.3, 0.7);\n\n"
            "// 释放（抬起时触发）\n"
            "mapi.release();\n"
        );
        m_editor->setStyleSheet(
            "QPlainTextEdit {"
            "  background-color: #1e1e1e;"
            "  color: #d4d4d4;"
            "  font-family: 'Consolas', 'Monaco', 'Courier New', monospace;"
            "  font-size: 11pt;"
            "  border: 1px solid #3c3c3c;"
            "  selection-background-color: #264f78;"
            "}"
        );
        m_editor->setTabStopDistance(40);
        editorLayout->addWidget(m_editor, 1);

        // 底部按钮栏
        QHBoxLayout* btnLayout = new QHBoxLayout();

        QPushButton* btnImport = new QPushButton("从脚本库导入", this);
        btnImport->setToolTip("从 keymap/scripts 目录导入脚本");
        connect(btnImport, &QPushButton::clicked, this, &ScriptEditorDialog::onImport);
        btnLayout->addWidget(btnImport);

        QPushButton* btnOpenDir = new QPushButton("打开脚本目录", this);
        connect(btnOpenDir, &QPushButton::clicked, this, &ScriptEditorDialog::onOpenScriptDir);
        btnLayout->addWidget(btnOpenDir);

        QPushButton* btnClear = new QPushButton("清空", this);
        connect(btnClear, &QPushButton::clicked, [this]() {
            if (QMessageBox::question(this, "确认", "确定要清空脚本内容吗？") == QMessageBox::Yes) {
                m_editor->clear();
            }
        });
        btnLayout->addWidget(btnClear);

        btnLayout->addStretch();

        QPushButton* btnCancel = new QPushButton("取消", this);
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
        btnLayout->addWidget(btnCancel);

        QPushButton* btnSave = new QPushButton("保存", this);
        btnSave->setStyleSheet("font-weight: bold; background-color: #0e639c; color: white; padding: 5px 15px;");
        connect(btnSave, &QPushButton::clicked, this, &ScriptEditorDialog::onSave);
        btnLayout->addWidget(btnSave);

        editorLayout->addLayout(btnLayout);
        mainLayout->addLayout(editorLayout, 1);
    }

    QString getScript() const { return m_script; }

    // 设置帧获取回调 (由 VideoForm 提供)
    void setFrameGrabCallback(FrameGrabFunc callback) { m_frameGrabCallback = callback; }

private:
    // =========================================================
    // 创建快捷指令面板
    // =========================================================
    QWidget* createSnippetPanel() {
        QScrollArea* scrollArea = new QScrollArea(this);
        scrollArea->setFixedWidth(230);
        scrollArea->setWidgetResizable(true);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scrollArea->setStyleSheet("QScrollArea { border: none; background: transparent; }");

        QWidget* panel = new QWidget();
        panel->setStyleSheet(
            "QGroupBox { font-weight: bold; margin-top: 10px; padding-top: 10px; border: 1px solid #555; border-radius: 4px; }"
            "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }"
        );

        QVBoxLayout* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(5, 5, 5, 5);
        layout->setSpacing(8);

        // ---------------------------------------------------------
        // 触摸操作
        // ---------------------------------------------------------
        QGroupBox* touchGroup = new QGroupBox("触摸操作", panel);
        QVBoxLayout* touchLayout = new QVBoxLayout(touchGroup);
        touchLayout->setSpacing(3);

        addSnippetButton(touchLayout, "点击 (click)",
            "mapi.click(0.5, 0.5);  // x, y: 0.0~1.0",
            "在指定位置模拟点击\n参数: x, y (0.0~1.0 的相对坐标)\n省略参数则使用锚点位置");

        addSnippetButton(touchLayout, "长按按下 (holdpress)",
            "mapi.holdpress(0.5, 0.5);  // 按下",
            "模拟长按的按下阶段\n通常在 KeyPress 时调用\n需配合 release() 使用");

        addSnippetButton(touchLayout, "长按释放 (release)",
            "mapi.release();  // 释放",
            "释放当前按键绑定的触摸点\n通常在 KeyRelease 时调用");

        addSnippetButton(touchLayout, "滑动 (slide)",
            "mapi.slide(0.3, 0.5, 0.7, 0.5, 200, 10);",
            "模拟滑动操作\n参数: 起点x, 起点y, 终点x, 终点y, 时长ms, 步数");

        layout->addWidget(touchGroup);

        // ---------------------------------------------------------
        // 按键操作
        // ---------------------------------------------------------
        QGroupBox* keyGroup = new QGroupBox("按键操作", panel);
        QVBoxLayout* keyLayout = new QVBoxLayout(keyGroup);
        keyLayout->setSpacing(3);

        addSnippetButton(keyLayout, "发送按键 (key)",
            "mapi.key(\"BACK\");  // 返回键",
            "发送 Android 按键\n常用: BACK, HOME, MENU, ENTER\n字母: A-Z, 数字: 0-9");

        addSnippetButton(keyLayout, "方向键重置",
            "mapi.directionreset();",
            "重置 WASD 方向键状态\n发送所有方向键的抬起事件");

        layout->addWidget(keyGroup);

        // ---------------------------------------------------------
        // 视角控制
        // ---------------------------------------------------------
        QGroupBox* viewGroup = new QGroupBox("视角控制", panel);
        QVBoxLayout* viewLayout = new QVBoxLayout(viewGroup);
        viewLayout->setSpacing(3);

        addSnippetButton(viewLayout, "重置视角",
            "mapi.resetview();",
            "重置鼠标视角控制\n用于 FPS 游戏视角归位");

        addSnippetButton(viewLayout, "设置轮盘灵敏度",
            "mapi.setRadialParam(0.15, 0.15, 0.15, 0.15);",
            "动态调整方向轮盘灵敏度\n参数: 上, 下, 左, 右");

        addSnippetButton(viewLayout, "切换瞄准模式",
            "mapi.shotmode(true);  // true=进入, false=退出",
            "切换游戏瞄准/普通模式\n进入时隐藏光标，退出时显示");

        layout->addWidget(viewGroup);

        // ---------------------------------------------------------
        // 状态查询
        // ---------------------------------------------------------
        QGroupBox* queryGroup = new QGroupBox("状态查询", panel);
        QVBoxLayout* queryLayout = new QVBoxLayout(queryGroup);
        queryLayout->setSpacing(3);

        addSnippetButton(queryLayout, "获取鼠标位置",
            "var pos = mapi.getmousepos();\nmapi.tip(\"x=\" + pos.x + \", y=\" + pos.y);",
            "获取当前鼠标位置\n返回 {x, y} 对象");

        addSnippetButton(queryLayout, "获取按键位置",
            "var pos = mapi.getkeypos(\"W\");\nif (pos.valid) mapi.click(pos.x, pos.y);",
            "获取指定按键映射的位置\n返回 {x, y, valid} 对象");

        addSnippetButton(queryLayout, "获取按键状态",
            "var state = mapi.getKeyState(\"W\");\nif (state) { /* 按下中 */ }",
            "检查指定按键是否按下\n返回 0=未按下, 1=按下中");

        layout->addWidget(queryGroup);

        // ---------------------------------------------------------
        // 图像识别
        // ---------------------------------------------------------
        QGroupBox* imageGroup = new QGroupBox("图像识别", panel);
        QVBoxLayout* imageLayout = new QVBoxLayout(imageGroup);
        imageLayout->setSpacing(3);

        // 区域找图按钮 + 工具按钮
        QHBoxLayout* findImageRow = new QHBoxLayout();
        findImageRow->setSpacing(2);

        QPushButton* btnFindImage = new QPushButton("区域找图 (findImage)", this);
        btnFindImage->setToolTip("在指定区域搜索模板图片\n返回 {found, x, y, confidence}");
        btnFindImage->setCursor(Qt::PointingHandCursor);
        btnFindImage->setStyleSheet(
            "QPushButton { text-align: left; padding: 5px 8px; border: 1px solid #555; "
            "border-radius: 3px; background-color: #3c3c3c; color: #d4d4d4; font-size: 9pt; }"
            "QPushButton:hover { background-color: #4a4a4a; border-color: #0e639c; }"
            "QPushButton:pressed { background-color: #0e639c; }"
        );
        connect(btnFindImage, &QPushButton::clicked, [this]() {
            insertCode("// 区域找图\n"
                       "var result = mapi.findImage(\"模板图片.png\", 0, 0, 1, 1, 0.8);\n"
                       "if (result.found) {\n"
                       "    mapi.click(result.x, result.y);\n"
                       "    mapi.tip(\"找到目标: \" + result.confidence.toFixed(2));\n"
                       "} else {\n"
                       "    mapi.tip(\"未找到目标\");\n"
                       "}");
        });
        findImageRow->addWidget(btnFindImage, 1);

        // 工具按钮 (截图/框选)
        QToolButton* btnImageTool = new QToolButton(this);
        btnImageTool->setText("⚙");
        btnImageTool->setToolTip("图像工具\n- 截取模板图片\n- 框选搜索区域");
        btnImageTool->setFixedSize(28, 28);
        btnImageTool->setPopupMode(QToolButton::InstantPopup);
        btnImageTool->setStyleSheet(
            "QToolButton { background-color: #3c3c3c; color: #d4d4d4; border: 1px solid #555; border-radius: 3px; font-size: 12pt; }"
            "QToolButton:hover { background-color: #4a4a4a; border-color: #0e639c; }"
            "QToolButton::menu-indicator { image: none; }"
        );

        QMenu* imageToolMenu = new QMenu(this);
        imageToolMenu->setStyleSheet(
            "QMenu { background-color: #2d2d2d; color: #d4d4d4; border: 1px solid #555; }"
            "QMenu::item { padding: 6px 20px; }"
            "QMenu::item:selected { background-color: #0e639c; }"
        );

        QAction* actCapture = imageToolMenu->addAction("📷 截取模板图片");
        actCapture->setToolTip("从当前视频帧截取模板图片保存");
        connect(actCapture, &QAction::triggered, this, &ScriptEditorDialog::onCaptureTemplate);

        QAction* actRegion = imageToolMenu->addAction("🔲 框选搜索区域");
        actRegion->setToolTip("框选区域并生成找图代码");
        connect(actRegion, &QAction::triggered, this, &ScriptEditorDialog::onSelectRegion);

        imageToolMenu->addSeparator();

        QAction* actOpenImages = imageToolMenu->addAction("📁 打开图片文件夹");
        connect(actOpenImages, &QAction::triggered, []() {
            QString path = QCoreApplication::applicationDirPath() + "/keymap/images";
            QDir dir(path);
            if (!dir.exists()) dir.mkpath(".");
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });

        btnImageTool->setMenu(imageToolMenu);
        findImageRow->addWidget(btnImageTool);

        imageLayout->addLayout(findImageRow);

        addSnippetButton(imageLayout, "找图并点击",
            "// 找到图片就点击\n"
            "var result = mapi.findImage(\"按钮.png\");\n"
            "if (result.found) mapi.click(result.x, result.y);",
            "简化的找图点击\n全屏搜索，默认阈值 0.8");

        addSnippetButton(imageLayout, "循环等待图片",
            "// 循环等待图片出现 (最多5秒)\n"
            "for (var i = 0; i < 50; i++) {\n"
            "    var result = mapi.findImage(\"目标.png\");\n"
            "    if (result.found) {\n"
            "        mapi.click(result.x, result.y);\n"
            "        break;\n"
            "    }\n"
            "    mapi.delay(100);\n"
            "}",
            "循环检测直到找到图片\n适合等待界面加载");

        addSnippetButton(imageLayout, "多图判断",
            "// 判断当前界面\n"
            "if (mapi.findImage(\"主界面.png\").found) {\n"
            "    mapi.tip(\"在主界面\");\n"
            "} else if (mapi.findImage(\"战斗界面.png\").found) {\n"
            "    mapi.tip(\"在战斗中\");\n"
            "}",
            "根据不同图片判断当前界面状态");

        layout->addWidget(imageGroup);

        // ---------------------------------------------------------
        // 工具函数
        // ---------------------------------------------------------
        QGroupBox* utilGroup = new QGroupBox("工具", panel);
        QVBoxLayout* utilLayout = new QVBoxLayout(utilGroup);
        utilLayout->setSpacing(3);

        addSnippetButton(utilLayout, "延时 (delay)",
            "mapi.delay(100);  // 毫秒",
            "脚本暂停执行指定毫秒\n注意: 会阻塞当前脚本");

        addSnippetButton(utilLayout, "调试输出 (tip)",
            "mapi.tip(\"调试信息\");",
            "在控制台输出调试信息");

        layout->addWidget(utilGroup);

        // ---------------------------------------------------------
        // 代码结构
        // ---------------------------------------------------------
        QGroupBox* codeGroup = new QGroupBox("代码结构", panel);
        QVBoxLayout* codeLayout = new QVBoxLayout(codeGroup);
        codeLayout->setSpacing(3);

        addSnippetButton(codeLayout, "if 条件判断",
            "if (condition) {\n    // 条件为真时执行\n}",
            "条件判断语句");

        addSnippetButton(codeLayout, "if-else 分支",
            "if (condition) {\n    // 条件为真\n} else {\n    // 条件为假\n}",
            "条件分支语句");

        addSnippetButton(codeLayout, "for 循环",
            "for (var i = 0; i < 10; i++) {\n    // 循环体\n    mapi.delay(50);\n}",
            "计数循环");

        addSnippetButton(codeLayout, "while 循环",
            "while (condition) {\n    // 循环体\n    mapi.delay(100);\n}",
            "条件循环\n注意添加延时避免死循环");

        addSnippetButton(codeLayout, "定义函数",
            "function myFunc(param) {\n    // 函数体\n    return result;\n}",
            "自定义函数");

        addSnippetButton(codeLayout, "导入模块",
            "var mod = import('keymap/scripts/example.js');\nif (mod) mod.run(mapi);",
            "从脚本库导入外部模块");

        layout->addWidget(codeGroup);

        // ---------------------------------------------------------
        // 常用组合
        // ---------------------------------------------------------
        QGroupBox* comboGroup = new QGroupBox("常用组合", panel);
        QVBoxLayout* comboLayout = new QVBoxLayout(comboGroup);
        comboLayout->setSpacing(3);

        addSnippetButton(comboLayout, "长按完整模板",
            "// 长按脚本模板\n// 绑定到按键后，按下触发 holdpress，松开触发 release\nmapi.holdpress(0.5, 0.5);\n// 注意：release 会在按键释放时自动调用",
            "长按操作的完整模板\n按下时 holdpress，松开时 release");

        addSnippetButton(comboLayout, "连续点击",
            "// 连续点击 n 次\nfor (var i = 0; i < 5; i++) {\n    mapi.click(0.5, 0.5);\n    mapi.delay(100);\n}",
            "循环执行多次点击");

        addSnippetButton(comboLayout, "滑动攻击",
            "// 滑动攻击组合\nmapi.holdpress(0.5, 0.5);\nmapi.delay(50);\nmapi.slide(0.5, 0.5, 0.7, 0.5, 100, 5);\nmapi.release();",
            "按下后滑动的组合操作");

        addSnippetButton(comboLayout, "检测后执行",
            "// 检测按键状态后执行\nif (mapi.getKeyState(\"SHIFT\")) {\n    mapi.click(0.8, 0.8);  // Shift 按下时点击这里\n} else {\n    mapi.click(0.5, 0.5);  // 否则点击这里\n}",
            "根据其他按键状态执行不同操作");

        layout->addWidget(comboGroup);

        layout->addStretch();
        scrollArea->setWidget(panel);
        return scrollArea;
    }

    // =========================================================
    // 添加快捷指令按钮
    // =========================================================
    void addSnippetButton(QVBoxLayout* layout, const QString& label,
                          const QString& code, const QString& tooltip) {
        QPushButton* btn = new QPushButton(label, this);
        btn->setToolTip(tooltip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton {"
            "  text-align: left;"
            "  padding: 5px 8px;"
            "  border: 1px solid #555;"
            "  border-radius: 3px;"
            "  background-color: #3c3c3c;"
            "  color: #d4d4d4;"
            "  font-size: 9pt;"
            "}"
            "QPushButton:hover {"
            "  background-color: #4a4a4a;"
            "  border-color: #0e639c;"
            "}"
            "QPushButton:pressed {"
            "  background-color: #0e639c;"
            "}"
        );
        connect(btn, &QPushButton::clicked, [this, code]() {
            insertCode(code);
        });
        layout->addWidget(btn);
    }

    // =========================================================
    // 插入代码到编辑器
    // =========================================================
    void insertCode(const QString& code) {
        QTextCursor cursor = m_editor->textCursor();

        // 如果当前行不为空，先换行
        cursor.movePosition(QTextCursor::StartOfLine, QTextCursor::MoveAnchor);
        cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
        if (!cursor.selectedText().trimmed().isEmpty()) {
            cursor.movePosition(QTextCursor::End);
            cursor.insertText("\n");
        }

        cursor.insertText(code);
        cursor.insertText("\n");
        m_editor->setTextCursor(cursor);
        m_editor->setFocus();
    }

private slots:
    QString getScriptPath() {
        QString path = QCoreApplication::applicationDirPath() + "/keymap/scripts";
        QDir dir(path);
        if (!dir.exists()) {
            dir.mkpath(".");
        }
        return path;
    }

    void onOpenScriptDir() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(getScriptPath()));
    }

    void onImport() {
        QString scriptDir = getScriptPath();
        QString fileName = QFileDialog::getOpenFileName(this, "选择脚本文件", scriptDir,
            "JavaScript (*.js);;All Files (*)");

        if (!fileName.isEmpty()) {
            QDir appDir(QCoreApplication::applicationDirPath());
            QString relativePath = appDir.relativeFilePath(fileName);
            QString cleanPath = relativePath.replace("\\", "/");

            QString code = QString(
                "var mod = import('%1');\n"
                "if (mod) {\n"
                "    mod.run(mapi);\n"
                "}"
            ).arg(cleanPath);

            insertCode(code);
        }
    }

    void onSave() {
        m_script = m_editor->toPlainText();
        accept();
    }

    void onCaptureTemplate() {
        if (!m_frameGrabCallback) {
            QMessageBox::warning(this, "错误", "未设置帧获取回调，无法截取图片");
            return;
        }
        QImage frame = m_frameGrabCallback();
        if (frame.isNull()) {
            QMessageBox::warning(this, "错误", "当前没有可用的视频帧");
            return;
        }
        m_imageCaptureDialog->captureTemplate(frame);
    }

    void onSelectRegion() {
        if (!m_frameGrabCallback) {
            QMessageBox::warning(this, "错误", "未设置帧获取回调，无法框选区域");
            return;
        }
        QImage frame = m_frameGrabCallback();
        if (frame.isNull()) {
            QMessageBox::warning(this, "错误", "当前没有可用的视频帧");
            return;
        }
        m_imageCaptureDialog->selectRegion(frame);
    }

private:
    QPlainTextEdit* m_editor;
    QString m_script;
    FrameGrabFunc m_frameGrabCallback;
    ImageCaptureDialog* m_imageCaptureDialog = nullptr;
};

#endif // SCRIPTEDITORDIALOG_H
