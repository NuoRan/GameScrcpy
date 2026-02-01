#ifndef VIDEOFORM_H
#define VIDEOFORM_H

#include <QWidget>
#include <QPointer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QMutex>
#include <QVariant>
#include <QPointF>
#include <atomic>

#include "QtScrcpyCore.h"
#include "KeyMapEditView.h"

namespace Ui { class videoForm; }
class ToolForm; class QYUVOpenGLWidget; class QLabel;

// ==========================================
// 视频显示窗口类
// 负责显示设备画面、处理用户输入及键位映射
// ==========================================
class VideoForm : public QWidget, public qsc::DeviceObserver
{
    Q_OBJECT
public:
    explicit VideoForm(bool framelessWindow = false, bool skin = true, bool showToolBar = true, QWidget *parent = 0);
    ~VideoForm();

    // 窗口控制接口
    void staysOnTop(bool top = true);
    void updateShowSize(const QSize &newSize);
    void updateRender(int width, int height, uint8_t* dataY, uint8_t* dataU, uint8_t* dataV, int linesizeY, int linesizeU, int linesizeV);
    void setSerial(const QString& serial);
    QRect getGrabCursorRect();
    const QSize &frameSize();
    void resizeSquare();
    void removeBlackRect();
    void showFPS(bool show);
    void switchFullScreen();
    bool isHost();

    // 脚本控制接口：模拟触控和按键
    void sendTouchDown(int id, float x, float y);
    void sendTouchMove(int id, float x, float y);
    void sendTouchUp(int id, float x, float y);
    void sendKeyClick(int qtKey);

    // 获取当前视频帧 (用于图像识别)
    QImage grabCurrentFrame();

public slots:
    // 键位映射管理
    void loadKeyMap(const QString& filename);
    void saveKeyMap();

private slots:
    void onKeyMapEditModeToggled(bool active);

private:
    // DeviceObserver 接口实现
    void onFrame(int width, int height, uint8_t* dataY, uint8_t* dataU, uint8_t* dataV, int linesizeY, int linesizeU, int linesizeV) override;
    void updateFPS(quint32 fps) override;
    void grabCursor(bool grab) override;

    // 内部辅助方法
    void updateStyleSheet(bool vertical);
    QMargins getMargins(bool vertical);
    void initUI();
    void showToolForm(bool show = true);
    void moveCenter();
    QRect getScreenRect();

protected:
    // 事件处理 override
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    Ui::videoForm *ui;
    QPointer<ToolForm> m_toolForm;
    QPointer<QWidget> m_loadingWidget;
    QPointer<QYUVOpenGLWidget> m_videoWidget;
    QPointer<QLabel> m_fpsLabel;
    KeyMapEditView* m_keyMapEditView = nullptr;

    QJsonObject m_currentConfigBase;
    QString m_currentKeyMapFile;

    QSize m_frameSize; QSize m_normalSize; QPoint m_dragPosition;
    float m_widthHeightRatio = 0.5f; bool m_skin = true;
    QPoint m_fullScreenBeforePos; QString m_serial; bool show_toolbar = true;

    // 防止重复鼠标事件
    Qt::MouseButtons m_pressedButtons;

    // 渲染更新节流（防止队列堆积导致UI卡死）
    std::atomic<bool> m_renderQueued{false};
};

#endif // VIDEOFORM_H
