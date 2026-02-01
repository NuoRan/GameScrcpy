// Header definition
#ifndef TOOLFORM_H
#define TOOLFORM_H

#include <QWidget>
#include <QLabel>
#include <QMouseEvent>
#include <QComboBox>
#include <QPushButton>
#include "magneticwidget.h"
#include "KeyMapBase.h"

namespace Ui { class ToolForm; }

// 可拖拽标签类
class DraggableLabel : public QLabel {
    Q_OBJECT
public:
    DraggableLabel(KeyMapType type, const QString& text, QWidget* parent = nullptr);
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
private:
    KeyMapType m_type; QPoint m_dragStartPosition;
};

// 浮动工具栏类
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

signals:
    void keyMapEditModeToggled(bool active);
    void keyMapChanged(const QString& filename);
    void keyMapSaveRequested();

protected:
    void mousePressEvent(QMouseEvent *event);
    void mouseReleaseEvent(QMouseEvent *event);
    void mouseMoveEvent(QMouseEvent *event);
    void showEvent(QShowEvent *event);
    void hideEvent(QHideEvent *event);

private slots:
    void on_fullScreenBtn_clicked(); void on_returnBtn_clicked(); void on_homeBtn_clicked();
    void on_appSwitchBtn_clicked();
    void on_keyMapBtn_clicked();

    void onConfigChanged(const QString& text);
    void createNewConfig();
    void refreshConfig();
    void saveConfig();

private:
    void initStyle();
    void initKeyMapPalette();
    void refreshKeyMapList();

    Ui::ToolForm *ui;
    QPoint m_dragPosition; QString m_serial; bool m_showTouch = false; bool m_isHost = false; bool m_isKeyMapMode = false;
    QComboBox* m_configComboBox = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_newConfigBtn = nullptr;
    QPushButton* m_refreshBtn = nullptr;
};

#endif // TOOLFORM_H
