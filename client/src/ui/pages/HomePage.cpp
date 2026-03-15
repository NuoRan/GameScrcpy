#include "HomePage.h"
#include "FluentCard.h"
#include "FluentButton.h"
#include "ThemeManager.h"
#include "DesignTokens.h"
#include "hid/TouchRouter.h"
#include "ConfigCenter.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QSvgRenderer>
#include <QPainter>

using namespace Fluent;

HomePage::HomePage(QWidget *parent) : QWidget(parent)
{
    setupUI();
    retranslateUi();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        // 刷新列表选中色、卡片等
        update();
    });
}

void HomePage::setupUI()
{
    auto &tm = ThemeManager::instance();

    // 外层滚动容器
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto *scrollContent = new QWidget;
    auto *mainLayout = new QVBoxLayout(scrollContent);
    mainLayout->setContentsMargins(32, 28, 32, 20);
    mainLayout->setSpacing(24);

    // ---- 标题 ----
    QWidget* titleBar = new QWidget;
    titleBar->setStyleSheet("background: transparent;");
    QHBoxLayout* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(10);

    // 图标
    QLabel* logoLabel = new QLabel;
    QSvgRenderer renderer(QStringLiteral(":/icons/gamepad.svg"));
    QPixmap logoPx(28, 28);
    logoPx.fill(Qt::transparent);
    QPainter logoPainter(&logoPx);
    renderer.render(&logoPainter);
    logoPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    logoPainter.fillRect(logoPx.rect(), QColor(tm.accentPrimary()));
    logoPainter.end();
    logoLabel->setPixmap(logoPx);
    logoLabel->setFixedSize(28, 28);
    logoLabel->setStyleSheet("background: transparent;");
    titleLayout->addStretch();
    titleLayout->addWidget(logoLabel);

    m_titleLabel = new QLabel(QStringLiteral("GameScrcpy"));
    m_titleLabel->setStyleSheet(QStringLiteral(
        "font-size: 22px; font-weight: 700; color: %1; background: transparent;")
        .arg(tm.textPrimary()));
    titleLayout->addWidget(m_titleLabel);

    QLabel* subtitleLabel = new QLabel(QStringLiteral("Screen Control"));
    subtitleLabel->setStyleSheet(QStringLiteral(
        "font-size: 11px; color: %1; background: transparent; padding-top: 6px;")
        .arg(tm.textTertiary()));
    titleLayout->addWidget(subtitleLabel);
    titleLayout->addStretch();

    mainLayout->addWidget(titleBar);

    // ---- 快速连接卡片 ----
    auto *connectCard = new FluentCard(this);
    auto *connectLayout = new QVBoxLayout(connectCard);
    connectLayout->setContentsMargins(20, 20, 20, 20);
    connectLayout->setSpacing(16);

    auto *connectTitle = new QLabel;
    connectTitle->setStyleSheet(QStringLiteral(
        "font-size: 14px; font-weight: 600; color: %1; background: transparent;")
        .arg(tm.textPrimary()));
    connectLayout->addWidget(connectTitle);
    // 存标签稍后翻译
    connectTitle->setProperty("_role", "connectTitle");

    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(16);

    m_usbBtn = new FluentButton(QString(), FluentButton::Primary, this);
    m_usbBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_usbBtn->setMinimumHeight(48);

    m_wifiBtn = new FluentButton(QString(), FluentButton::Secondary, this);
    m_wifiBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_wifiBtn->setMinimumHeight(48);

    btnRow->addWidget(m_usbBtn);
    btnRow->addWidget(m_wifiBtn);
    connectLayout->addLayout(btnRow);

    mainLayout->addWidget(connectCard);

    // ---- 设备列表卡片 ----
    auto *deviceCard = new FluentCard(this);
    deviceCard->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto *deviceLayout = new QVBoxLayout(deviceCard);
    deviceLayout->setContentsMargins(20, 20, 20, 20);
    deviceLayout->setSpacing(14);

    // 标题行
    auto *headerRow = new QHBoxLayout;
    headerRow->setSpacing(8);

    m_deviceTitle = new QLabel;
    m_deviceTitle->setStyleSheet(QStringLiteral(
        "font-size: 14px; font-weight: 600; color: %1; background: transparent;")
        .arg(tm.textPrimary()));

    m_deviceHint = new QLabel;
    m_deviceHint->setStyleSheet(QStringLiteral(
        "font-size: 12px; color: %1; background: transparent;")
        .arg(tm.textTertiary()));

    m_autoRefreshChk = new QCheckBox;
    m_autoRefreshChk->setChecked(true);

    m_refreshBtn = new QPushButton;
    m_refreshBtn->setMinimumHeight(34);

    headerRow->addWidget(m_deviceTitle);
    headerRow->addWidget(m_deviceHint);
    headerRow->addStretch();
    headerRow->addWidget(m_autoRefreshChk);
    headerRow->addWidget(m_refreshBtn);

    deviceLayout->addLayout(headerRow);

    // 列表
    m_deviceList = new QListWidget;
    m_deviceList->setMinimumHeight(100);
    m_deviceList->setAlternatingRowColors(false);
    m_deviceList->setStyleSheet(QStringLiteral(
        "QListWidget { background: transparent; border: none; }"
        "QListWidget::item { padding: 10px 14px; border-radius: 6px; color: %1; }"
        "QListWidget::item:hover { background: %2; }"
        "QListWidget::item:selected { background: %3; color: %4; }")
        .arg(tm.textPrimary(), tm.navHover(), tm.accentPrimary(), "#ffffff"));

    deviceLayout->addWidget(m_deviceList);
    mainLayout->addWidget(deviceCard, 1);

    scrollArea->setWidget(scrollContent);

    // 外层布局
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);

    // ---- 信号 ----
    connect(m_usbBtn,  &QPushButton::clicked, this, &HomePage::requestUsbConnect);
    connect(m_wifiBtn, &QPushButton::clicked, this, [this]() {
        // WiFi 连接需要地址——通知 MainWindow 打开输入对话框
        emit requestWifiConnect(QString());
    });
    connect(m_refreshBtn, &QPushButton::clicked, this, &HomePage::requestRefresh);

    connect(m_deviceList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (!item) return;
        if (item->data(Qt::UserRole).toString() == "__DIRECT__") {
            emit requestDirectConnect();
        } else {
            emit requestDeviceConnect(item->text());
        }
    });
}

void HomePage::updateDeviceList(const QStringList &serials)
{
    m_lastSerials = serials;  // 缓存真实设备列表

    const QString currentText = m_deviceList->currentItem()
                                    ? m_deviceList->currentItem()->text()
                                    : QString();
    m_deviceList->clear();

    // 如果触控方式是 AOA/ESP32，在顶部插入直连条目
    updateDirectConnectItem();

    for (const auto &s : serials) {
        m_deviceList->addItem(s);
    }
    // 恢复选中
    if (!currentText.isEmpty()) {
        auto items = m_deviceList->findItems(currentText, Qt::MatchExactly);
        if (!items.isEmpty()) m_deviceList->setCurrentItem(items.first());
    }
}

void HomePage::setDeviceIP(const QString &ip)
{
    m_lastDeviceIP = ip;
}

void HomePage::retranslateUi()
{
    m_usbBtn->setText(tr("USB 连接"));
    m_wifiBtn->setText(tr("WiFi 连接"));
    m_deviceTitle->setText(tr("设备列表"));
    m_deviceHint->setText(tr("双击连接"));
    m_autoRefreshChk->setText(tr("自动刷新"));
    m_refreshBtn->setText(tr("刷新"));

    // connectTitle
    auto labels = findChildren<QLabel *>();
    for (auto *l : labels) {
        if (l->property("_role").toString() == "connectTitle")
            l->setText(tr("快速连接"));
    }
}

void HomePage::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QWidget::changeEvent(event);
}

void HomePage::onTouchMethodChanged(int method)
{
    m_touchMethod = method;
    // 重新构建列表 (在最前面插入或移除直连条目)
    updateDeviceList(m_lastSerials);
}

void HomePage::updateDirectConnectItem()
{
    auto tm = static_cast<TouchMethod>(m_touchMethod);
    // AOA / ESP32 不依赖 ADB，始终提供直连入口
    if (methodNeedsServer(tm)) return;

    QString label;
    if (methodUsesAoa(tm))
        label = tr("🔌 AOA 直连 (双击连接)");
    else if (methodUsesEsp32(tm))
        label = tr("🔌 ESP32 直连 (双击连接)");
    else
        return;

    auto *item = new QListWidgetItem(label);
    item->setData(Qt::UserRole, "__DIRECT__");
    QFont f = item->font();
    f.setBold(true);
    item->setFont(f);
    m_deviceList->insertItem(0, item);
}
