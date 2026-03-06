#ifndef ICONHELPER_H
#define ICONHELPER_H

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QLabel>
#include <mutex>
#include <QObject>
#include <QPushButton>

/**
 * @brief 图标字体助手 / Icon Font Helper (Singleton)
 *
 * 使用 FontAwesome 等图标字体为控件设置图标。
 * Uses icon fonts (FontAwesome, etc.) to set icons on widgets.
 */
class IconHelper : public QObject
{
private:
    explicit IconHelper(QObject *parent = 0);
    QFont iconFont;
    static IconHelper *_instance;

public:
    static IconHelper *Instance()
    {
        static std::mutex mutex;
        if (!_instance) {
            std::lock_guard<std::mutex> locker(mutex);
            if (!_instance) {
                _instance = new IconHelper;
            }
        }
        return _instance;
    }

    void SetIcon(QLabel *lab, QChar c, int size = 10);
    void SetIcon(QPushButton *btn, QChar c, int size = 10);
};

#endif // ICONHELPER_H
