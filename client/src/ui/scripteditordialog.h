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
#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QPainter>
#include <QTextBlock>
#include <QCompleter>
#include <QStringListModel>
#include <QAbstractItemView>
#include <QScrollBar>
#include <QShortcut>
#include <functional>

#ifdef Q_OS_WIN
#include "winutils.h"
#endif

#include "selectioneditordialog.h"
#include "selectionregionmanager.h"

// FrameGrabFunc 已在 selectioneditordialog.h 中定义

// ---------------------------------------------------------
// JavaScript 语法高亮器 / JavaScript Syntax Highlighter
// ---------------------------------------------------------
class JsSyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit JsSyntaxHighlighter(QTextDocument* parent = nullptr) : QSyntaxHighlighter(parent) {
        // 关键字
        QTextCharFormat keywordFormat;
        keywordFormat.setForeground(QColor("#c586c0"));  // 紫色
        keywordFormat.setFontWeight(QFont::Bold);
        QStringList keywords = {
            "\\bvar\\b", "\\blet\\b", "\\bconst\\b", "\\bfunction\\b",
            "\\breturn\\b", "\\bif\\b", "\\belse\\b", "\\bfor\\b",
            "\\bwhile\\b", "\\bdo\\b", "\\bswitch\\b", "\\bcase\\b",
            "\\bbreak\\b", "\\bcontinue\\b", "\\bdefault\\b", "\\btry\\b",
            "\\bcatch\\b", "\\bfinally\\b", "\\bthrow\\b", "\\bnew\\b",
            "\\bclass\\b", "\\bextends\\b", "\\bexport\\b", "\\bimport\\b",
            "\\bfrom\\b", "\\btypeof\\b", "\\binstanceof\\b", "\\bin\\b",
            "\\bthis\\b", "\\bnull\\b", "\\bundefined\\b", "\\btrue\\b", "\\bfalse\\b"
        };
        for (const QString& pattern : keywords) {
            m_rules.append({QRegularExpression(pattern), keywordFormat});
        }

        // mapi 对象和方法 - 特殊高亮
        QTextCharFormat mapiFormat;
        mapiFormat.setForeground(QColor("#4ec9b0"));  // 青色
        mapiFormat.setFontWeight(QFont::Bold);
        m_rules.append({QRegularExpression("\\bmapi\\b"), mapiFormat});

        // mapi 方法名
        QTextCharFormat methodFormat;
        methodFormat.setForeground(QColor("#dcdcaa"));  // 黄色
        m_rules.append({QRegularExpression("\\b(click|holdpress|release|releaseAll|slide|pinch|key|sleep|toast|log|"
                                            "isPress|isInterrupted|stop|setGlobal|getGlobal|loadModule|"
                                            "shotmode|setRadialParam|resetview|resetwheel|getmousepos|getkeypos|"
                                            "getKeyState|setKeyUIPos|findImage|findImageByRegion|getbuttonpos|swipeById)\\b"), methodFormat});

        // 数字
        QTextCharFormat numberFormat;
        numberFormat.setForeground(QColor("#b5cea8"));  // 浅绿色
        m_rules.append({QRegularExpression("\\b[0-9]+\\.?[0-9]*\\b"), numberFormat});

        // 字符串 (单引号和双引号)
        QTextCharFormat stringFormat;
        stringFormat.setForeground(QColor("#ce9178"));  // 橙色
        m_rules.append({QRegularExpression("\"[^\"]*\""), stringFormat});
        m_rules.append({QRegularExpression("'[^']*'"), stringFormat});

        // 单行注释
        QTextCharFormat commentFormat;
        commentFormat.setForeground(QColor("#6a9955"));  // 绿色
        commentFormat.setFontItalic(true);
        m_rules.append({QRegularExpression("//[^\n]*"), commentFormat});

        // 多行注释 (需要特殊处理)
        m_multiLineCommentFormat = commentFormat;
        m_commentStartExp = QRegularExpression("/\\*");
        m_commentEndExp = QRegularExpression("\\*/");

        // 函数名
        QTextCharFormat funcFormat;
        funcFormat.setForeground(QColor("#dcdcaa"));  // 黄色
        m_rules.append({QRegularExpression("\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()"), funcFormat});

        // 属性访问
        QTextCharFormat propFormat;
        propFormat.setForeground(QColor("#9cdcfe"));  // 浅蓝色
        m_rules.append({QRegularExpression("(?<=\\.)\\b[A-Za-z_][A-Za-z0-9_]*\\b"), propFormat});
    }

protected:
    void highlightBlock(const QString& text) override {
        // 应用普通规则
        for (const HighlightRule& rule : m_rules) {
            QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
            while (it.hasNext()) {
                QRegularExpressionMatch match = it.next();
                setFormat(match.capturedStart(), match.capturedLength(), rule.format);
            }
        }

        // 多行注释处理
        setCurrentBlockState(0);
        int startIndex = 0;
        if (previousBlockState() != 1) {
            startIndex = text.indexOf(m_commentStartExp);
        }
        while (startIndex >= 0) {
            QRegularExpressionMatch endMatch = m_commentEndExp.match(text, startIndex);
            int endIndex = endMatch.capturedStart();
            int commentLength;
            if (endIndex == -1) {
                setCurrentBlockState(1);
                commentLength = text.length() - startIndex;
            } else {
                commentLength = endIndex - startIndex + endMatch.capturedLength();
            }
            setFormat(startIndex, commentLength, m_multiLineCommentFormat);
            startIndex = text.indexOf(m_commentStartExp, startIndex + commentLength);
        }
    }

private:
    struct HighlightRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QVector<HighlightRule> m_rules;
    QTextCharFormat m_multiLineCommentFormat;
    QRegularExpression m_commentStartExp;
    QRegularExpression m_commentEndExp;
};

// ---------------------------------------------------------
// 代码编辑器 (带行号、自动缩进、括号匹配)
// ---------------------------------------------------------
class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit CodeEditor(QWidget* parent = nullptr) : QPlainTextEdit(parent) {
        m_lineNumberArea = new LineNumberArea(this);
        m_highlighter = new JsSyntaxHighlighter(document());

        connect(this, &CodeEditor::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
        connect(this, &CodeEditor::updateRequest, this, &CodeEditor::updateLineNumberArea);
        connect(this, &CodeEditor::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);
        connect(this, &CodeEditor::cursorPositionChanged, this, &CodeEditor::matchBrackets);

        updateLineNumberAreaWidth(0);
        highlightCurrentLine();

        // 设置自动补全
        setupCompleter();
    }

    void lineNumberAreaPaintEvent(QPaintEvent* event) {
        QPainter painter(m_lineNumberArea);
        painter.fillRect(event->rect(), QColor("#1e1e1e"));

        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
        int bottom = top + qRound(blockBoundingRect(block).height());

        while (block.isValid() && top <= event->rect().bottom()) {
            if (block.isVisible() && bottom >= event->rect().top()) {
                QString number = QString::number(blockNumber + 1);
                painter.setPen(QColor("#858585"));
                painter.setFont(font());
                painter.drawText(0, top, m_lineNumberArea->width() - 8, fontMetrics().height(),
                                 Qt::AlignRight, number);
            }
            block = block.next();
            top = bottom;
            bottom = top + qRound(blockBoundingRect(block).height());
            ++blockNumber;
        }
    }

    int lineNumberAreaWidth() {
        int digits = 1;
        int max = qMax(1, blockCount());
        while (max >= 10) {
            max /= 10;
            ++digits;
        }
        return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(digits, 3);
    }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QPlainTextEdit::resizeEvent(event);
        QRect cr = contentsRect();
        m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
    }

    void keyPressEvent(QKeyEvent* event) override {
        // 自动补全
        if (m_completer && m_completer->popup()->isVisible()) {
            switch (event->key()) {
            case Qt::Key_Enter:
            case Qt::Key_Return:
            case Qt::Key_Escape:
            case Qt::Key_Tab:
            case Qt::Key_Backtab:
                event->ignore();
                return;
            default:
                break;
            }
        }

        // Tab 键处理：插入4个空格
        if (event->key() == Qt::Key_Tab) {
            insertPlainText("    ");
            return;
        }

        // 回车自动缩进
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            QTextCursor cursor = textCursor();
            QString line = cursor.block().text();

            // 计算当前行的缩进
            QString indent;
            for (QChar c : line) {
                if (c == ' ' || c == '\t') indent += c;
                else break;
            }

            // 如果行末是 { 或 (，增加缩进
            QString trimmed = line.trimmed();
            if (trimmed.endsWith('{') || trimmed.endsWith('(') || trimmed.endsWith('[')) {
                indent += "    ";
            }

            QPlainTextEdit::keyPressEvent(event);
            insertPlainText(indent);
            return;
        }

        // 自动补全括号
        if (event->key() == Qt::Key_BraceLeft) {
            insertPlainText("{}");
            moveCursor(QTextCursor::Left);
            return;
        }
        if (event->key() == Qt::Key_ParenLeft) {
            insertPlainText("()");
            moveCursor(QTextCursor::Left);
            return;
        }
        if (event->key() == Qt::Key_BracketLeft) {
            insertPlainText("[]");
            moveCursor(QTextCursor::Left);
            return;
        }
        if (event->key() == Qt::Key_QuoteDbl) {
            QTextCursor cursor = textCursor();
            if (!cursor.hasSelection()) {
                insertPlainText("\"\"");
                moveCursor(QTextCursor::Left);
                return;
            }
        }
        if (event->key() == Qt::Key_Apostrophe) {
            QTextCursor cursor = textCursor();
            if (!cursor.hasSelection()) {
                insertPlainText("''");
                moveCursor(QTextCursor::Left);
                return;
            }
        }

        QPlainTextEdit::keyPressEvent(event);

        // 触发自动补全
        if (m_completer) {
            QString prefix = wordUnderCursor();
            if (prefix.length() >= 2) {
                m_completer->setCompletionPrefix(prefix);
                if (m_completer->completionCount() > 0) {
                    QRect cr = cursorRect();
                    cr.setWidth(m_completer->popup()->sizeHintForColumn(0) +
                               m_completer->popup()->verticalScrollBar()->sizeHint().width());
                    m_completer->complete(cr);
                } else {
                    m_completer->popup()->hide();
                }
            } else {
                m_completer->popup()->hide();
            }
        }
    }

private slots:
    void updateLineNumberAreaWidth(int) {
        setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
    }

    void updateLineNumberArea(const QRect& rect, int dy) {
        if (dy)
            m_lineNumberArea->scroll(0, dy);
        else
            m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
        if (rect.contains(viewport()->rect()))
            updateLineNumberAreaWidth(0);
    }

    void highlightCurrentLine() {
        QList<QTextEdit::ExtraSelection> extraSelections;
        if (!isReadOnly()) {
            QTextEdit::ExtraSelection selection;
            selection.format.setBackground(QColor("#2d2d30"));
            selection.format.setProperty(QTextFormat::FullWidthSelection, true);
            selection.cursor = textCursor();
            selection.cursor.clearSelection();
            extraSelections.append(selection);
        }
        // 保留括号匹配的高亮
        extraSelections.append(m_bracketSelections);
        setExtraSelections(extraSelections);
    }

    void matchBrackets() {
        m_bracketSelections.clear();
        QTextCursor cursor = textCursor();
        QTextDocument* doc = document();
        int pos = cursor.position();

        auto matchBracket = [&](int position, QChar open, QChar close, bool forward) {
            int depth = 1;
            int i = position + (forward ? 1 : -1);
            while (i >= 0 && i < doc->characterCount()) {
                QChar c = doc->characterAt(i);
                if (c == open) depth += forward ? 1 : -1;
                else if (c == close) depth += forward ? -1 : 1;
                if (depth == 0) return i;
                i += forward ? 1 : -1;
            }
            return -1;
        };

        auto addBracketHighlight = [&](int p1, int p2) {
            QTextEdit::ExtraSelection sel1, sel2;
            sel1.format.setBackground(QColor("#3f3f46"));
            sel1.format.setForeground(QColor("#ffd700"));
            sel2.format = sel1.format;

            QTextCursor c1 = textCursor();
            c1.setPosition(p1);
            c1.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
            sel1.cursor = c1;

            QTextCursor c2 = textCursor();
            c2.setPosition(p2);
            c2.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);
            sel2.cursor = c2;

            m_bracketSelections.append(sel1);
            m_bracketSelections.append(sel2);
        };

        if (pos > 0) {
            QChar c = doc->characterAt(pos - 1);
            if (c == '(' || c == '{' || c == '[') {
                QChar close = (c == '(') ? ')' : (c == '{') ? '}' : ']';
                int match = matchBracket(pos - 1, c, close, true);
                if (match >= 0) addBracketHighlight(pos - 1, match);
            } else if (c == ')' || c == '}' || c == ']') {
                QChar open = (c == ')') ? '(' : (c == '}') ? '{' : '[';
                int match = matchBracket(pos - 1, c, open, false);
                if (match >= 0) addBracketHighlight(match, pos - 1);
            }
        }
        highlightCurrentLine();
    }

    void insertCompletion(const QString& completion) {
        if (!m_completer) return;
        QTextCursor cursor = textCursor();
        int extra = completion.length() - m_completer->completionPrefix().length();
        cursor.movePosition(QTextCursor::Left);
        cursor.movePosition(QTextCursor::EndOfWord);
        cursor.insertText(completion.right(extra));
        setTextCursor(cursor);
    }

private:
    void setupCompleter() {
        QStringList words = {
            // mapi 方法
            "mapi", "click", "holdpress", "release", "releaseAll", "slide", "pinch",
            "key", "sleep", "toast", "log", "isPress", "isInterrupted", "stop",
            "setGlobal", "getGlobal", "loadModule", "shotmode", "setRadialParam",
            "resetview", "resetwheel", "getmousepos", "getkeypos", "getKeyState", "setKeyUIPos",
            "findImage", "findImageByRegion", "getbuttonpos", "swipeById",
            // 关键字
            "var", "let", "const", "function", "return", "if", "else", "for", "while",
            "true", "false", "null", "undefined", "new", "this",
            // 常用
            "found", "confidence"
        };

        m_completer = new QCompleter(words, this);
        m_completer->setWidget(this);
        m_completer->setCompletionMode(QCompleter::PopupCompletion);
        m_completer->setCaseSensitivity(Qt::CaseInsensitive);
        m_completer->popup()->setStyleSheet(
            "QListView {"
            "  background-color: #1e1e1e;"
            "  color: #d4d4d4;"
            "  border: 1px solid #3f3f46;"
            "  border-radius: 4px;"
            "  selection-background-color: #094771;"
            "  outline: none;"
            "}"
            "QListView::item { padding: 4px 8px; }"
            "QListView::item:selected { background-color: #094771; }"
        );
        connect(m_completer, QOverload<const QString&>::of(&QCompleter::activated),
                this, &CodeEditor::insertCompletion);
    }

    QString wordUnderCursor() {
        QTextCursor cursor = textCursor();
        cursor.select(QTextCursor::WordUnderCursor);
        return cursor.selectedText();
    }

    // 行号区域组件
    class LineNumberArea : public QWidget {
    public:
        LineNumberArea(CodeEditor* editor) : QWidget(editor), m_editor(editor) {}
        QSize sizeHint() const override { return QSize(m_editor->lineNumberAreaWidth(), 0); }
    protected:
        void paintEvent(QPaintEvent* event) override { m_editor->lineNumberAreaPaintEvent(event); }
    private:
        CodeEditor* m_editor;
    };

    LineNumberArea* m_lineNumberArea;
    JsSyntaxHighlighter* m_highlighter;
    QCompleter* m_completer = nullptr;
    QList<QTextEdit::ExtraSelection> m_bracketSelections;
};

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
        setWindowTitle(tr("脚本编辑器"));
        resize(850, 550);
        setAttribute(Qt::WA_DeleteOnClose, false); // exec() 模式下由栈管理生命周期
        setWindowModality(Qt::WindowModal);  // 只阻塞父窗口链，不阻塞独立顶层窗口

        // 设置 Windows 深色标题栏
#ifdef Q_OS_WIN
        WinUtils::setDarkBorderToWindow(reinterpret_cast<HWND>(winId()), true);
#endif

        // 设置对话框整体样式 (与 modern_dark.qss 一致的 Zinc 色系)
        setStyleSheet(
            "QDialog { background-color: #18181b; }"
            "QWidget { background-color: #18181b; }"
            "QLabel { color: #fafafa; background: transparent; }"
            "QGroupBox { "
            "  font-weight: bold; "
            "  color: #fafafa; "
            "  margin-top: 12px; "
            "  padding-top: 12px; "
            "  border: 1px solid #3f3f46; "
            "  border-radius: 6px; "
            "  background-color: #18181b; "
            "}"
            "QGroupBox::title { "
            "  subcontrol-origin: margin; "
            "  left: 10px; "
            "  padding: 0 6px; "
            "  color: #a1a1aa; "
            "  background-color: #18181b; "
            "}"
            "QScrollArea { border: none; background-color: #18181b; }"
            "QScrollArea > QWidget > QWidget { background-color: #18181b; }"
            "QScrollBar:vertical { "
            "  background: #18181b; "
            "  width: 8px; "
            "  border-radius: 4px; "
            "}"
            "QScrollBar::handle:vertical { "
            "  background: #3f3f46; "
            "  border-radius: 4px; "
            "  min-height: 30px; "
            "}"
            "QScrollBar::handle:vertical:hover { background: #52525b; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QMenu { "
            "  background-color: #18181b; "
            "  color: #fafafa; "
            "  border: 1px solid #3f3f46; "
            "  border-radius: 6px; "
            "  padding: 4px; "
            "}"
            "QMenu::item { "
            "  padding: 8px 16px; "
            "  border-radius: 4px; "
            "}"
            "QMenu::item:selected { "
            "  background-color: #6366f1; "
            "}"
            "QMenu::separator { "
            "  height: 1px; "
            "  background-color: #3f3f46; "
            "  margin: 4px 8px; "
            "}"
            "QMessageBox { background-color: #18181b; color: #fafafa; }"
            "QMessageBox QLabel { color: #fafafa; }"
            "QMessageBox QPushButton { "
            "  background-color: #27272a; "
            "  color: #fafafa; "
            "  border: 1px solid #3f3f46; "
            "  border-radius: 6px; "
            "  padding: 6px 16px; "
            "}"
            "QMessageBox QPushButton:hover { background-color: #3f3f46; }"
        );

        // 初始化选区编辑器引用
        m_selectionEditorDialog = nullptr;

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

        QLabel* titleLabel = new QLabel(tr("JavaScript 脚本 (mapi 为内置对象):"), this);
        titleLabel->setStyleSheet("color: #a1a1aa; font-size: 10pt;");
        editorLayout->addWidget(titleLabel);

        m_editor = new CodeEditor(this);
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
            "CodeEditor, QPlainTextEdit {"
            "  background-color: #1e1e1e;"
            "  color: #d4d4d4;"
            "  font-family: 'Consolas', 'Monaco', 'Courier New', monospace;"
            "  font-size: 11pt;"
            "  border: 1px solid #3f3f46;"
            "  border-radius: 6px;"
            "  padding: 4px;"
            "  selection-background-color: #264f78;"
            "  selection-color: #ffffff;"
            "}"
            "CodeEditor:focus, QPlainTextEdit:focus {"
            "  border-color: #6366f1;"
            "}"
        );
        m_editor->setTabStopDistance(40);
        editorLayout->addWidget(m_editor, 1);

        // 底部按钮栏
        QHBoxLayout* btnLayout = new QHBoxLayout();

        // "获取工具" 按钮 (打开选区编辑器)
        QPushButton* btnTools = new QPushButton(tr("获取工具"), this);
        btnTools->setToolTip(tr("打开自定义选区管理器\n支持获取位置、创建选区、截图等"));
        styleButton(btnTools, false);
        connect(btnTools, &QPushButton::clicked, this, &ScriptEditorDialog::onCustomRegion);
        btnLayout->addWidget(btnTools);

        QPushButton* btnOpenDir = new QPushButton(tr("打开脚本目录"), this);
        styleButton(btnOpenDir, false);
        connect(btnOpenDir, &QPushButton::clicked, this, &ScriptEditorDialog::onOpenScriptDir);
        btnLayout->addWidget(btnOpenDir);

        QPushButton* btnClear = new QPushButton(tr("清空"), this);
        styleButton(btnClear, false);
        connect(btnClear, &QPushButton::clicked, [this]() {
            if (QMessageBox::question(this, tr("确认"), tr("确定要清空脚本内容吗？")) == QMessageBox::Yes) {
                m_editor->clear();
            }
        });
        btnLayout->addWidget(btnClear);

        btnLayout->addStretch();

        QPushButton* btnCancel = new QPushButton(tr("取消"), this);
        styleButton(btnCancel, false);
        connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
        btnLayout->addWidget(btnCancel);

        QPushButton* btnSave = new QPushButton(tr("保存"), this);
        styleButton(btnSave, true);  // 主按钮
        connect(btnSave, &QPushButton::clicked, this, &ScriptEditorDialog::onSave);
        btnLayout->addWidget(btnSave);

        editorLayout->addLayout(btnLayout);
        mainLayout->addLayout(editorLayout, 1);
    }

    QString getScript() const { return m_script; }

    ~ScriptEditorDialog() {
        if (m_selectionEditorDialog) {
            m_selectionEditorDialog->close();
            delete m_selectionEditorDialog;
            m_selectionEditorDialog = nullptr;
        }
    }

    // 设置帧获取回调 (由 VideoForm 提供)
    void setFrameGrabCallback(FrameGrabFunc callback) { m_frameGrabCallback = callback; }

private:
    // =========================================================
    // 按钮样式辅助函数
    // =========================================================
    void styleButton(QPushButton* btn, bool isPrimary) {
        if (isPrimary) {
            btn->setStyleSheet(
                "QPushButton {"
                "  background-color: #6366f1;"
                "  color: #ffffff;"
                "  border: none;"
                "  border-radius: 6px;"
                "  padding: 8px 20px;"
                "  font-weight: bold;"
                "  font-size: 10pt;"
                "}"
                "QPushButton:hover {"
                "  background-color: #818cf8;"
                "}"
                "QPushButton:pressed {"
                "  background-color: #4f46e5;"
                "}"
            );
        } else {
            btn->setStyleSheet(
                "QPushButton {"
                "  background-color: #27272a;"
                "  color: #fafafa;"
                "  border: 1px solid #3f3f46;"
                "  border-radius: 6px;"
                "  padding: 8px 16px;"
                "  font-size: 10pt;"
                "}"
                "QPushButton:hover {"
                "  background-color: #3f3f46;"
                "  border-color: #52525b;"
                "}"
                "QPushButton:pressed {"
                "  background-color: #52525b;"
                "}"
            );
        }
    }
    // =========================================================
    // 创建快捷指令面板
    // =========================================================
    QWidget* createSnippetPanel() {
        QScrollArea* scrollArea = new QScrollArea(this);
        scrollArea->setFixedWidth(240);
        scrollArea->setWidgetResizable(true);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scrollArea->setStyleSheet("QScrollArea { background-color: #18181b; border: none; }");

        QWidget* panel = new QWidget();
        panel->setStyleSheet("QWidget { background-color: #18181b; }");
        QVBoxLayout* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(10);

        // ---------------------------------------------------------
        // 触摸操作
        // ---------------------------------------------------------
        QGroupBox* touchGroup = new QGroupBox(tr("触摸操作"), panel);
        QVBoxLayout* touchLayout = new QVBoxLayout(touchGroup);
        touchLayout->setSpacing(3);

        addSnippetButton(touchLayout, tr("点击 (click)"),
            "mapi.click();  // 省略参数使用锚点位置，或 mapi.click(x, y);",
            tr("在指定位置模拟点击\n参数: x, y (0.0~1.0 的相对坐标)\n省略参数则使用锚点位置\n获取位置按钮可存储坐标"));

        addSnippetButton(touchLayout, tr("长按 (holdpress)"),
            "mapi.holdpress();  // 省略参数使用锚点位置",
            tr("模拟长按的按下阶段\n按下时调用，松开时自动 release"));

        addSnippetButton(touchLayout, tr("滑动 (slide)"),
            "mapi.slide(x0, y0, x1, y1, 200, 10);  // 起点到终点，200ms，10步",
            tr("模拟滑动操作\n参数: 起点x, 起点y, 终点x, 终点y, 时长ms, 步数"));

        addSnippetButton(touchLayout, tr("双指缩放 (pinch)"),
            "mapi.pinch(0.5, 0.5, 2.0, 300, 10);  // 中心点, 放大2倍, 300ms",
            tr("双指缩放操作\n参数: 中心x, 中心y, 缩放比例, 时长ms, 步数\nscale>1 放大, scale<1 缩小"));

        addSnippetButton(touchLayout, tr("释放触摸 (release)"),
            "mapi.release();  // 释放当前按键的触摸点",
            tr("释放当前 holdpress 按下的触摸点\n通常在松开按键时调用"));

        addSnippetButton(touchLayout, tr("释放所有触摸 (releaseAll)"),
            "mapi.releaseAll();  // 释放当前按键的所有触摸点",
            tr("释放当前按键绑定的所有触摸点\n用于多点触控时批量释放"));

        layout->addWidget(touchGroup);

        // ---------------------------------------------------------
        // 按键操作
        // ---------------------------------------------------------
        QGroupBox* keyGroup = new QGroupBox(tr("按键操作"), panel);
        QVBoxLayout* keyLayout = new QVBoxLayout(keyGroup);
        keyLayout->setSpacing(3);

        addSnippetButton(keyLayout, tr("执行按键 (key)"),
            "mapi.key(\"W\", 50);  // 执行 W 键，按下 50ms",
            tr("模拟按下键位中的按键\n参数: 按键名, 持续时间(ms)\n会触发对应的宏脚本\n支持: A-Z, 0-9, Tab, =, 符号等"));

        layout->addWidget(keyGroup);

        // ---------------------------------------------------------
        // 视角控制
        // ---------------------------------------------------------
        QGroupBox* viewGroup = new QGroupBox(tr("视角控制"), panel);
        QVBoxLayout* viewLayout = new QVBoxLayout(viewGroup);
        viewLayout->setSpacing(3);

        addSnippetButton(viewLayout, tr("重置视角"),
            "mapi.resetview();",
            tr("重置鼠标视角控制\n用于 FPS 游戏视角归位"));

        addSnippetButton(viewLayout, tr("重置轮盘"),
            "mapi.resetwheel();",
            tr("重置轮盘状态\n用于场景切换后轮盘重同步\n例如：跑步时按F进入车辆"));

        addSnippetButton(viewLayout, tr("设置轮盘偏移系数"),
            "mapi.setRadialParam(2, 1, 1, 1);  // 上*2, 下*1, 左*1, 右*1",
            tr("临时设置轮盘偏移系数\n实际偏移 = 原值 × 系数\n默认 1,1,1,1（不变）"));

        addSnippetButton(viewLayout, tr("切换光标/游戏模式"),
            "mapi.shotmode(false);  // false=光标模式, true=游戏模式",
            tr("切换光标/游戏模式\nfalse = 显示光标\ntrue = 隐藏光标(游戏模式)"));

        addSnippetButton(viewLayout, tr("设置按键UI位置"),
            "mapi.setKeyUIPos(\"J\", 0.5, 0.5);  // 将 J 键的 UI 移动到中心",
            tr("动态更新宏按键的UI显示位置\n参数: 按键名, x, y, [xoffset], [yoffset]\n用于多功能按键的位置指示"));

        layout->addWidget(viewGroup);

        // ---------------------------------------------------------
        // 状态查询
        // ---------------------------------------------------------
        QGroupBox* queryGroup = new QGroupBox(tr("状态查询"), panel);
        QVBoxLayout* queryLayout = new QVBoxLayout(queryGroup);
        queryLayout->setSpacing(3);

        addSnippetButton(queryLayout, tr("获取鼠标位置"),
            "var pos = mapi.getmousepos();\nmapi.toast(\"x=\" + pos.x + \", y=\" + pos.y);",
            tr("获取当前鼠标位置\n返回 {x, y} 对象"));

        addSnippetButton(queryLayout, tr("获取按键位置"),
            "var pos = mapi.getkeypos(\"LMB\");\nif (pos.valid) mapi.click(pos.x, pos.y);",
            tr("获取指定按键映射的位置\n参数: 按键显示名称(如 LMB, Tab, =)\n返回 {x, y, valid} 对象"));

        addSnippetButton(queryLayout, tr("获取按键状态"),
            "var state = mapi.getKeyState(\"W\");\nif (state) { /* 按下中 */ }",
            tr("检查指定按键是否按下\n返回 0=未按下, 1=按下中"));

        addSnippetButton(queryLayout, tr("获取按钮位置"),
            "var pos = mapi.getbuttonpos(1);\nif (pos.valid) mapi.click(pos.x, pos.y);",
            tr("获取预定义按钮的位置\n参数: 按钮编号\n返回 {x, y, valid, name} 对象\n需先在「获取工具」中创建按钮"));

        addSnippetButton(queryLayout, tr("按编号滑动"),
            "mapi.swipeById(1, 200, 10);  // 滑动编号1, 200ms, 10步",
            tr("执行预定义的滑动路径\n参数: 滑动编号, 时长ms, 步数\n需先在「获取工具」中创建滑动"));

        layout->addWidget(queryGroup);

        // ---------------------------------------------------------
        // 图像识别
        // ---------------------------------------------------------
        QGroupBox* imageGroup = new QGroupBox(tr("图像识别"), panel);
        QVBoxLayout* imageLayout = new QVBoxLayout(imageGroup);
        imageLayout->setSpacing(3);

        // 区域找图按钮
        QPushButton* btnFindImage = new QPushButton(tr("区域找图 (findImage)"), this);
        btnFindImage->setToolTip(tr("在指定区域搜索模板图片\n返回 {found, x, y, confidence}"));
        btnFindImage->setCursor(Qt::PointingHandCursor);
        styleSnippetButton(btnFindImage);
        connect(btnFindImage, &QPushButton::clicked, [this]() {
            insertCode("// 区域找图\n"
                       "var result = mapi.findImage(\"模板图片\", 0, 0, 1, 1, 0.8);\n"
                       "if (result.found) {\n"
                       "    mapi.click(result.x, result.y);\n"
                       "    mapi.toast(\"找到目标: \" + result.confidence.toFixed(2));\n"
                       "} else {\n"
                       "    mapi.toast(\"未找到目标\");\n"
                       "}");
        });
        imageLayout->addWidget(btnFindImage);

        // 按选区编号找图按钮
        QPushButton* btnFindImageRegion = new QPushButton(tr("按选区找图"), this);
        btnFindImageRegion->setToolTip(tr("使用预定义选区编号搜索模板图片\n需先在「获取工具」中创建选区"));
        btnFindImageRegion->setCursor(Qt::PointingHandCursor);
        styleSnippetButton(btnFindImageRegion);
        connect(btnFindImageRegion, &QPushButton::clicked, [this]() {
            insertCode("// 按选区编号找图 (需先在「获取工具」中创建选区)\n"
                       "var result = mapi.findImageByRegion(\"模板图片\", 1, 0.8);  // 选区编号1, 置信度0.8\n"
                       "if (result.found) {\n"
                       "    mapi.click(result.x, result.y);\n"
                       "    mapi.toast(\"找到目标: \" + result.confidence.toFixed(2));\n"
                       "} else {\n"
                       "    mapi.toast(\"未找到目标\");\n"
                       "}");
        });
        imageLayout->addWidget(btnFindImageRegion);

        layout->addWidget(imageGroup);

        // ---------------------------------------------------------
        // 工具函数
        // ---------------------------------------------------------
        QGroupBox* utilGroup = new QGroupBox(tr("工具"), panel);
        QVBoxLayout* utilLayout = new QVBoxLayout(utilGroup);
        utilLayout->setSpacing(3);

        addSnippetButton(utilLayout, tr("延时 (sleep)"),
            "mapi.sleep(100);  // 暂停 100 毫秒",
            tr("脚本暂停执行指定毫秒\n会检查中断标志，可被 stop() 中断"));

        addSnippetButton(utilLayout, tr("弹窗提示 (toast)"),
            "mapi.toast(\"提示信息\", 3000);  // 显示 3 秒",
            tr("显示浮动提示信息\n参数: 消息内容, 显示时长(ms)\n同一按键的消息会更新而非新增"));

        addSnippetButton(utilLayout, tr("日志输出 (log)"),
            "mapi.log(\"调试信息\");  // 输出到控制台",
            tr("输出日志到控制台\n用于脚本调试"));

        addSnippetButton(utilLayout, tr("检查按下状态 (isPress)"),
            "if (mapi.isPress()) {\n    // 按下时执行\n} else {\n    // 松开时执行\n}",
            tr("检查当前触发状态\ntrue = 按下, false = 松开\n用于区分按下/松开逻辑"));

        addSnippetButton(utilLayout, tr("检查中断 (isInterrupted)"),
            "if (mapi.isInterrupted()) return;  // 被中断则退出",
            tr("检查脚本是否被中断\n用于长循环中提前退出"));

        addSnippetButton(utilLayout, tr("停止脚本 (stop)"),
            "mapi.stop();  // 停止当前脚本执行",
            tr("停止当前 Worker 脚本\n会触发中断标志"));

        layout->addWidget(utilGroup);

        // ---------------------------------------------------------
        // 全局状态
        // ---------------------------------------------------------
        QGroupBox* globalGroup = new QGroupBox(tr("全局状态"), panel);
        QVBoxLayout* globalLayout = new QVBoxLayout(globalGroup);
        globalLayout->setSpacing(3);

        addSnippetButton(globalLayout, tr("设置全局变量"),
            "mapi.setGlobal(\"模式\", \"攻击\");  // 设置全局状态",
            tr("设置全局状态变量（线程安全）\n参数: 键名, 值\n可在不同脚本间共享"));

        addSnippetButton(globalLayout, tr("获取全局变量"),
            "var mode = mapi.getGlobal(\"模式\");\nif (mode === \"攻击\") { /* ... */ }",
            tr("获取全局状态变量\n参数: 键名\n不存在则返回 undefined"));

        layout->addWidget(globalGroup);

        // ---------------------------------------------------------
        // 代码结构
        // ---------------------------------------------------------
        QGroupBox* codeGroup = new QGroupBox(tr("代码结构"), panel);
        QVBoxLayout* codeLayout = new QVBoxLayout(codeGroup);
        codeLayout->setSpacing(3);

        addSnippetButton(codeLayout, tr("if 条件判断"),
            "// 条件判断：当 condition 为 true 时执行大括号内的代码\nif (condition) {\n    // 条件为真时执行的代码\n}",
            tr("条件判断语句\n当条件为 true 时执行代码块"));

        addSnippetButton(codeLayout, tr("if-else 分支"),
            "// 条件分支：根据条件选择执行不同的代码块\nif (condition) {\n    // 条件为真时执行\n} else {\n    // 条件为假时执行\n}",
            tr("条件分支语句\n根据条件选择执行哪个代码块"));

        addSnippetButton(codeLayout, tr("for 循环"),
            "// for 循环：重复执行指定次数\n// i=0 开始, i<10 循环10次, i++ 每次加1\nfor (var i = 0; i < 10; i++) {\n    // 循环体，会执行10次\n    mapi.delay(50);\n}",
            tr("计数循环\n重复执行固定次数"));

        addSnippetButton(codeLayout, tr("while 循环"),
            "// while 循环：当条件为 true 时持续执行\n// 注意：务必添加 delay 避免死循环\nwhile (condition) {\n    // 循环体\n    mapi.delay(100);  // 必须添加延时\n}",
            tr("条件循环\n当条件为真时持续执行\n注意添加延时避免死循环"));

        addSnippetButton(codeLayout, tr("定义函数"),
            "// 定义函数：封装可复用的代码\nfunction myFunc(param) {\n    // 函数体\n    return result;\n}",
            tr("自定义函数\n封装可复用的代码"));

        addSnippetButton(codeLayout, tr("导入模块 (函数式)"),
            "// 从脚本目录加载模块 (函数式)\nvar m = mapi.loadModule('examples.js');\nm.示例函数();  // 调用模块中的函数",
            tr("从 keymap/scripts 目录加载模块\n模块导出函数供调用\n适合工具函数集合"));



        addSnippetButton(codeLayout, tr("导入模块 (对象式)"),
            "var m = mapi.loadModule('mymodule.js');\nvar obj = new m.create().demo1(); ",
            tr("从 keymap/scripts 目录加载模块\n使用工厂函数创建对象\n避免 class/new 的兼容性问题"));

        layout->addWidget(codeGroup);

        layout->addStretch();
        scrollArea->setWidget(panel);
        return scrollArea;
    }

    // =========================================================
    // 添加快捷指令按钮
    // =========================================================
    void styleSnippetButton(QPushButton* btn) {
        btn->setStyleSheet(
            "QPushButton {"
            "  text-align: left;"
            "  padding: 6px 10px;"
            "  border: 1px solid #3f3f46;"
            "  border-radius: 6px;"
            "  background-color: #27272a;"
            "  color: #e4e4e7;"
            "  font-size: 9pt;"
            "}"
            "QPushButton:hover {"
            "  background-color: #3f3f46;"
            "  border-color: #6366f1;"
            "  color: #fafafa;"
            "}"
            "QPushButton:pressed {"
            "  background-color: #6366f1;"
            "  color: #ffffff;"
            "}"
        );
    }

    void addSnippetButton(QVBoxLayout* layout, const QString& label,
                          const QString& code, const QString& tooltip) {
        QPushButton* btn = new QPushButton(label, this);
        btn->setToolTip(tooltip);
        btn->setCursor(Qt::PointingHandCursor);
        styleSnippetButton(btn);
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

    void onSave() {
        m_script = m_editor->toPlainText();
        accept();
    }

    void onCustomRegion() {
        if (!m_selectionEditorDialog) {
            // parent 设为 nullptr 使其成为独立顶层窗口，不受模态对话框阻塞
            m_selectionEditorDialog = new SelectionEditorDialog(nullptr);
            connect(m_selectionEditorDialog, &SelectionEditorDialog::codeSnippetGenerated,
                    this, [this](const QString& code) {
                        insertCode(code);
                        raise();
                        activateWindow();
                    });
            connect(m_selectionEditorDialog, &QDialog::destroyed,
                    this, [this]() { m_selectionEditorDialog = nullptr; });
        }
        m_selectionEditorDialog->setFrameGrabCallback(m_frameGrabCallback);
        m_selectionEditorDialog->show();
        m_selectionEditorDialog->raise();
        m_selectionEditorDialog->activateWindow();
    }

private:
    CodeEditor* m_editor;
    QString m_script;
    FrameGrabFunc m_frameGrabCallback;
    SelectionEditorDialog* m_selectionEditorDialog = nullptr;
};

#endif // SCRIPTEDITORDIALOG_H
