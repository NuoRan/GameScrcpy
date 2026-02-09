#ifndef KEYMAPITEMS_H
#define KEYMAPITEMS_H

#include "KeyMapBase.h"
#include "scripteditordialog.h"
#include "videoform.h"
#include <QPainter>
#include <QCursor>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QTimer>
#include <QKeySequence>
#include <QGraphicsSceneMouseEvent>
#include <QtMath>
#include <QDebug>
#include <QMetaEnum>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QTextEdit>
#include <QPushButton>
#include <QDir>
#include <QFile>
#include <QDesktopServices>
#include <QUrl>
#include <QTextStream>
#include <QInputDialog>
#include <QCheckBox>

// ---------------------------------------------------------
// 辅助工具类：类型与字符串的转换 / Helper: Type-String Conversion
// ---------------------------------------------------------
class KeyMapHelper {
public:
    static KeyMapType getTypeFromString(const QString& typeStr) {
        if (typeStr == "KMT_STEER_WHEEL") return KMT_STEER_WHEEL;
        if (typeStr == "KMT_SCRIPT") return KMT_SCRIPT;
        if (typeStr == "KMT_CAMERA_MOVE") return KMT_CAMERA_MOVE;
        if (typeStr == "KMT_FREE_LOOK") return KMT_FREE_LOOK;
        return KMT_INVALID;
    }

    static QString getStringFromType(KeyMapType type) {
        switch(type) {
        case KMT_STEER_WHEEL: return "KMT_STEER_WHEEL";
        case KMT_SCRIPT: return "KMT_SCRIPT";
        case KMT_CAMERA_MOVE: return "KMT_CAMERA_MOVE";
        case KMT_FREE_LOOK: return "KMT_FREE_LOOK";
        default: return "KMT_INVALID";
        }
    }

    // 将按键转换为存储用的字符串（支持修饰键）
    static QString keyToString(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        // 修饰键单独处理
        switch (key) {
        case Qt::Key_Control: return "Key_Control";
        case Qt::Key_Shift: return "Key_Shift";
        case Qt::Key_Alt: return "Key_Alt";
        case Qt::Key_Meta: return "Key_Meta";
        }
        // 普通按键
        if (modifiers != Qt::NoModifier) {
            return QKeySequence(key | modifiers).toString(QKeySequence::PortableText);
        }
        QMetaEnum m = QMetaEnum::fromType<Qt::Key>();
        const char* s = m.valueToKey(key);
        return s ? QString(s) : QKeySequence(key).toString();
    }

    // 将按键字符串转换为显示用的字符串（符号化）
    static QString keyToDisplay(const QString& keyStr) {
        QString t = keyStr;
        if (t.startsWith("Key_")) t = t.mid(4);
        // 鼠标按键简化
        if (t == "LeftButton" || t == "Left") return "LMB";
        if (t == "RightButton" || t == "Right") return "RMB";
        if (t == "MiddleButton" || t == "Middle") return "MMB";
        // 滚轮
        if (t == "WheelUp") return "滚上";
        if (t == "WheelDown") return "滚下";
        // 符号替换 - 注意区分主键盘和小键盘
        if (t == "Equal") return "=";           // 主键盘 = 键
        if (t == "Plus") return "+";            // 小键盘 + 键
        if (t == "Minus") return "-";
        if (t == "Asterisk") return "*";
        if (t == "Slash") return "/";
        if (t == "QuoteLeft") return "`";       // 反引号
        if (t == "AsciiTilde") return "~";      // 波浪号
        if (t == "Backslash") return "\\";
        if (t == "BracketLeft") return "[";
        if (t == "BracketRight") return "]";
        if (t == "Semicolon") return ";";
        if (t == "Apostrophe") return "'";
        if (t == "Comma") return ",";
        if (t == "Period") return ".";
        if (t == "Space") return "Space";
        if (t == "Tab") return "Tab";
        if (t == "Return" || t == "Enter") return "Enter";
        if (t == "Backspace") return "Backspace";
        if (t == "Escape") return "Esc";
        if (t == "Control") return "Ctrl";
        if (t == "Alt") return "Alt";
        if (t == "Shift") return "Shift";
        if (t == "Meta") return "Win";
        return t;
    }
};

class KeyMapItemSteerWheel;

// ---------------------------------------------------------
// 轮盘子项类 (SteerWheelSubItem)
// 代表WASD方向键中的某一个具体方向按钮
// ---------------------------------------------------------
class SteerWheelSubItem : public QGraphicsObject
{
    Q_OBJECT
public:
    enum Direction { Dir_Up, Dir_Down, Dir_Left, Dir_Right };
    SteerWheelSubItem(Direction dir, KeyMapItemSteerWheel* parentWheel);
    QRectF boundingRect() const override { return QRectF(-15, -15, 30, 30); }

    // 按键绑定与获取
    void setKey(const QString& key) { m_key = key; update(); }
    QString getKey() const { return m_key; }

    // 编辑状态控制
    void setEditing(bool edit);
    bool isEditing() const { return m_isEditing; }

    // 输入事件处理
    void inputKey(QKeyEvent* event);
    void inputMouse(Qt::MouseButton button);
    void inputWheel(int delta);

    void setConflicted(bool conflicted) { if (m_isConflicted != conflicted) { m_isConflicted = conflicted; update(); } }
protected:
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *e) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *e) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override;
private:
    Direction m_dir; KeyMapItemSteerWheel* m_parentWheel; QString m_key;
    bool m_isEditing = false; bool m_showCursor = false; bool m_isConflicted = false;
    QString m_displayKey; QTimer* m_cursorTimer;
};

// ---------------------------------------------------------
// 轮盘主项类 (KeyMapItemSteerWheel)
// 包含四个方向子项，管理整体轮盘逻辑
// ---------------------------------------------------------
class KeyMapItemSteerWheel : public KeyMapItemBase
{
    Q_OBJECT
public:
    friend class SteerWheelSubItem;
    KeyMapItemSteerWheel(QGraphicsItem *parent = nullptr);
    KeyMapType typeId() const override { return KMT_STEER_WHEEL; }

    // 设置四个方向的按键
    void setKeys(const QString& u, const QString& d, const QString& l, const QString& r);
    // 设置四个方向的偏移量（控制轮盘大小范围）
    void setOffsets(double u, double d, double l, double r);
    // 根据子项位置更新偏移量
    void updateOffsetFromSubItem(SteerWheelSubItem::Direction dir, const QPointF& localPos);
    void updateSubItemsPos();

    // 获取子项信息
    SteerWheelSubItem* getSubItemAt(const QPointF& pos);
    QString getUpKey() const { return m_subUp->getKey(); }
    QString getDownKey() const { return m_subDown->getKey(); }
    QString getLeftKey() const { return m_subLeft->getKey(); }
    QString getRightKey() const { return m_subRight->getKey(); }

    // 设置子项冲突状态
    void setSubItemConflicted(int dir, bool conflicted) {
        if(dir==0) m_subUp->setConflicted(conflicted); if(dir==1) m_subDown->setConflicted(conflicted);
        if(dir==2) m_subLeft->setConflicted(conflicted); if(dir==3) m_subRight->setConflicted(conflicted);
    }

    // 序列化支持
    QJsonObject toJson() const override; void fromJson(const QJsonObject& json) override;
    void resize(qreal w, qreal h);
protected:
    QRectF boundingRect() const override { return m_rect; }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;
    QPainterPath shape() const override;
private:
    QRectF m_rect; SteerWheelSubItem* m_subUp; SteerWheelSubItem* m_subDown;
    SteerWheelSubItem* m_subLeft; SteerWheelSubItem* m_subRight;
    double m_leftOffset = 0.15; double m_rightOffset = 0.15; double m_upOffset = 0.15; double m_downOffset = 0.15;
};

// ---------------------------------------------------------
// 脚本键位类 (KeyMapItemScript)
// 支持一键执行预设的宏脚本
// ---------------------------------------------------------
class KeyMapItemScript : public KeyMapItemBase
{
    Q_OBJECT
public:
    KeyMapItemScript(QGraphicsItem *parent = nullptr) : KeyMapItemBase(parent) {
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
        m_key = "";  // 默认为空，需要用户设置
        m_comment = "Script";

        // 光标闪烁定时器，用于编辑模式
        m_cursorTimer = new QTimer(this);
        m_cursorTimer->setInterval(600);
        connect(m_cursorTimer, &QTimer::timeout, this, [this](){ m_showCursor = !m_showCursor; update(); });
    }

    KeyMapType typeId() const override { return KMT_SCRIPT; }
    QString getScript() const { return m_script; }

    // 进入/退出按键编辑模式
    void setEditing(bool edit) {
        if (m_isEditing == edit) return;
        m_isEditing = edit;
        if (m_isEditing) {
            m_displayKey = "";
            m_showCursor = true;
            m_cursorTimer->start();
            setSelected(true);
        } else {
            m_cursorTimer->stop();
            m_showCursor = false;
        }
        update();
    }
    bool isEditing() const { return m_isEditing; }

    // 处理按键录入（支持修饰键）
    void inputKey(QKeyEvent* event) {
        int key = event->key();
        if (key == Qt::Key_unknown) return;
        m_key = KeyMapHelper::keyToString(key, event->modifiers());
        m_displayKey = m_key;
        update();
    }

    // 处理鼠标按键录入
    void inputMouse(Qt::MouseButton button) {
        QString keyName;
        switch (button) {
        case Qt::LeftButton: keyName = "LeftButton"; break;
        case Qt::RightButton: keyName = "RightButton"; break;
        case Qt::MiddleButton: keyName = "MiddleButton"; break;
        case Qt::XButton1: keyName = "SideButton1"; break;
        case Qt::XButton2: keyName = "SideButton2"; break;
        default: return;
        }
        if (!keyName.isEmpty()) { m_key = keyName; m_displayKey = keyName; update(); }
    }

    // 处理滚轮录入
    void inputWheel(int delta) {
        QString keyName = (delta > 0) ? "WheelUp" : "WheelDown";
        m_key = keyName;
        m_displayKey = keyName;
        update();
    }

    // JSON序列化
    QJsonObject toJson() const override {
        QJsonObject json;
        json["type"] = "KMT_SCRIPT";
        QPointF r = getNormalizedPos(scene()?scene()->sceneRect().size():QSizeF(1,1));
        QJsonObject pos;
        pos["x"]=QString::number(r.x(),'f',4).toDouble();
        pos["y"]=QString::number(r.y(),'f',4).toDouble();
        json["pos"] = pos;
        json["key"] = m_key;
        json["script"] = m_script;
        return json;
    }

    void fromJson(const QJsonObject& json) override {
        if (json.contains("key")) m_key = json["key"].toString();
        if (json.contains("script")) m_script = json["script"].toString();
    }

protected:
    QRectF boundingRect() const override { return QRectF(-25, -25, 50, 50); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override {
        Q_UNUSED(option); Q_UNUSED(widget);
        painter->setRenderHint(QPainter::Antialiasing);

        // 背景颜色：冲突显示红，编辑显示灰，选中显示橙，普通显示黑半透明
        QColor bg = m_isConflicted ? QColor(255, 50, 50, 200) :
                        (m_isEditing ? QColor(40,40,40,230) :
                             (isSelected() ? QColor(255, 170, 0, 200) : QColor(0, 0, 0, 150)));

        painter->setPen(Qt::NoPen);
        painter->setBrush(bg);
        painter->drawEllipse(boundingRect());

        // 边框绘制
        painter->setPen(m_isEditing ? QPen(Qt::white, 1) : QPen(Qt::white, 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(boundingRect());

        // 文字绘制
        painter->setPen(Qt::white);
        QFont font = painter->font();
        font.setBold(true);

        QString t = m_isEditing ? (KeyMapHelper::keyToDisplay(m_displayKey) + (m_showCursor?"|":"")) : KeyMapHelper::keyToDisplay(m_key);

        // 根据文字长度自适应字体大小
        int fontSize = 10;
        if (t.length() > 6) fontSize = 7;
        else if (t.length() > 4) fontSize = 8;
        else if (t.length() > 2) fontSize = 9;
        font.setPointSize(fontSize);
        painter->setFont(font);

        painter->drawText(boundingRect().adjusted(2, 2, -2, -2), Qt::AlignCenter | Qt::TextWordWrap, t.isEmpty()?"?":t);

        if (!m_isEditing) drawGear(painter);
    }

    // 鼠标点击事件：左键点击打开脚本编辑器
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override {
        if (m_isEditing) { event->accept(); return; }

        if (event->button() == Qt::LeftButton) {
            QPointF p = event->pos();
            if (p.x() > 5 && p.y() > 5) {
                openScriptEditor();
                event->accept();
                return;
            }
        }
        KeyMapItemBase::mousePressEvent(event);
    }

private:
    // 绘制设置齿轮图标
    void drawGear(QPainter* painter) {
        painter->save();
        painter->translate(14, 14);
        painter->setPen(QPen(Qt::lightGray, 1.5));
        painter->setBrush(Qt::darkGray);
        painter->drawEllipse(QPoint(0,0), 6, 6);
        painter->setBrush(Qt::lightGray);
        painter->drawEllipse(QPoint(0,0), 2, 2);
        for(int i=0; i<8; ++i) { painter->rotate(45); painter->drawLine(0, 6, 0, 8); }
        painter->restore();
    }

    // 打开脚本编辑对话框
    void openScriptEditor() {
        ScriptEditorDialog dlg(m_script);

        // 尝试获取 VideoForm 以设置帧获取回调
        if (scene()) {
            QList<QGraphicsView*> views = scene()->views();
            if (!views.isEmpty()) {
                // 向上遍历父控件链查找 VideoForm
                QWidget* w = views.first()->parentWidget();
                while (w) {
                    VideoForm* videoForm = qobject_cast<VideoForm*>(w);
                    if (videoForm) {
                        dlg.setFrameGrabCallback([videoForm]() -> QImage {
                            return videoForm->grabCurrentFrame();
                        });
                        break;
                    }
                    w = w->parentWidget();
                }
            }
        }

        if (dlg.exec() == QDialog::Accepted) {
            m_script = dlg.getScript();
        }
    }

private:
    QString m_script;
    bool m_isEditing = false; bool m_showCursor = false;
    QString m_displayKey; QTimer* m_cursorTimer;
};

// ---------------------------------------------------------
// 视角控制键位类 (KeyMapItemCamera)
// 用于FPS游戏中的鼠标视角移动，支持X/Y轴灵敏度调节
// ---------------------------------------------------------
class KeyMapItemCamera : public KeyMapItemBase
{
    Q_OBJECT
public:
    KeyMapItemCamera(QGraphicsItem *parent = nullptr) : KeyMapItemBase(parent) {
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
        m_key = "";  // 默认为空，需要用户设置
        m_comment = "Camera";

        m_cursorTimer = new QTimer(this);
        m_cursorTimer->setInterval(600);
        connect(m_cursorTimer, &QTimer::timeout, this, [this](){ m_showCursor = !m_showCursor; update(); });
    }

    KeyMapType typeId() const override { return KMT_CAMERA_MOVE; }

    enum EditMode { Edit_None, Edit_Key, Edit_X, Edit_Y };

    // 根据点击位置判断编辑模式：编辑按键、X轴灵敏度或Y轴灵敏度
    void startEditing(const QPointF& pos) {
        bool wasEditing = m_isEditing;
        m_isEditing = true;

        EditMode newMode = Edit_None;
        if (pos.x() < -20) newMode = Edit_X;
        else if (pos.x() > 20) newMode = Edit_Y;
        else newMode = Edit_Key;

        if (!wasEditing || m_editMode != newMode) {
            m_editMode = newMode;
            // 点击 XY 时清空 buffer，实现"全选"效果
            if (m_editMode == Edit_X) m_inputBuffer = "";
            else if (m_editMode == Edit_Y) m_inputBuffer = "";
            else m_displayKey = "";

            m_showCursor = true;
            if (!m_cursorTimer->isActive()) m_cursorTimer->start();
            setSelected(true);
            update();
        }
    }

    void setEditing(bool edit) {
        if (m_isEditing == edit) return;
        m_isEditing = edit;
        if (m_isEditing) {
            m_editMode = Edit_Key;
            m_displayKey = "";
            m_showCursor = true;
            m_cursorTimer->start();
            setSelected(true);
        } else {
            m_editMode = Edit_None;
            m_cursorTimer->stop();
            m_showCursor = false;
        }
        update();
    }
    bool isEditing() const { return m_isEditing; }

    // 处理键盘输入：区别处理绑定按键和输入数字（灵敏度）
    void inputKey(QKeyEvent* event) {
        if (m_editMode == Edit_Key) {
            int key = event->key();
            if (key == Qt::Key_unknown) return;
            m_key = KeyMapHelper::keyToString(key, event->modifiers());
            m_displayKey = m_key;
            update();
        } else if (m_editMode == Edit_X || m_editMode == Edit_Y) {
            if (event->key() == Qt::Key_Backspace) {
                if (!m_inputBuffer.isEmpty()) m_inputBuffer.chop(1);
            } else {
                QString text = event->text();
                if (!text.isEmpty() && (text.at(0).isDigit() || text.at(0) == '.')) {
                    m_inputBuffer.append(text);
                }
            }

            // 如果 buffer 为空，保持原值
            if (!m_inputBuffer.isEmpty()) {
            double val = m_inputBuffer.toDouble();
            if (m_editMode == Edit_X) m_speedX = val;
            else m_speedY = val;
            }
            update();
        }
    }

    void inputMouse(Qt::MouseButton button) {
        if (m_editMode != Edit_Key) return;

        QString keyName;
        switch (button) {
        case Qt::LeftButton: keyName = "LeftButton"; break;
        case Qt::RightButton: keyName = "RightButton"; break;
        case Qt::MiddleButton: keyName = "MiddleButton"; break;
        case Qt::XButton1: keyName = "SideButton1"; break;
        case Qt::XButton2: keyName = "SideButton2"; break;
        default: return;
        }
        if (!keyName.isEmpty()) { m_key = keyName; m_displayKey = keyName; update(); }
    }

    void inputWheel(int delta) {
        if (m_editMode != Edit_Key) return;
        QString keyName = (delta > 0) ? "WheelUp" : "WheelDown";
        m_key = keyName;
        m_displayKey = keyName;
        update();
    }

    QJsonObject toJson() const override {
        QJsonObject json;
        json["type"] = "KMT_CAMERA_MOVE";
        QPointF r = getNormalizedPos(scene()?scene()->sceneRect().size():QSizeF(1,1));
        QJsonObject pos;
        pos["x"]=QString::number(r.x(),'f',4).toDouble();
        pos["y"]=QString::number(r.y(),'f',4).toDouble();
        json["pos"] = pos;
        json["key"] = m_key;
        json["speedRatioX"] = m_speedX;
        json["speedRatioY"] = m_speedY;
        return json;
    }

    void fromJson(const QJsonObject& json) override {
        if (json.contains("key")) m_key = json["key"].toString();
        if (json.contains("speedRatioX")) m_speedX = json["speedRatioX"].toDouble();
        if (json.contains("speedRatioY")) m_speedY = json["speedRatioY"].toDouble();
    }

    QString getKey() const override { return m_key; }

protected:
    QRectF boundingRect() const override { return QRectF(-60, -25, 120, 50); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override {
        Q_UNUSED(option); Q_UNUSED(widget);
        painter->setRenderHint(QPainter::Antialiasing);

        QColor bg = m_isConflicted ? QColor(255, 50, 50, 200) :
                        (m_isEditing ? QColor(40,40,40,230) :
                             (isSelected() ? QColor(0, 150, 136, 200) : QColor(0, 0, 0, 150)));

        painter->setPen(Qt::NoPen);
        painter->setBrush(bg);
        painter->drawRoundedRect(boundingRect(), 5, 5);

        painter->setPen(m_isEditing ? QPen(Qt::white, 1) : QPen(Qt::white, 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(boundingRect(), 5, 5);

        // 分隔线
        painter->setPen(QPen(Qt::lightGray, 1));
        painter->drawLine(-20, -25, -20, 25);
        painter->drawLine(20, -25, 20, 25);

        // 绘制文字信息
        QFont font = painter->font();
        font.setBold(true);
        painter->setPen(Qt::white);

        // X轴灵敏度
        font.setPointSize(8);
        painter->setFont(font);
        QString xStr = (m_isEditing && m_editMode == Edit_X) ? (m_inputBuffer + (m_showCursor?"|":"")) : QString::number(m_speedX);
        painter->drawText(QRectF(-60, -25, 40, 50), Qt::AlignCenter, QString("X\n%1").arg(xStr));

        // Y轴灵敏度
        QString yStr = (m_isEditing && m_editMode == Edit_Y) ? (m_inputBuffer + (m_showCursor?"|":"")) : QString::number(m_speedY);
        painter->drawText(QRectF(20, -25, 40, 50), Qt::AlignCenter, QString("Y\n%1").arg(yStr));

        // 激活按键
        font.setPointSize(10);
        painter->setFont(font);
        QString t = (m_isEditing && m_editMode == Edit_Key) ? (KeyMapHelper::keyToDisplay(m_displayKey) + (m_showCursor?"|":"")) : KeyMapHelper::keyToDisplay(m_key);
        painter->drawText(QRectF(-20, -25, 40, 50), Qt::AlignCenter, t.isEmpty()?"?":t);
        // 已移除底部图标
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *event) override {
        if (m_isEditing) {
            event->ignore();
            return;
        }
        KeyMapItemBase::mousePressEvent(event);
    }

private:
    double m_speedX = 1.0;
    double m_speedY = 1.0;
    bool m_isEditing = false; bool m_showCursor = false;
    EditMode m_editMode = Edit_None;
    QString m_displayKey;
    QString m_inputBuffer;
    QTimer* m_cursorTimer;
};

// ---------------------------------------------------------
// 小眼睛自由视角键位类 (KeyMapItemFreeLook)
// 按住热键后启用自由视角，松开后恢复
// 与Camera不同：无边缘修正、无空闲回中
// ---------------------------------------------------------
class KeyMapItemFreeLook : public KeyMapItemBase
{
    Q_OBJECT
public:
    KeyMapItemFreeLook(QGraphicsItem *parent = nullptr) : KeyMapItemBase(parent) {
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
        m_key = "";  // 默认为空，需要用户设置
        m_comment = "FreeLook";

        m_cursorTimer = new QTimer(this);
        m_cursorTimer->setInterval(600);
        connect(m_cursorTimer, &QTimer::timeout, this, [this](){ m_showCursor = !m_showCursor; update(); });
    }

    KeyMapType typeId() const override { return KMT_FREE_LOOK; }

    enum EditMode { Edit_None, Edit_Key, Edit_X, Edit_Y };

    void startEditing(const QPointF& pos) {
        bool wasEditing = m_isEditing;
        m_isEditing = true;

        EditMode newMode = Edit_None;
        if (pos.x() < -15) newMode = Edit_X;
        else if (pos.x() > 15) newMode = Edit_Y;
        else newMode = Edit_Key;

        if (!wasEditing || m_editMode != newMode) {
            m_editMode = newMode;
            // 点击 XY 时清空 buffer，实现"全选"效果
            if (m_editMode == Edit_X) m_inputBuffer = "";
            else if (m_editMode == Edit_Y) m_inputBuffer = "";
            else m_displayKey = "";

            m_showCursor = true;
            if (!m_cursorTimer->isActive()) m_cursorTimer->start();
            setSelected(true);
            update();
        }
    }

    void setEditing(bool edit) {
        if (m_isEditing == edit) return;
        m_isEditing = edit;
        if (m_isEditing) {
            m_editMode = Edit_Key;
            m_displayKey = "";
            m_showCursor = true;
            m_cursorTimer->start();
            setSelected(true);
        } else {
            m_editMode = Edit_None;
            m_cursorTimer->stop();
            m_showCursor = false;
        }
        update();
    }
    bool isEditing() const { return m_isEditing; }

    void inputKey(QKeyEvent* event) {
        if (m_editMode == Edit_Key) {
            int key = event->key();
            if (key == Qt::Key_unknown) return;
            m_key = KeyMapHelper::keyToString(key, event->modifiers());
            m_displayKey = m_key;
            update();
        } else if (m_editMode == Edit_X || m_editMode == Edit_Y) {
            if (event->key() == Qt::Key_Backspace) {
                if (!m_inputBuffer.isEmpty()) m_inputBuffer.chop(1);
            } else {
                QString text = event->text();
                if (!text.isEmpty() && (text.at(0).isDigit() || text.at(0) == '.')) {
                    m_inputBuffer.append(text);
                }
            }

            if (!m_inputBuffer.isEmpty()) {
                double val = m_inputBuffer.toDouble();
                if (m_editMode == Edit_X) m_speedX = val;
                else m_speedY = val;
            }
            update();
        }
    }

    void inputMouse(Qt::MouseButton button) {
        if (m_editMode != Edit_Key) return;

        QString keyName;
        switch (button) {
        case Qt::LeftButton: keyName = "LeftButton"; break;
        case Qt::RightButton: keyName = "RightButton"; break;
        case Qt::MiddleButton: keyName = "MiddleButton"; break;
        case Qt::XButton1: keyName = "SideButton1"; break;
        case Qt::XButton2: keyName = "SideButton2"; break;
        default: return;
        }
        if (!keyName.isEmpty()) { m_key = keyName; m_displayKey = keyName; update(); }
    }

    void inputWheel(int delta) {
        if (m_editMode != Edit_Key) return;
        QString keyName = (delta > 0) ? "WheelUp" : "WheelDown";
        m_key = keyName;
        m_displayKey = keyName;
        update();
    }

    bool resetViewOnRelease() const { return m_resetViewOnRelease; }
    void setResetViewOnRelease(bool reset) { m_resetViewOnRelease = reset; }

    QJsonObject toJson() const override {
        QJsonObject json;
        json["type"] = "KMT_FREE_LOOK";
        QPointF r = getNormalizedPos(scene()?scene()->sceneRect().size():QSizeF(1,1));
        QJsonObject pos;
        pos["x"]=QString::number(r.x(),'f',4).toDouble();
        pos["y"]=QString::number(r.y(),'f',4).toDouble();
        json["startPos"] = pos;  // 用于游戏逻辑解析
        json["pos"] = pos;       // 用于 UI 加载位置
        json["key"] = m_key;
        json["speedRatioX"] = m_speedX;
        json["speedRatioY"] = m_speedY;
        json["resetViewOnRelease"] = m_resetViewOnRelease;
        return json;
    }

    void fromJson(const QJsonObject& json) override {
        if (json.contains("key")) m_key = json["key"].toString();
        if (json.contains("speedRatioX")) m_speedX = json["speedRatioX"].toDouble();
        if (json.contains("speedRatioY")) m_speedY = json["speedRatioY"].toDouble();
        if (json.contains("resetViewOnRelease")) m_resetViewOnRelease = json["resetViewOnRelease"].toBool();
    }

    QString getKey() const override { return m_key; }

protected:
    // 椭圆形外观
    QRectF boundingRect() const override { return QRectF(-50, -20, 100, 40); }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override {
        Q_UNUSED(option); Q_UNUSED(widget);
        painter->setRenderHint(QPainter::Antialiasing);

        // 使用不同的颜色与Camera区分：紫色调
        QColor bg = m_isConflicted ? QColor(255, 50, 50, 200) :
                        (m_isEditing ? QColor(40,40,40,230) :
                             (isSelected() ? QColor(156, 39, 176, 200) : QColor(0, 0, 0, 150)));

        painter->setPen(Qt::NoPen);
        painter->setBrush(bg);
        painter->drawEllipse(boundingRect());  // 椭圆形

        painter->setPen(m_isEditing ? QPen(Qt::white, 1) : QPen(Qt::white, 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(boundingRect());

        QFont font = painter->font();
        font.setBold(true);
        painter->setPen(Qt::white);

        // X轴灵敏度（左侧）
        font.setPointSize(7);
        painter->setFont(font);
        QString xStr = (m_isEditing && m_editMode == Edit_X) ? (m_inputBuffer + (m_showCursor?"|":"")) : QString::number(m_speedX);
        painter->drawText(QRectF(-48, -18, 30, 36), Qt::AlignCenter, QString("X\n%1").arg(xStr));

        // Y轴灵敏度（右侧）
        QString yStr = (m_isEditing && m_editMode == Edit_Y) ? (m_inputBuffer + (m_showCursor?"|":"")) : QString::number(m_speedY);
        painter->drawText(QRectF(18, -18, 30, 36), Qt::AlignCenter, QString("Y\n%1").arg(yStr));

        // 激活按键（中间）
        font.setPointSize(9);
        painter->setFont(font);
        QString t = (m_isEditing && m_editMode == Edit_Key) ? (KeyMapHelper::keyToDisplay(m_displayKey) + (m_showCursor?"|":"")) : KeyMapHelper::keyToDisplay(m_key);
        painter->drawText(QRectF(-15, -18, 30, 36), Qt::AlignCenter, t.isEmpty()?"?":t);
        // 绘制设置齿轮图标（中间顶部）
        drawGear(painter);
    }

    // 绘制设置齿轮图标（中间顶部）
    void drawGear(QPainter* painter) {
        painter->save();
        painter->translate(0, -14);  // 中间顶部位置
        painter->setPen(QPen(Qt::lightGray, 1.2));
        painter->setBrush(Qt::darkGray);
        painter->drawEllipse(QPoint(0,0), 5, 5);
        painter->setBrush(Qt::lightGray);
        painter->drawEllipse(QPoint(0,0), 2, 2);
        for(int i=0; i<8; ++i) { painter->rotate(45); painter->drawLine(0, 5, 0, 7); }
        painter->restore();
    }

    // 打开设置对话框
    void openSettingsDialog() {
        QDialog dlg;
        dlg.setWindowTitle("小眼睛设置");
        dlg.setFixedSize(280, 120);

        auto* layout = new QVBoxLayout(&dlg);
        layout->setSpacing(12);
        layout->setContentsMargins(16, 16, 16, 16);

        // 复选框：松开时是否重置视角
        auto* checkBox = new QCheckBox("松开热键时重置视角", &dlg);
        checkBox->setChecked(m_resetViewOnRelease);
        checkBox->setToolTip("启用后，松开小眼睛热键时会自动将视角重置到初始位置");
        layout->addWidget(checkBox);

        // 按钮
        auto* btnLayout = new QHBoxLayout();
        auto* okBtn = new QPushButton("确定", &dlg);
        auto* cancelBtn = new QPushButton("取消", &dlg);
        btnLayout->addStretch();
        btnLayout->addWidget(okBtn);
        btnLayout->addWidget(cancelBtn);
        layout->addLayout(btnLayout);

        QObject::connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
        QObject::connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

        if (dlg.exec() == QDialog::Accepted) {
            m_resetViewOnRelease = checkBox->isChecked();
        }
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *event) override {
        if (m_isEditing) {
            event->ignore();
            return;
        }
        KeyMapItemBase::mousePressEvent(event);
    }

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override {
        Q_UNUSED(event);
        openSettingsDialog();
    }

private:
    double m_speedX = 1.0;
    double m_speedY = 1.0;
    bool m_isEditing = false; bool m_showCursor = false;
    EditMode m_editMode = Edit_None;
    QString m_displayKey;
    QString m_inputBuffer;
    QTimer* m_cursorTimer;
    bool m_resetViewOnRelease = false;  // 松开时是否重置视角
};

// ---------------------------------------------------------
// SteerWheelSubItem 实现部分 (内联函数)
// ---------------------------------------------------------
inline SteerWheelSubItem::SteerWheelSubItem(Direction dir, KeyMapItemSteerWheel* parentWheel) : QGraphicsObject(parentWheel), m_dir(dir), m_parentWheel(parentWheel) {
    setFlags(ItemIsSelectable | ItemIsFocusable); setAcceptHoverEvents(true);
    m_cursorTimer = new QTimer(this); m_cursorTimer->setInterval(600);
    connect(m_cursorTimer, &QTimer::timeout, this, [this](){ m_showCursor = !m_showCursor; update(); });
}
inline void SteerWheelSubItem::setEditing(bool edit) {
    if (m_isEditing == edit) return; m_isEditing = edit;
    if (m_isEditing) { m_displayKey = ""; m_showCursor = true; m_cursorTimer->start(); setSelected(true); }
    else { m_cursorTimer->stop(); m_showCursor = false; } update();
}
inline void SteerWheelSubItem::inputKey(QKeyEvent* event) {
    int key = event->key();
    if (key == Qt::Key_unknown) return;
    m_key = KeyMapHelper::keyToString(key, event->modifiers());
    m_displayKey = m_key; update();
}
inline void SteerWheelSubItem::inputMouse(Qt::MouseButton button) {
    QString keyName;
    switch (button) {
    case Qt::LeftButton: keyName = "LeftButton"; break;
    case Qt::RightButton: keyName = "RightButton"; break;
    case Qt::MiddleButton: keyName = "MiddleButton"; break;
    case Qt::XButton1: keyName = "SideButton1"; break;
    case Qt::XButton2: keyName = "SideButton2"; break;
    default: return;
    }
    if (!keyName.isEmpty()) { m_key = keyName; m_displayKey = keyName; update(); }
}
inline void SteerWheelSubItem::inputWheel(int delta) {
    QString keyName = (delta > 0) ? "WheelUp" : "WheelDown";
    m_key = keyName;
    m_displayKey = keyName;
    update();
}
inline void SteerWheelSubItem::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) {
    p->setRenderHint(QPainter::Antialiasing);
    QColor bg = m_isConflicted ? QColor(255,0,0,100) : (m_isEditing ? QColor(40,40,40,230) : QColor(0, 153, 255, 200));
    p->setBrush(bg); p->setPen(m_isConflicted?QPen(Qt::red,3):(m_isEditing?QPen(Qt::white,1):QPen(Qt::black,1)));
    p->drawEllipse(boundingRect());
    p->setPen(Qt::white); QFont f = p->font(); f.setBold(true);
    QString t = m_isEditing ? (KeyMapHelper::keyToDisplay(m_displayKey) + (m_showCursor?"|":"")) : KeyMapHelper::keyToDisplay(m_key);
    // 根据文字长度自适应字体大小
    int fontSize = 9;
    if (t.length() > 6) fontSize = 6;
    else if (t.length() > 3) fontSize = 7;
    else if (t.length() > 1) fontSize = 8;
    f.setPointSize(fontSize);
    p->setFont(f);
    p->drawText(boundingRect(), Qt::AlignCenter, t.isEmpty()?"?":t);
}
inline void SteerWheelSubItem::mousePressEvent(QGraphicsSceneMouseEvent *e) { e->accept(); }
inline void SteerWheelSubItem::mouseMoveEvent(QGraphicsSceneMouseEvent *e) {
    if(m_isEditing) return; QPointF p=mapToParent(e->pos()); m_parentWheel->updateOffsetFromSubItem(m_dir, p);
}
inline void SteerWheelSubItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *e) { QGraphicsObject::mouseReleaseEvent(e); }

// ---------------------------------------------------------
// KeyMapItemSteerWheel 实现部分 (内联函数)
// ---------------------------------------------------------
inline KeyMapItemSteerWheel::KeyMapItemSteerWheel(QGraphicsItem *parent) : KeyMapItemBase(parent) {
    resize(200, 200); m_comment = "方向盘";
    m_subUp = new SteerWheelSubItem(SteerWheelSubItem::Dir_Up, this); m_subDown = new SteerWheelSubItem(SteerWheelSubItem::Dir_Down, this);
    m_subLeft = new SteerWheelSubItem(SteerWheelSubItem::Dir_Left, this); m_subRight = new SteerWheelSubItem(SteerWheelSubItem::Dir_Right, this);
    m_subUp->setKey("Key_W"); m_subDown->setKey("Key_S"); m_subLeft->setKey("Key_A"); m_subRight->setKey("Key_D");
    updateSubItemsPos();
}
inline void KeyMapItemSteerWheel::setKeys(const QString& u, const QString& d, const QString& l, const QString& r) { m_subUp->setKey(u); m_subDown->setKey(d); m_subLeft->setKey(l); m_subRight->setKey(r); }
inline void KeyMapItemSteerWheel::setOffsets(double u, double d, double l, double r) { m_upOffset=u; m_downOffset=d; m_leftOffset=l; m_rightOffset=r; updateSubItemsPos(); }
inline void KeyMapItemSteerWheel::updateOffsetFromSubItem(SteerWheelSubItem::Direction dir, const QPointF& localPos) {
    if (!scene()) return; QSizeF sz = scene()->sceneRect().size(); if (sz.isEmpty()) return;
    double val=0;
    if(dir==SteerWheelSubItem::Dir_Up) val = -localPos.y()/sz.height();
    else if(dir==SteerWheelSubItem::Dir_Down) val = localPos.y()/sz.height();
    else if(dir==SteerWheelSubItem::Dir_Left) val = -localPos.x()/sz.width();
    else if(dir==SteerWheelSubItem::Dir_Right) val = localPos.x()/sz.width();
    if(val<0.02) val=0.02; if(val>0.48) val=0.48;
    if(dir==SteerWheelSubItem::Dir_Up) m_upOffset=val; else if(dir==SteerWheelSubItem::Dir_Down) m_downOffset=val;
    else if(dir==SteerWheelSubItem::Dir_Left) m_leftOffset=val; else m_rightOffset=val;
    updateSubItemsPos(); update();
}
inline void KeyMapItemSteerWheel::updateSubItemsPos() {
    if (!scene()) return; QSizeF sz = scene()->sceneRect().size(); if (sz.isEmpty()) return;
    m_subUp->setPos(0, -m_upOffset*sz.height()); m_subDown->setPos(0, m_downOffset*sz.height());
    m_subLeft->setPos(-m_leftOffset*sz.width(), 0); m_subRight->setPos(m_rightOffset*sz.width(), 0);
}
inline void KeyMapItemSteerWheel::resize(qreal w, qreal h) { prepareGeometryChange(); m_rect = QRectF(-w/2,-h/2,w,h); updateSubItemsPos(); }
inline QPainterPath KeyMapItemSteerWheel::shape() const {
    QPainterPath p; p.addEllipse(QPointF(0,0), 20, 20);
    QPainterPath l; l.moveTo(m_subUp->pos()); l.lineTo(m_subDown->pos()); l.moveTo(m_subLeft->pos()); l.lineTo(m_subRight->pos());
    QPainterPathStroker s; s.setWidth(10); p.addPath(s.createStroke(l)); return p;
}
inline void KeyMapItemSteerWheel::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) {
    p->setRenderHint(QPainter::Antialiasing); p->setPen(QPen(isSelected()?QColor(255,100,0,150):QColor(0,255,100,80), 4));
    p->drawLine(QPointF(0,0), m_subUp->pos()); p->drawLine(QPointF(0,0), m_subDown->pos());
    p->drawLine(QPointF(0,0), m_subLeft->pos()); p->drawLine(QPointF(0,0), m_subRight->pos());
    p->setBrush(isSelected()?QColor(255,100,0,150):QColor(0,255,100,80)); p->setPen(Qt::NoPen); p->drawEllipse(QPointF(0,0), 10, 10);
}
inline QJsonObject KeyMapItemSteerWheel::toJson() const {
    QJsonObject json; json["type"]="KMT_STEER_WHEEL"; json["comment"]=m_comment;
    QPointF r = getNormalizedPos(scene()?scene()->sceneRect().size():QSizeF(1,1));
    QJsonObject cp; cp["x"]=QString::number(r.x(),'f',4).toDouble(); cp["y"]=QString::number(r.y(),'f',4).toDouble(); json["centerPos"]=cp;
    json["leftOffset"]=m_leftOffset; json["rightOffset"]=m_rightOffset; json["upOffset"]=m_upOffset; json["downOffset"]=m_downOffset;
    json["leftKey"]=m_subLeft->getKey(); json["rightKey"]=m_subRight->getKey(); json["upKey"]=m_subUp->getKey(); json["downKey"]=m_subDown->getKey();
    return json;
}
inline void KeyMapItemSteerWheel::fromJson(const QJsonObject& json) {
    if(json.contains("leftOffset")) m_leftOffset = json["leftOffset"].toDouble();
    if(json.contains("rightOffset")) m_rightOffset = json["rightOffset"].toDouble();
    if(json.contains("upOffset")) m_upOffset = json["upOffset"].toDouble();
    if(json.contains("downOffset")) m_downOffset = json["downOffset"].toDouble();

    if(json.contains("leftKey")) m_subLeft->setKey(json["leftKey"].toString());
    if(json.contains("rightKey")) m_subRight->setKey(json["rightKey"].toString());
    if(json.contains("upKey")) m_subUp->setKey(json["upKey"].toString());
    if(json.contains("downKey")) m_subDown->setKey(json["downKey"].toString());
    updateSubItemsPos();
}
inline SteerWheelSubItem* KeyMapItemSteerWheel::getSubItemAt(const QPointF& pos) {
    if (m_subUp->sceneBoundingRect().contains(mapToScene(pos))) return m_subUp;
    return nullptr;
}

// ---------------------------------------------------------
// 工厂实现
// ---------------------------------------------------------
class KeyMapFactoryImpl : public KeyMapFactory {
public:
    KeyMapItemBase* createItem(KeyMapType type) override {
        switch (type) {
        case KMT_STEER_WHEEL: return new KeyMapItemSteerWheel();
        case KMT_SCRIPT: return new KeyMapItemScript();
        case KMT_CAMERA_MOVE: return new KeyMapItemCamera();
        case KMT_FREE_LOOK: return new KeyMapItemFreeLook();
        default: return nullptr;
        }
    }
};

#endif // KEYMAPITEMS_H
