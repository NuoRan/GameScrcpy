#include "toolform.h"
#include "ui_toolform.h"
#include "videoform.h"
#include "iconhelper.h"
#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDir>
#include <QInputDialog>
#include <QFile>
#include <QDebug>

// ---------------------------------------------------------
// 可拖拽的标签 (DraggableLabel)
// 实现从工具栏拖拽键位元素到视频窗口的逻辑
// ---------------------------------------------------------
DraggableLabel::DraggableLabel(KeyMapType type, const QString& text, QWidget* parent) : QLabel(text, parent), m_type(type) {
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

    // 配置选择下拉框
    m_configComboBox = new QComboBox(ui->page_keymap);
    m_configComboBox->setMinimumHeight(32);
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

    // 刷新按钮
    m_refreshBtn = new QPushButton("↻", ui->page_keymap);
    m_refreshBtn->setMinimumHeight(32);
    m_refreshBtn->setToolTip("刷新配置");
    m_refreshBtn->setStyleSheet(
        "QPushButton{background:#27272a;color:#fafafa;border:1px solid #3f3f46;border-radius:6px;font-size:14px;}"
        "QPushButton:hover{background:#3f3f46;border-color:#6366f1;}"
    );
    connect(m_refreshBtn, &QPushButton::clicked, this, &ToolForm::refreshConfig);
    layout->addWidget(m_refreshBtn);

    // 新建按钮
    m_newConfigBtn = new QPushButton("+ 新建", ui->page_keymap);
    m_newConfigBtn->setMinimumHeight(32);
    m_newConfigBtn->setStyleSheet(
        "QPushButton{background:#27272a;color:#fafafa;border:1px solid #3f3f46;border-radius:6px;font-size:9px;}"
        "QPushButton:hover{background:#3f3f46;}"
    );
    connect(m_newConfigBtn, &QPushButton::clicked, this, &ToolForm::createNewConfig);
    layout->addWidget(m_newConfigBtn);

    // 保存按钮
    m_saveBtn = new QPushButton("保存", ui->page_keymap);
    m_saveBtn->setMinimumHeight(32);
    m_saveBtn->setStyleSheet(
        "QPushButton{background:#6366f1;color:#ffffff;border:none;border-radius:6px;font-size:9px;font-weight:600;}"
        "QPushButton:hover{background:#818cf8;}"
    );
    connect(m_saveBtn, &QPushButton::clicked, this, &ToolForm::saveConfig);
    layout->addWidget(m_saveBtn);

    // 分隔线
    QFrame* separator = new QFrame(ui->page_keymap);
    separator->setFrameShape(QFrame::HLine);
    separator->setFixedHeight(1);
    separator->setStyleSheet("background:#3f3f46;margin:4px 0;");
    layout->addWidget(separator);

    // 可拖拽键位元素 - 居中对齐
    DraggableLabel* steerLabel = new DraggableLabel(KMT_STEER_WHEEL, "轮盘", ui->page_keymap);
    DraggableLabel* scriptLabel = new DraggableLabel(KMT_SCRIPT, "脚本", ui->page_keymap);
    DraggableLabel* cameraLabel = new DraggableLabel(KMT_CAMERA_MOVE, "视角", ui->page_keymap);

    layout->addWidget(steerLabel, 0, Qt::AlignHCenter);
    layout->addWidget(scriptLabel, 0, Qt::AlignHCenter);
    layout->addWidget(cameraLabel, 0, Qt::AlignHCenter);

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

void ToolForm::onConfigChanged(const QString& text) {
    if(!text.isEmpty()) emit keyMapChanged(text);
}

void ToolForm::createNewConfig() {
    bool ok;
    QString text = QInputDialog::getText(this, "新建配置", "文件名:", QLineEdit::Normal, "new_config", &ok);
    if(ok && !text.isEmpty()) {
        if(!text.endsWith(".json")) text+=".json";
        QDir dir("keymap"); if(!dir.exists()) dir.mkpath(".");
        QFile file(dir.filePath(text));
        if(file.open(QIODevice::WriteOnly)) {
            file.write("{}"); file.close();
            refreshKeyMapList();
            m_configComboBox->setCurrentText(text);
        }
    }
}


void ToolForm::refreshConfig() {
    emit keyMapChanged(getCurrentKeyMapFile());
}


void ToolForm::saveConfig() {
    emit keyMapSaveRequested();
}

// ---------------------------------------------------------
// 设备控制按钮槽函数
// 发送ADB控制指令
// ---------------------------------------------------------
void ToolForm::on_fullScreenBtn_clicked() { if(auto d=qsc::IDeviceManage::getInstance().getDevice(m_serial)) dynamic_cast<VideoForm*>(parent())->switchFullScreen(); }
void ToolForm::on_returnBtn_clicked() { if(auto d=qsc::IDeviceManage::getInstance().getDevice(m_serial)) d->postGoBack(); }
void ToolForm::on_homeBtn_clicked() { if(auto d=qsc::IDeviceManage::getInstance().getDevice(m_serial)) d->postGoHome(); }
void ToolForm::on_appSwitchBtn_clicked() { if(auto d=qsc::IDeviceManage::getInstance().getDevice(m_serial)) d->postAppSwitch(); }

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
        emit keyMapChanged(getCurrentKeyMapFile());
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
    emit keyMapEditModeToggled(m_isKeyMapMode);
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

