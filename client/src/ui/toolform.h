// Header definition
#ifndef TOOLFORM_H
#define TOOLFORM_H

#include <QWidget>
#include <QLabel>
#include <QMouseEvent>
#include <QComboBox>
#include <QPushButton>
#include <QPointer>
#include <QEvent>
#include "magneticwidget.h"
#include "KeyMapBase.h"

namespace Ui { class ToolForm; }

// 可拖拽标签类 / Draggable Label Class
class DraggableLabel : public QLabel {
    Q_OBJECT
public:
    DraggableLabel(KeyMapType type, const QString& text, QWidget* parent = nullptr, const QString& preset = QString());
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
private:
    KeyMapType m_type; QString m_preset; QPoint m_dragStartPosition;
};

// 浮动工具栏类 / Floating Toolbar Class
class ToolForm : public MagneticWidget
{
    Q_OBJECT
public:
    explicit ToolForm(QWidget *adsorbWidget, AdsorbPositions adsorbPos);
    ~ToolForm();
    void setSerial(const QString& serial);
    bool isHost();
    QString getCurrentKeyMapFile();
    void setCurrentKeyMap(const QString& filename);
    void setOverlayButtonState(bool checked);  // 同步 overlay 按钮状态

signals:
    void keyMapEditModeToggled(bool active);
    void keyMapChanged(const QString& filename);
    void keyMapSaveRequested();
    void keyMapOverlayToggled(bool visible);
    void keyMapOverlayOpacityChanged(int opacity);
    void scriptTipOpacityChanged(int opacity);

protected:
    void mousePressEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void showEvent(QShowEvent *event);
    void hideEvent(QHideEvent *event);
    void changeEvent(QEvent *event) override;

private slots:
    void on_fullScreenBtn_clicked(); void on_returnBtn_clicked(); void on_homeBtn_clicked();
    void on_appSwitchBtn_clicked();
    void on_keyMapBtn_clicked();

    void onConfigChanged(const QString& text);
    void createNewConfig();
    void refreshConfig();
    void saveConfig();
    void showAntiDetectSettings();
    void openKeyMapFolder();

private:
    void initStyle();
    void initKeyMapPalette();
    void refreshKeyMapList();
    void retranslateUi();

    Ui::ToolForm *ui;
    QPoint m_dragPosition; QString m_serial; bool m_showTouch = false; bool m_isHost = false; bool m_isKeyMapMode = false;
    QComboBox* m_configComboBox = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_newConfigBtn = nullptr;
    QPushButton* m_refreshBtn = nullptr;
    QPushButton* m_folderBtn = nullptr;
    QPushButton* m_antiDetectBtn = nullptr;
    QPushButton* m_overlayBtn = nullptr;
    bool m_overlayVisible = false;

    // 可翻译的拖拽标签 / Translatable draggable labels
    DraggableLabel* m_scriptLabel = nullptr;
    DraggableLabel* m_steerLabel = nullptr;
    DraggableLabel* m_cameraLabel = nullptr;
    DraggableLabel* m_freeLookLabel = nullptr;
};

#endif // TOOLFORM_H
