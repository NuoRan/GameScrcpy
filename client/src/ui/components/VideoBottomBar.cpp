/**
 * @file VideoBottomBar.cpp
 * @brief 视频窗口右侧竖直操作栏
 */
#include "VideoBottomBar.h"
#include "FluentButton.h"
#include "ThemeManager.h"
#include "ConfigCenter.h"

#include <QVBoxLayout>
#include <QPainter>
#include <QIcon>
#include <QPainterPath>
#include <QtMath>

using namespace Fluent;

// 生成 QPainter 绘制的图标 QIcon
static QIcon makeIcon(int sz, std::function<void(QPainter&, int)> draw) {
    QPixmap pm(sz, sz);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    draw(p, sz);
    p.end();
    return QIcon(pm);
}

VideoBottomBar::VideoBottomBar(QWidget* parent) : QWidget(parent)
{
    setFixedWidth(40);
    setupUI();
}

void VideoBottomBar::setupUI()
{
    auto& tm = ThemeManager::instance();
    QColor fg(tm.textPrimary());

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 6, 2, 6);
    layout->setSpacing(2);

    auto makeBtn = [this](QIcon icon, const QString& tooltip) {
        auto* btn = new FluentButton(QString(), FluentButton::Ghost, this);
        btn->setIcon(icon);
        btn->setIconSize(QSize(18, 18));
        btn->setFixedSize(36, 36);
        btn->setToolTip(tooltip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::NoFocus);
        return btn;
    };

    // 返回 ◁ 三角形
    QIcon backIcon = makeIcon(24, [fg](QPainter& p, int s) {
        p.setPen(Qt::NoPen);
        p.setBrush(fg);
        QPolygonF tri;
        tri << QPointF(s*0.7, s*0.2) << QPointF(s*0.3, s*0.5) << QPointF(s*0.7, s*0.8);
        p.drawPolygon(tri);
    });

    // 主页 ○ 圆圈
    QIcon homeIcon = makeIcon(24, [fg](QPainter& p, int s) {
        p.setPen(QPen(fg, 2.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QRectF(s*0.2, s*0.2, s*0.6, s*0.6));
    });

    // 多任务 □ 方框
    QIcon appSwitchIcon = makeIcon(24, [fg](QPainter& p, int s) {
        p.setPen(QPen(fg, 2.5));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(s*0.22, s*0.22, s*0.56, s*0.56), 2, 2);
    });

    // 全屏 ⤢ 对角箭头
    QIcon fullScreenIcon = makeIcon(24, [fg](QPainter& p, int s) {
        p.setPen(QPen(fg, 2.0));
        // 左上角
        p.drawLine(QPointF(s*0.15, s*0.35), QPointF(s*0.15, s*0.15));
        p.drawLine(QPointF(s*0.15, s*0.15), QPointF(s*0.35, s*0.15));
        // 右下角
        p.drawLine(QPointF(s*0.85, s*0.65), QPointF(s*0.85, s*0.85));
        p.drawLine(QPointF(s*0.85, s*0.85), QPointF(s*0.65, s*0.85));
        // 对角线
        p.drawLine(QPointF(s*0.2, s*0.2), QPointF(s*0.8, s*0.8));
    });

    // 键位映射 ⌨ 键盘
    QIcon keyMapIcon = makeIcon(24, [fg](QPainter& p, int s) {
        p.setPen(QPen(fg, 1.5));
        p.setBrush(Qt::NoBrush);
        // 键盘外框
        p.drawRoundedRect(QRectF(s*0.1, s*0.25, s*0.8, s*0.5), 3, 3);
        // 按键行
        qreal y1 = s*0.38, y2 = s*0.52, kw = s*0.08, kh = s*0.08;
        p.setBrush(fg);
        for (int i = 0; i < 4; ++i)
            p.drawRect(QRectF(s*0.18 + i*s*0.17, y1, kw, kh));
        for (int i = 0; i < 4; ++i)
            p.drawRect(QRectF(s*0.18 + i*s*0.17, y2, kw, kh));
        // 空格键
        p.drawRect(QRectF(s*0.3, s*0.64, s*0.4, kh));
    });

    // 设置 ⚙ 齿轮
    QIcon settingsIcon = makeIcon(24, [fg](QPainter& p, int s) {
        p.setPen(QPen(fg, 1.5));
        p.setBrush(Qt::NoBrush);
        qreal cx = s*0.5, cy = s*0.5, r = s*0.18;
        p.drawEllipse(QPointF(cx, cy), r, r);
        // 齿轮齿 (8个)
        for (int i = 0; i < 8; ++i) {
            qreal angle = i * 3.14159265 / 4.0;
            qreal x1 = cx + s*0.25 * qCos(angle);
            qreal y1 = cy + s*0.25 * qSin(angle);
            qreal x2 = cx + s*0.38 * qCos(angle);
            qreal y2 = cy + s*0.38 * qSin(angle);
            p.drawLine(QPointF(x1, y1), QPointF(x2, y2));
        }
    });

    // 音频 � 播放中喇叭 (默认图标，音频启用时默认播放)
    QIcon audioPlayingIcon = makeIcon(24, [fg](QPainter& p, int s) {
        p.setPen(QPen(fg, 1.8));
        p.setBrush(Qt::NoBrush);
        QPolygonF speaker;
        speaker << QPointF(s*0.12, s*0.38) << QPointF(s*0.25, s*0.38)
                << QPointF(s*0.42, s*0.2) << QPointF(s*0.42, s*0.8)
                << QPointF(s*0.25, s*0.62) << QPointF(s*0.12, s*0.62);
        p.drawPolygon(speaker);
        // 声波弧线
        p.drawArc(QRectF(s*0.48, s*0.3, s*0.2, s*0.4), -60*16, 120*16);
        p.drawArc(QRectF(s*0.58, s*0.18, s*0.3, s*0.64), -60*16, 120*16);
    });

    m_backBtn       = makeBtn(backIcon,       tr("返回"));
    m_homeBtn       = makeBtn(homeIcon,       tr("主页"));
    m_appSwitchBtn  = makeBtn(appSwitchIcon,  tr("多任务"));
    m_fullScreenBtn = makeBtn(fullScreenIcon, tr("全屏"));
    m_keyMapBtn     = makeBtn(keyMapIcon,     tr("键位映射"));
    m_audioBtn      = makeBtn(audioPlayingIcon, tr("音频 (播放中)"));
    m_settingsBtn   = makeBtn(settingsIcon,   tr("设置"));

    // 伴侣 App 📱 手机图标
    QIcon companionIcon = makeIcon(24, [fg](QPainter& p, int s) {
        p.setPen(QPen(fg, 1.8));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(s*0.28, s*0.12, s*0.44, s*0.76), 4, 4);
        p.setPen(Qt::NoPen);
        p.setBrush(fg);
        p.drawEllipse(QPointF(s*0.5, s*0.8), s*0.04, s*0.04);
    });
    m_companionBtn  = makeBtn(companionIcon,  tr("手机伴侣"));

    // 横竖屏切换 ⟳ 旋转图标
    QIcon rotateIcon = makeIcon(24, [fg](QPainter& p, int s) {
        p.setPen(QPen(fg, 1.8));
        p.setBrush(Qt::NoBrush);
        // 手机外框 (竖屏)
        p.drawRoundedRect(QRectF(s*0.18, s*0.14, s*0.34, s*0.52), 3, 3);
        // 旋转箭头弧线
        p.drawArc(QRectF(s*0.42, s*0.38, s*0.42, s*0.42), 30*16, 240*16);
        // 箭头头部
        QPointF arrowTip(s*0.82, s*0.52);
        QPolygonF arrow;
        arrow << arrowTip << QPointF(s*0.74, s*0.44) << QPointF(s*0.74, s*0.60);
        p.setPen(Qt::NoPen);
        p.setBrush(fg);
        p.drawPolygon(arrow);
    });

    // 判断当前是否横屏 (w > h)
    int curW = qsc::ConfigCenter::instance().get<int>("user/aoaResWidth", 1080);
    int curH = qsc::ConfigCenter::instance().get<int>("user/aoaResHeight", 2400);
    m_isLandscape = (curW > curH);

    m_rotateBtn = makeBtn(rotateIcon, m_isLandscape ? tr("切换竖屏") : tr("切换横屏"));

    layout->addWidget(m_backBtn);
    layout->addWidget(m_homeBtn);
    layout->addWidget(m_appSwitchBtn);
    layout->addStretch();
    layout->addWidget(m_companionBtn);
    layout->addWidget(m_rotateBtn);
    layout->addWidget(m_settingsBtn);
    layout->addWidget(m_audioBtn);
    layout->addWidget(m_fullScreenBtn);
    layout->addWidget(m_keyMapBtn);

    connect(m_backBtn,       &QPushButton::clicked, this, &VideoBottomBar::goBack);
    connect(m_homeBtn,       &QPushButton::clicked, this, &VideoBottomBar::goHome);
    connect(m_appSwitchBtn,  &QPushButton::clicked, this, &VideoBottomBar::appSwitch);
    connect(m_fullScreenBtn, &QPushButton::clicked, this, &VideoBottomBar::fullScreen);
    connect(m_settingsBtn,   &QPushButton::clicked, this, &VideoBottomBar::settingsClicked);
    connect(m_companionBtn,  &QPushButton::clicked, this, &VideoBottomBar::companionClicked);

    connect(m_rotateBtn, &QPushButton::clicked, this, [this]() {
        // 读取当前分辨率并对调 W↔H
        auto& cc = qsc::ConfigCenter::instance();
        int w = cc.get<int>("user/aoaResWidth", 1080);
        int h = cc.get<int>("user/aoaResHeight", 2400);
        cc.set("user/aoaResWidth", h);
        cc.set("user/aoaResHeight", w);
        m_isLandscape = (h > w);
        m_rotateBtn->setToolTip(m_isLandscape ? tr("切换竖屏") : tr("切换横屏"));
        emit rotateClicked();
    });

    connect(m_audioBtn, &QPushButton::clicked, this, [this]() {
        m_audioMuted = !m_audioMuted;
        setAudioMuted(m_audioMuted);
        emit audioToggled(m_audioMuted);
    });

    connect(m_keyMapBtn, &QPushButton::clicked, this, [this]() {
        m_keyMapActive = !m_keyMapActive;
        setKeyMapMode(m_keyMapActive);
        emit keyMapToggled(m_keyMapActive);
    });
}

void VideoBottomBar::setKeyMapMode(bool active)
{
    m_keyMapActive = active;
    auto& tm = ThemeManager::instance();
    if (active) {
        m_keyMapBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; border: none; border-radius: 8px; color: #ffffff; }"
            "QPushButton:hover { background: %2; }").arg(tm.accentPrimary(), tm.accentHover()));
    } else {
        m_keyMapBtn->setStyleSheet(QString()); // reset to default
    }
}

void VideoBottomBar::setAudioMuted(bool muted)
{
    m_audioMuted = muted;
    auto& tm = ThemeManager::instance();
    QColor fg(tm.textPrimary());

    if (!muted) {
        // 播放中: 喇叭 + 声波
        QIcon icon = makeIcon(24, [fg](QPainter& p, int s) {
            p.setPen(QPen(fg, 1.8));
            p.setBrush(Qt::NoBrush);
            QPolygonF speaker;
            speaker << QPointF(s*0.12, s*0.38) << QPointF(s*0.25, s*0.38)
                    << QPointF(s*0.42, s*0.2) << QPointF(s*0.42, s*0.8)
                    << QPointF(s*0.25, s*0.62) << QPointF(s*0.12, s*0.62);
            p.drawPolygon(speaker);
            // 声波弧线
            p.drawArc(QRectF(s*0.48, s*0.3, s*0.2, s*0.4), -60*16, 120*16);
            p.drawArc(QRectF(s*0.58, s*0.18, s*0.3, s*0.64), -60*16, 120*16);
        });
        m_audioBtn->setIcon(icon);
        m_audioBtn->setToolTip(tr("音频 (播放中)"));
        // 高亮按钮
        m_audioBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; border: none; border-radius: 8px; color: #ffffff; }"
            "QPushButton:hover { background: %2; }").arg(tm.accentPrimary(), tm.accentHover()));
    } else {
        // 静音: 喇叭 + 叉号
        QIcon icon = makeIcon(24, [fg](QPainter& p, int s) {
            p.setPen(QPen(fg, 1.8));
            p.setBrush(Qt::NoBrush);
            QPolygonF speaker;
            speaker << QPointF(s*0.15, s*0.38) << QPointF(s*0.28, s*0.38)
                    << QPointF(s*0.45, s*0.2) << QPointF(s*0.45, s*0.8)
                    << QPointF(s*0.28, s*0.62) << QPointF(s*0.15, s*0.62);
            p.drawPolygon(speaker);
            p.drawLine(QPointF(s*0.58, s*0.35), QPointF(s*0.82, s*0.65));
            p.drawLine(QPointF(s*0.82, s*0.35), QPointF(s*0.58, s*0.65));
        });
        m_audioBtn->setIcon(icon);
        m_audioBtn->setToolTip(tr("音频 (已静音)"));
        m_audioBtn->setStyleSheet(QString()); // reset to default
    }
}

void VideoBottomBar::setFullScreenMode(bool fullScreen)
{
    auto& tm = ThemeManager::instance();
    QColor fg(tm.textPrimary());
    if (fullScreen) {
        // 退出全屏 ↙
        m_fullScreenBtn->setIcon(makeIcon(24, [fg](QPainter& p, int s) {
            p.setPen(QPen(fg, 2.0));
            // 右上角
            p.drawLine(QPointF(s*0.85, s*0.35), QPointF(s*0.85, s*0.15));
            p.drawLine(QPointF(s*0.85, s*0.15), QPointF(s*0.65, s*0.15));
            // 左下角
            p.drawLine(QPointF(s*0.15, s*0.65), QPointF(s*0.15, s*0.85));
            p.drawLine(QPointF(s*0.15, s*0.85), QPointF(s*0.35, s*0.85));
            // 对角线
            p.drawLine(QPointF(s*0.8, s*0.2), QPointF(s*0.2, s*0.8));
        }));
    } else {
        // 全屏 ⤢ (恢复默认图标)
        m_fullScreenBtn->setIcon(makeIcon(24, [fg](QPainter& p, int s) {
            p.setPen(QPen(fg, 2.0));
            p.drawLine(QPointF(s*0.15, s*0.35), QPointF(s*0.15, s*0.15));
            p.drawLine(QPointF(s*0.15, s*0.15), QPointF(s*0.35, s*0.15));
            p.drawLine(QPointF(s*0.85, s*0.65), QPointF(s*0.85, s*0.85));
            p.drawLine(QPointF(s*0.85, s*0.85), QPointF(s*0.65, s*0.85));
            p.drawLine(QPointF(s*0.2, s*0.2), QPointF(s*0.8, s*0.8));
        }));
    }
}

void VideoBottomBar::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    auto& tm = ThemeManager::instance();
    p.fillRect(rect(), QColor(tm.card()));
    // 左边线
    p.setPen(QColor(tm.border()));
    p.drawLine(0, 0, 0, height());
}

void VideoBottomBar::setAudioVisible(bool visible)
{
    if (m_audioBtn) {
        m_audioBtn->setVisible(visible);
        if (visible) {
            // 同步图标状态
            setAudioMuted(m_audioMuted);
        }
    }
}

void VideoBottomBar::setCompanionConnected(bool connected)
{
    if (!m_companionBtn) return;
    auto& tm = ThemeManager::instance();
    if (connected) {
        m_companionBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; border: none; border-radius: 8px; color: #ffffff; }"
            "QPushButton:hover { background: %2; }").arg(tm.accentPrimary(), tm.accentHover()));
        m_companionBtn->setToolTip(tr("手机伴侣 (已连接)"));
    } else {
        m_companionBtn->setStyleSheet(QString());
        m_companionBtn->setToolTip(tr("手机伴侣"));
    }
}

void VideoBottomBar::setLandscapeMode(bool landscape)
{
    m_isLandscape = landscape;
    if (m_rotateBtn)
        m_rotateBtn->setToolTip(landscape ? tr("切换竖屏") : tr("切换横屏"));
}
