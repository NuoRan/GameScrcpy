#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqmlregistration.h>

namespace QWK {
class QuickWindowAgent;
}

namespace gs::app {

class WindowChrome : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickWindow* window READ window WRITE setWindow NOTIFY windowChanged FINAL)
    Q_PROPERTY(QQuickItem* titleBar READ titleBar WRITE setTitleBar NOTIFY titleBarChanged FINAL)
    Q_PROPERTY(QQuickItem* minimizeButton READ minimizeButton WRITE setMinimizeButton NOTIFY
                   minimizeButtonChanged FINAL)
    Q_PROPERTY(QQuickItem* maximizeButton READ maximizeButton WRITE setMaximizeButton NOTIFY
                   maximizeButtonChanged FINAL)
    Q_PROPERTY(QQuickItem* closeButton READ closeButton WRITE setCloseButton NOTIFY
                   closeButtonChanged FINAL)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged FINAL)

public:
    explicit WindowChrome(QObject* parent = nullptr);
    ~WindowChrome() override;

    Q_INVOKABLE bool attach();

    Q_INVOKABLE void setHitTestVisible(QQuickItem* item, bool visible = true);

    [[nodiscard]] QQuickWindow* window() const { return m_window; }
    void setWindow(QQuickWindow* window);

    [[nodiscard]] QQuickItem* titleBar() const { return m_titleBar; }
    void setTitleBar(QQuickItem* item);

    [[nodiscard]] QQuickItem* minimizeButton() const { return m_minimizeButton; }
    void setMinimizeButton(QQuickItem* item);

    [[nodiscard]] QQuickItem* maximizeButton() const { return m_maximizeButton; }
    void setMaximizeButton(QQuickItem* item);

    [[nodiscard]] QQuickItem* closeButton() const { return m_closeButton; }
    void setCloseButton(QQuickItem* item);

    [[nodiscard]] bool darkMode() const { return m_darkMode; }
    void setDarkMode(bool dark);

signals:
    void windowChanged();
    void titleBarChanged();
    void minimizeButtonChanged();
    void maximizeButtonChanged();
    void closeButtonChanged();
    void darkModeChanged();

private:
    void applyTitleBar();
    void applySystemButtons();
    void applyDarkMode();

    QPointer<QQuickWindow> m_window;
    QPointer<QQuickItem> m_titleBar;
    QPointer<QQuickItem> m_minimizeButton;
    QPointer<QQuickItem> m_maximizeButton;
    QPointer<QQuickItem> m_closeButton;
    bool m_darkMode = true;
    QWK::QuickWindowAgent* m_agent = nullptr;
};

}
