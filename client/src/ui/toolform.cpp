#include "toolform.h"
#include "ui_toolform.h"
#include "videoform.h"
#include "iconhelper.h"
#include "service/DeviceSession.h"
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDir>
#include <QInputDialog>
#include <QFile>
#include <QDebug>
#include <QDialog>
#include <QSlider>
#include <QLabel>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include "ConfigCenter.h"

// ---------------------------------------------------------
// 可拖拽的标签 (DraggableLabel)
// 实现从工具栏拖拽键位元素到视频窗口的逻辑
// ---------------------------------------------------------
DraggableLabel::DraggableLabel(KeyMapType type, const QString& text, QWidget* parent, const QString& preset) : QLabel(text, parent), m_type(type), m_preset(preset) {
    setAlignment(Qt::AlignCenter);
    setMinimumSize(70, 34);
    setCursor(Qt::OpenHandCursor);
    setStyleSheet(
        "QLabel{"
        "  border:1px solid #3f3f46;"
        "  border-radius:6px;"
        "  color:#a1a1aa;"
        "  background-color:#27272a;"
        "  font-size:11px;"
        "  font-weight:500;"
        "  padding:4px 8px;"
        "}"
        "QLabel:hover{"
        "  background-color:#3f3f46;"
        "  border-color:#6366f1;"
        "  color:#fafafa;"
        "}"
    );
}
void DraggableLabel::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) m_dragStartPosition = event->pos();
    QLabel::mousePressEvent(event);
}
void DraggableLabel::mouseMoveEvent(QMouseEvent *event) {
    if (!(event->buttons() & Qt::LeftButton)) return;
    if ((event->pos() - m_dragStartPosition).manhattanLength() < QApplication::startDragDistance()) return;

    // 开始拖拽操作，传递键位类型
    QDrag *drag = new QDrag(this);
    QMimeData *mime = new QMimeData;
    mime->setData("application/x-keymap-type", QByteArray::number((int)m_type));
    if (!m_preset.isEmpty())
        mime->setData("application/x-keymap-preset", m_preset.toUtf8());
    drag->setMimeData(mime);
    QPixmap pix(size()); pix.fill(Qt::transparent); render(&pix);
    drag->setPixmap(pix); drag->setHotSpot(event->pos());
    drag->exec(Qt::CopyAction | Qt::MoveAction);
}

// ---------------------------------------------------------
// 工具栏窗口 (ToolForm)
// 包含设备控制按钮和键位映射配置管理
// ---------------------------------------------------------
ToolForm::ToolForm(QWidget *parent, AdsorbPositions pos) : MagneticWidget(parent, pos), ui(new Ui::ToolForm) {
    ui->setupUi(this);
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    setFixedWidth(64);

    // 设置自适应高度策略
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);

    initStyle();
    initKeyMapPalette();
    ui->stackedWidget->setCurrentIndex(0);

    // 初始自适应大小
    adjustSize();
}
ToolForm::~ToolForm() { delete ui; }

// ---------------------------------------------------------
// 初始化键位面板
// 创建配置下拉框、保存按钮以及可拖拽的键位组件
// ---------------------------------------------------------
void ToolForm::initKeyMapPalette() {
    QVBoxLayout* layout = qobject_cast<QVBoxLayout*>(ui->page_keymap->layout());
    if (!layout) return;

    // 设置布局间距
    layout->setSpacing(8);

    // 配置选择下拉框 - 固定宽度，避免展开时撑大侧边栏
    m_configComboBox = new QComboBox(ui->page_keymap);
    m_configComboBox->setMinimumHeight(32);
    m_configComboBox->setFixedWidth(100);  // 固定宽度
    m_configComboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_configComboBox->setStyleSheet(
        "QComboBox{background:#27272a;color:#fafafa;border:1px solid #3f3f46;border-radius:6px;padding:2px 6px;font-size:9px;}"
        "QComboBox:hover{border-color:#6366f1;}"
        "QComboBox::drop-down{border:none;width:18px;subcontrol-position:center right;right:4px;}"
        "QComboBox::down-arrow{image:none;width:0;height:0;border-style:solid;border-width:5px 4px 0 4px;border-color:#71717a transparent transparent transparent;}"
        "QComboBox::down-arrow:on,QComboBox::down-arrow:hover{border-color:#a1a1aa transparent transparent transparent;}"
        "QComboBox QAbstractItemView{background:#27272a;border:1px solid #3f3f46;border-radius:6px;padding:4px;}"
        "QComboBox QAbstractItemView::item{padding:6px;border-radius:4px;color:#fafafa;}"
        "QComboBox QAbstractItemView::item:hover{background:#3f3f46;}"
    );
    connect(m_configComboBox, &QComboBox::currentTextChanged, this, &ToolForm::onConfigChanged);
    layout->addWidget(m_configComboBox);

    // 按钮行：刷新 | 文件夹 | 新建
    QHBoxLayout* btnRowLayout = new QHBoxLayout();
    btnRowLayout->setSpacing(4);

    // 刷新按钮
    m_refreshBtn = new QPushButton("↻", ui->page_keymap);
    m_refreshBtn->setMinimumHeight(28);
    m_refreshBtn->setToolTip(tr("刷新配置列表"));
    m_refreshBtn->setStyleSheet(
        "QPushButton{background:#27272a;color:#fafafa;border:1px solid #3f3f46;border-radius:6px;font-size:12px;}"
        "QPushButton:hover{background:#3f3f46;border-color:#6366f1;}"
    );
    connect(m_refreshBtn, &QPushButton::clicked, this, &ToolForm::refreshConfig);
    btnRowLayout->addWidget(m_refreshBtn, 1);

    // 文件夹按钮
    m_folderBtn = new QPushButton("📁", ui->page_keymap);
    m_folderBtn->setMinimumHeight(28);
    m_folderBtn->setToolTip(tr("打开配置文件夹"));
    m_folderBtn->setStyleSheet(
        "QPushButton{background:#27272a;color:#fafafa;border:1px solid #3f3f46;border-radius:6px;font-size:11px;}"
        "QPushButton:hover{background:#3f3f46;border-color:#6366f1;}"
    );
    connect(m_folderBtn, &QPushButton::clicked, this, &ToolForm::openKeyMapFolder);
    btnRowLayout->addWidget(m_folderBtn, 1);

    // 新建按钮
    m_newConfigBtn = new QPushButton("+", ui->page_keymap);
    m_newConfigBtn->setMinimumHeight(28);
    m_newConfigBtn->setToolTip(tr("新建配置"));
    m_newConfigBtn->setStyleSheet(
        "QPushButton{background:#27272a;color:#fafafa;border:1px solid #3f3f46;border-radius:6px;font-size:14px;}"
        "QPushButton:hover{background:#3f3f46;border-color:#6366f1;}"
    );
    connect(m_newConfigBtn, &QPushButton::clicked, this, &ToolForm::createNewConfig);
    btnRowLayout->addWidget(m_newConfigBtn, 1);

    layout->addLayout(btnRowLayout);

    // 保存按钮
    m_saveBtn = new QPushButton(tr("保存"), ui->page_keymap);
    m_saveBtn->setMinimumHeight(32);
    m_saveBtn->setToolTip(tr("保存当前配置"));
    m_saveBtn->setStyleSheet(
        "QPushButton{background:#6366f1;color:#ffffff;border:none;border-radius:6px;font-size:9px;font-weight:600;}"
        "QPushButton:hover{background:#818cf8;}"
    );
    connect(m_saveBtn, &QPushButton::clicked, this, &ToolForm::saveConfig);
    layout->addWidget(m_saveBtn);

    // 显示键位按钮
    m_overlayBtn = new QPushButton(tr("显示键位"), ui->page_keymap);
    m_overlayBtn->setMinimumHeight(32);
    m_overlayBtn->setCheckable(true);
    m_overlayBtn->setToolTip(tr("显示/隐藏键位提示"));
    m_overlayBtn->setStyleSheet(
        "QPushButton{background:#27272a;color:#fafafa;border:1px solid #3f3f46;border-radius:6px;font-size:9px;}"
        "QPushButton:hover{background:#3f3f46;border-color:#6366f1;}"
        "QPushButton:checked{background:#6366f1;border-color:#6366f1;}"
    );
    connect(m_overlayBtn, &QPushButton::toggled, this, [this](bool checked) {
        m_overlayVisible = checked;
        m_overlayBtn->setText(checked ? tr("隐藏键位") : tr("显示键位"));
        emit keyMapOverlayToggled(checked);
    });
    layout->addWidget(m_overlayBtn);

    // 初始化时从配置读取状态并同步按钮
    bool overlayVisible = qsc::ConfigCenter::instance().keyMapOverlayVisible();
    m_overlayBtn->blockSignals(true);
    m_overlayBtn->setChecked(overlayVisible);
    m_overlayVisible = overlayVisible;
    m_overlayBtn->setText(overlayVisible ? tr("隐藏键位") : tr("显示键位"));
    m_overlayBtn->blockSignals(false);

    // 设置按钮（在显示键位下面）
    m_antiDetectBtn = new QPushButton(tr("设置"), ui->page_keymap);
    m_antiDetectBtn->setMinimumHeight(32);
    m_antiDetectBtn->setToolTip(tr("打开设置面板"));
    m_antiDetectBtn->setStyleSheet(
        "QPushButton{background:#27272a;color:#fafafa;border:1px solid #3f3f46;border-radius:6px;font-size:9px;}"
        "QPushButton:hover{background:#3f3f46;border-color:#6366f1;}"
    );
    connect(m_antiDetectBtn, &QPushButton::clicked, this, &ToolForm::showAntiDetectSettings);
    layout->addWidget(m_antiDetectBtn);

    // 分隔线
    QFrame* separator = new QFrame(ui->page_keymap);
    separator->setFrameShape(QFrame::HLine);
    separator->setFixedHeight(1);
    separator->setStyleSheet("background:#3f3f46;margin:4px 0;");
    layout->addWidget(separator);

    // 可拖拽键位元素 - 居中对齐（点击/长按在脚本上面）
    auto* clickLabel = new DraggableLabel(KMT_SCRIPT, tr("点击"), ui->page_keymap, "click");
    auto* holdLabel = new DraggableLabel(KMT_SCRIPT, tr("长按"), ui->page_keymap, "hold");
    m_scriptLabel = new DraggableLabel(KMT_SCRIPT, tr("脚本"), ui->page_keymap);
    m_steerLabel = new DraggableLabel(KMT_STEER_WHEEL, tr("轮盘"), ui->page_keymap);
    m_cameraLabel = new DraggableLabel(KMT_CAMERA_MOVE, tr("视角"), ui->page_keymap);
    m_freeLookLabel = new DraggableLabel(KMT_FREE_LOOK, tr("小眼睛"), ui->page_keymap);

    layout->addWidget(clickLabel, 0, Qt::AlignHCenter);
    layout->addWidget(holdLabel, 0, Qt::AlignHCenter);
    layout->addWidget(m_scriptLabel, 0, Qt::AlignHCenter);
    layout->addWidget(m_steerLabel, 0, Qt::AlignHCenter);
    layout->addWidget(m_cameraLabel, 0, Qt::AlignHCenter);
    layout->addWidget(m_freeLookLabel, 0, Qt::AlignHCenter);

    refreshKeyMapList();
}

// ---------------------------------------------------------
// 配置文件管理
// 刷新、新建、保存键位配置文件 (*.json)
// ---------------------------------------------------------
void ToolForm::refreshKeyMapList() {
    if (!m_configComboBox) return;
    QString current = m_configComboBox->currentText();
    m_configComboBox->blockSignals(true); m_configComboBox->clear();
    QDir dir("keymap"); if (!dir.exists()) dir.mkpath(".");
    QStringList files = dir.entryList(QStringList() << "*.json", QDir::Files);
    if (files.isEmpty()) m_configComboBox->addItem("default.json");
    else m_configComboBox->addItems(files);
    int idx = m_configComboBox->findText(current);
    if (idx >= 0) m_configComboBox->setCurrentIndex(idx);
    m_configComboBox->blockSignals(false);
}

QString ToolForm::getCurrentKeyMapFile() { return m_configComboBox ? m_configComboBox->currentText() : "default.json"; }

void ToolForm::setCurrentKeyMap(const QString& filename) {
    if (!m_configComboBox) return;
    refreshKeyMapList();
    int index = m_configComboBox->findText(filename);
    if (index >= 0) {
        m_configComboBox->blockSignals(true);
        m_configComboBox->setCurrentIndex(index);
        m_configComboBox->blockSignals(false);
    }
}

void ToolForm::setOverlayButtonState(bool checked) {
    if (!m_overlayBtn) return;
    m_overlayBtn->blockSignals(true);
    m_overlayBtn->setChecked(checked);
    m_overlayVisible = checked;
    m_overlayBtn->setText(checked ? tr("隐藏键位") : tr("显示键位"));
    m_overlayBtn->blockSignals(false);
}

void ToolForm::onConfigChanged(const QString& text) {
    if(!text.isEmpty()) emit keyMapChanged(text);
}

void ToolForm::createNewConfig() {
    bool ok;
    QString text = QInputDialog::getText(this, tr("新建配置"), tr("文件名:"), QLineEdit::Normal, "new_config", &ok);
    if(ok && !text.isEmpty()) {
        if(!text.endsWith(".json")) text+=".json";
        QDir dir("keymap"); if(!dir.exists()) dir.mkpath(".");
        QString filePath = dir.filePath(text);

        // 检查文件是否已存在
        if (QFile::exists(filePath)) {
            QMessageBox::StandardButton reply = QMessageBox::question(
                this,
                tr("文件已存在"),
                tr("配置文件 \"%1\" 已存在。\n是否覆盖？").arg(text),
                QMessageBox::Yes | QMessageBox::No,
                QMessageBox::No
            );
            if (reply != QMessageBox::Yes) {
                return;  // 用户选择不覆盖，返回
            }
        }

        QFile file(filePath);
        if(file.open(QIODevice::WriteOnly)) {
            file.write("{}"); file.close();
            refreshKeyMapList();
            m_configComboBox->setCurrentText(text);
        }
    }
}

void ToolForm::openKeyMapFolder() {
    QDir dir("keymap");
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir.absolutePath()));
}


void ToolForm::refreshConfig() {
    emit keyMapChanged(getCurrentKeyMapFile());
}


void ToolForm::saveConfig() {
    emit keyMapSaveRequested();
}

void ToolForm::showAntiDetectSettings() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("设置"));
    dialog.setFixedSize(300, 580);
    dialog.setStyleSheet(
        "QDialog{background:#18181b;}"
        "QLabel{color:#fafafa;font-size:11px;}"
        "QSlider::groove:horizontal{height:6px;background:#3f3f46;border-radius:3px;}"
        "QSlider::handle:horizontal{width:14px;height:14px;margin:-4px 0;background:#6366f1;border-radius:7px;}"
        "QSlider::handle:horizontal:hover{background:#818cf8;}"
        "QSlider::sub-page:horizontal{background:#6366f1;border-radius:3px;}"
    );

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(8);

    // ===== 随机偏移 =====
    QLabel* randomTitle = new QLabel(tr("随机偏移"), &dialog);
    randomTitle->setStyleSheet("font-weight:600;font-size:12px;color:#a1a1aa;");
    layout->addWidget(randomTitle);

    QHBoxLayout* randomLayout = new QHBoxLayout();
    QSlider* randomSlider = new QSlider(Qt::Horizontal, &dialog);
    randomSlider->setRange(0, 100);
    randomSlider->setValue(qsc::ConfigCenter::instance().randomOffset());
    QLabel* randomValue = new QLabel(QString::number(randomSlider->value()), &dialog);
    randomValue->setFixedWidth(28);
    randomValue->setAlignment(Qt::AlignCenter);
    randomValue->setStyleSheet("color:#22c55e;font-weight:600;");
    connect(randomSlider, &QSlider::valueChanged, [randomValue](int v) { randomValue->setText(QString::number(v)); });
    randomLayout->addWidget(randomSlider);
    randomLayout->addWidget(randomValue);
    layout->addLayout(randomLayout);

    // 分隔线
    QFrame* sep1 = new QFrame(&dialog);
    sep1->setFrameShape(QFrame::HLine);
    sep1->setFixedHeight(1);
    sep1->setStyleSheet("background:#3f3f46;margin:4px 0;");
    layout->addWidget(sep1);

    // ===== 轮盘平滑 =====
    QLabel* smoothTitle = new QLabel(tr("轮盘平滑"), &dialog);
    smoothTitle->setStyleSheet("font-weight:600;font-size:12px;color:#a1a1aa;");
    layout->addWidget(smoothTitle);

    QLabel* smoothDesc = new QLabel(tr("0=瞬间移动, 100=高平滑缓动"), &dialog);
    smoothDesc->setStyleSheet("color:#71717a;font-size:9px;");
    layout->addWidget(smoothDesc);

    QHBoxLayout* smoothLayout = new QHBoxLayout();
    QSlider* smoothSlider = new QSlider(Qt::Horizontal, &dialog);
    smoothSlider->setRange(0, 100);
    smoothSlider->setValue(qsc::ConfigCenter::instance().steerWheelSmooth());
    QLabel* smoothValue = new QLabel(QString::number(smoothSlider->value()), &dialog);
    smoothValue->setFixedWidth(28);
    smoothValue->setAlignment(Qt::AlignCenter);
    smoothValue->setStyleSheet("color:#22c55e;font-weight:600;");
    connect(smoothSlider, &QSlider::valueChanged, [smoothValue](int v) { smoothValue->setText(QString::number(v)); });
    smoothLayout->addWidget(smoothSlider);
    smoothLayout->addWidget(smoothValue);
    layout->addLayout(smoothLayout);

    // 分隔线
    QFrame* sep2 = new QFrame(&dialog);
    sep2->setFrameShape(QFrame::HLine);
    sep2->setFixedHeight(1);
    sep2->setStyleSheet("background:#3f3f46;margin:4px 0;");
    layout->addWidget(sep2);

    // ===== 轮盘曲线 =====
    QLabel* curveTitle = new QLabel(tr("轮盘拟人曲线"), &dialog);
    curveTitle->setStyleSheet("font-weight:600;font-size:12px;color:#a1a1aa;");
    layout->addWidget(curveTitle);

    QLabel* curveDesc = new QLabel(tr("0=直线移动, 100=最大弧度曲线"), &dialog);
    curveDesc->setStyleSheet("color:#71717a;font-size:9px;");
    layout->addWidget(curveDesc);

    QHBoxLayout* curveLayout = new QHBoxLayout();
    QSlider* curveSlider = new QSlider(Qt::Horizontal, &dialog);
    curveSlider->setRange(0, 100);
    curveSlider->setValue(qsc::ConfigCenter::instance().steerWheelCurve());
    QLabel* curveValue = new QLabel(QString::number(curveSlider->value()), &dialog);
    curveValue->setFixedWidth(28);
    curveValue->setAlignment(Qt::AlignCenter);
    curveValue->setStyleSheet("color:#22c55e;font-weight:600;");
    connect(curveSlider, &QSlider::valueChanged, [curveValue](int v) { curveValue->setText(QString::number(v)); });
    curveLayout->addWidget(curveSlider);
    curveLayout->addWidget(curveValue);
    layout->addLayout(curveLayout);

    // 分隔线
    QFrame* sep3 = new QFrame(&dialog);
    sep3->setFrameShape(QFrame::HLine);
    sep3->setFixedHeight(1);
    sep3->setStyleSheet("background:#3f3f46;margin:4px 0;");
    layout->addWidget(sep3);

    // ===== 滑动曲线 =====
    QLabel* slideCurveTitle = new QLabel(tr("滑动曲线"), &dialog);
    slideCurveTitle->setStyleSheet("font-weight:600;font-size:12px;color:#a1a1aa;");
    layout->addWidget(slideCurveTitle);

    QLabel* slideCurveDesc = new QLabel(tr("脚本slide等滑动API的轨迹曲线"), &dialog);
    slideCurveDesc->setStyleSheet("color:#71717a;font-size:9px;");
    layout->addWidget(slideCurveDesc);

    QHBoxLayout* slideCurveLayout = new QHBoxLayout();
    QSlider* slideCurveSlider = new QSlider(Qt::Horizontal, &dialog);
    slideCurveSlider->setRange(0, 100);
    slideCurveSlider->setValue(qsc::ConfigCenter::instance().slideCurve());
    QLabel* slideCurveValue = new QLabel(QString::number(slideCurveSlider->value()), &dialog);
    slideCurveValue->setFixedWidth(28);
    slideCurveValue->setAlignment(Qt::AlignCenter);
    slideCurveValue->setStyleSheet("color:#22c55e;font-weight:600;");
    connect(slideCurveSlider, &QSlider::valueChanged, [slideCurveValue](int v) { slideCurveValue->setText(QString::number(v)); });
    slideCurveLayout->addWidget(slideCurveSlider);
    slideCurveLayout->addWidget(slideCurveValue);
    layout->addLayout(slideCurveLayout);

    // 分隔线
    QFrame* sep3b = new QFrame(&dialog);
    sep3b->setFrameShape(QFrame::HLine);
    sep3b->setFixedHeight(1);
    sep3b->setStyleSheet("background:#3f3f46;margin:4px 0;");
    layout->addWidget(sep3b);

    // ===== 键位透明度 =====
    QLabel* opacityTitle = new QLabel(tr("键位提示透明度"), &dialog);
    opacityTitle->setStyleSheet("font-weight:600;font-size:12px;color:#a1a1aa;");
    layout->addWidget(opacityTitle);

    QLabel* opacityDesc = new QLabel(tr("0=全透明, 100=不透明"), &dialog);
    opacityDesc->setStyleSheet("color:#71717a;font-size:9px;");
    layout->addWidget(opacityDesc);

    QHBoxLayout* opacityLayout = new QHBoxLayout();
    QSlider* opacitySlider = new QSlider(Qt::Horizontal, &dialog);
    opacitySlider->setRange(0, 100);
    opacitySlider->setValue(qsc::ConfigCenter::instance().keyMapOverlayOpacity());
    QLabel* opacityValue = new QLabel(QString::number(opacitySlider->value()), &dialog);
    opacityValue->setFixedWidth(28);
    opacityValue->setAlignment(Qt::AlignCenter);
    opacityValue->setStyleSheet("color:#22c55e;font-weight:600;");
    connect(opacitySlider, &QSlider::valueChanged, [opacityValue](int v) { opacityValue->setText(QString::number(v)); });
    opacityLayout->addWidget(opacitySlider);
    opacityLayout->addWidget(opacityValue);
    layout->addLayout(opacityLayout);

    // 分隔线
    QFrame* sep4 = new QFrame(&dialog);
    sep4->setFrameShape(QFrame::HLine);
    sep4->setFixedHeight(1);
    sep4->setStyleSheet("background:#3f3f46;margin:4px 0;");
    layout->addWidget(sep4);

    // ===== 脚本弹窗透明度 =====
    QLabel* tipOpacityTitle = new QLabel(tr("脚本弹窗透明度"), &dialog);
    tipOpacityTitle->setStyleSheet("font-weight:600;font-size:12px;color:#a1a1aa;");
    layout->addWidget(tipOpacityTitle);

    QLabel* tipOpacityDesc = new QLabel(tr("0=全透明, 100=不透明"), &dialog);
    tipOpacityDesc->setStyleSheet("color:#71717a;font-size:9px;");
    layout->addWidget(tipOpacityDesc);

    QHBoxLayout* tipOpacityLayout = new QHBoxLayout();
    QSlider* tipOpacitySlider = new QSlider(Qt::Horizontal, &dialog);
    tipOpacitySlider->setRange(0, 100);
    tipOpacitySlider->setValue(qsc::ConfigCenter::instance().scriptTipOpacity());
    QLabel* tipOpacityValue = new QLabel(QString::number(tipOpacitySlider->value()), &dialog);
    tipOpacityValue->setFixedWidth(28);
    tipOpacityValue->setAlignment(Qt::AlignCenter);
    tipOpacityValue->setStyleSheet("color:#22c55e;font-weight:600;");
    connect(tipOpacitySlider, &QSlider::valueChanged, [tipOpacityValue](int v) { tipOpacityValue->setText(QString::number(v)); });
    tipOpacityLayout->addWidget(tipOpacitySlider);
    tipOpacityLayout->addWidget(tipOpacityValue);
    layout->addLayout(tipOpacityLayout);

    layout->addStretch();

    // 确定按钮
    QPushButton* okBtn = new QPushButton(tr("确定"), &dialog);
    okBtn->setStyleSheet(
        "QPushButton{background:#6366f1;color:#ffffff;border:none;border-radius:6px;padding:10px;font-weight:600;}"
        "QPushButton:hover{background:#818cf8;}"
    );
    connect(okBtn, &QPushButton::clicked, [this, &dialog, randomSlider, smoothSlider, curveSlider, slideCurveSlider, opacitySlider, tipOpacitySlider]() {
        qsc::ConfigCenter::instance().setRandomOffset(randomSlider->value());
        qsc::ConfigCenter::instance().setSteerWheelSmooth(smoothSlider->value());
        qsc::ConfigCenter::instance().setSteerWheelCurve(curveSlider->value());
        qsc::ConfigCenter::instance().setSlideCurve(slideCurveSlider->value());
        qsc::ConfigCenter::instance().setKeyMapOverlayOpacity(opacitySlider->value());
        qsc::ConfigCenter::instance().setScriptTipOpacity(tipOpacitySlider->value());
        emit keyMapOverlayOpacityChanged(opacitySlider->value());
        emit scriptTipOpacityChanged(tipOpacitySlider->value());
        dialog.accept();
    });
    layout->addWidget(okBtn);

    dialog.exec();
}

// ---------------------------------------------------------
// 设备控制按钮槽函数
// 发送ADB控制指令
// ---------------------------------------------------------
void ToolForm::on_fullScreenBtn_clicked() {
    if (auto* vf = qobject_cast<VideoForm*>(parent())) {
        if (vf->session()) vf->switchFullScreen();
    }
}
void ToolForm::on_returnBtn_clicked() {
    if (auto* vf = qobject_cast<VideoForm*>(parent())) {
        if (auto* s = vf->session()) s->postGoBack();
    }
}
void ToolForm::on_homeBtn_clicked() {
    if (auto* vf = qobject_cast<VideoForm*>(parent())) {
        if (auto* s = vf->session()) s->postGoHome();
    }
}
void ToolForm::on_appSwitchBtn_clicked() {
    if (auto* vf = qobject_cast<VideoForm*>(parent())) {
        if (auto* s = vf->session()) s->postAppSwitch();
    }
}

// 切换键位编辑模式
void ToolForm::on_keyMapBtn_clicked() {
    m_isKeyMapMode = !m_isKeyMapMode;
    if(m_isKeyMapMode) {
        ui->keyMapBtn->setStyleSheet(
            "QPushButton{background:#6366f1;border:none;border-radius:10px;color:#ffffff;}"
            "QPushButton:hover{background:#818cf8;}"
        );
        ui->stackedWidget->setCurrentIndex(1);
        // 编辑模式下加宽以显示完整内容
        setFixedWidth(90);
        refreshKeyMapList();
    } else {
        ui->keyMapBtn->setStyleSheet(
            "QPushButton{background:#27272a;border:1px solid #3f3f46;border-radius:10px;color:#fafafa;}"
            "QPushButton:hover{background:#3f3f46;border-color:#6366f1;}"
        );
        ui->stackedWidget->setCurrentIndex(0);
        setFixedWidth(64);
    }
    // 自适应高度
    adjustSize();

    // 先发送 UI 状态更新信号（show/hide 编辑视图）
    // 这样 loadKeyMap 检查 isVisible 时才能得到正确的结果
    emit keyMapEditModeToggled(m_isKeyMapMode);

    // 然后加载键位配置
    // - 进入编辑模式：m_keyMapEditView 已经 show，isVisible=true，不执行自动启动脚本
    // - 退出编辑模式：m_keyMapEditView 已经 hide，isVisible=false，执行自动启动脚本
    emit keyMapChanged(getCurrentKeyMapFile());
}

void ToolForm::setSerial(const QString &serial) { m_serial = serial; }
bool ToolForm::isHost() { return m_isHost; }

// 初始化FontAwesome图标
void ToolForm::initStyle() {
    IconHelper::Instance()->SetIcon(ui->fullScreenBtn, QChar(0xf0b2), 15);
    IconHelper::Instance()->SetIcon(ui->homeBtn, QChar(0xf1db), 15);
    IconHelper::Instance()->SetIcon(ui->returnBtn, QChar(0xf053), 15);
    IconHelper::Instance()->SetIcon(ui->appSwitchBtn, QChar(0xf24d), 15);
    IconHelper::Instance()->SetIcon(ui->keyMapBtn, QChar(0xf11c), 15);
}

// 窗口拖动逻辑
void ToolForm::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        m_dragPosition = event->globalPos() - frameGeometry().topLeft();
#else
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
#endif
        event->accept();
    }
}
void ToolForm::mouseReleaseEvent(QMouseEvent *event) { Q_UNUSED(event) }
void ToolForm::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton) {
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
        move(event->globalPos() - m_dragPosition);
#else
        move(event->globalPosition().toPoint() - m_dragPosition);
#endif
        event->accept();
    }
}
void ToolForm::showEvent(QShowEvent *event) { Q_UNUSED(event) }
void ToolForm::hideEvent(QHideEvent *event) { Q_UNUSED(event) }

void ToolForm::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    MagneticWidget::changeEvent(event);
}

void ToolForm::retranslateUi()
{
    // 工具按钮提示
    if (m_saveBtn) {
        m_saveBtn->setText(tr("保存"));
        m_saveBtn->setToolTip(tr("保存当前配置"));
    }
    if (m_overlayBtn) {
        m_overlayBtn->setText(m_overlayVisible ? tr("隐藏键位") : tr("显示键位"));
        m_overlayBtn->setToolTip(tr("显示/隐藏键位提示"));
    }
    if (m_antiDetectBtn) {
        m_antiDetectBtn->setText(tr("设置"));
        m_antiDetectBtn->setToolTip(tr("打开设置面板"));
    }
    if (m_refreshBtn) m_refreshBtn->setToolTip(tr("刷新配置列表"));
    if (m_folderBtn) m_folderBtn->setToolTip(tr("打开配置文件夹"));
    if (m_newConfigBtn) m_newConfigBtn->setToolTip(tr("新建配置"));

    // 可拖拽标签
    if (m_scriptLabel) m_scriptLabel->setText(tr("脚本"));
    if (m_steerLabel) m_steerLabel->setText(tr("轮盘"));
    if (m_cameraLabel) m_cameraLabel->setText(tr("视角"));
    if (m_freeLookLabel) m_freeLookLabel->setText(tr("小眼睛"));
}
