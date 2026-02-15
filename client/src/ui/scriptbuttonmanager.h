#ifndef SCRIPTBUTTONMANAGER_H
#define SCRIPTBUTTONMANAGER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <QReadWriteLock>

// ---------------------------------------------------------
// 虚拟按钮数据结构 / Script Button Data Structure
// 表示画面上的一个固定位置点
// ---------------------------------------------------------
struct ScriptButton
{
    int id = 0;             // 按钮编号 (唯一) / Button ID (unique)
    QString name;           // 备注名字 / Label name
    double x = 0.5;         // x 坐标 (0.0~1.0) / Normalized x
    double y = 0.5;         // y 坐标 (0.0~1.0) / Normalized y

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["name"] = name;
        obj["x"] = x;
        obj["y"] = y;
        return obj;
    }

    static ScriptButton fromJson(const QJsonObject& obj) {
        ScriptButton b;
        b.id = obj["id"].toInt();
        b.name = obj["name"].toString();
        b.x = obj["x"].toDouble(0.5);
        b.y = obj["y"].toDouble(0.5);
        return b;
    }

    QString coordString() const {
        return QString("%1, %2")
            .arg(QString::number(x, 'f', 4))
            .arg(QString::number(y, 'f', 4));
    }
};

// ---------------------------------------------------------
// 虚拟按钮管理器 - 管理脚本虚拟按钮的增删改查和持久化
// 线程安全：所有公共方法均通过 QReadWriteLock 保护
// ---------------------------------------------------------
class ScriptButtonManager : public QObject
{
    Q_OBJECT
public:
    static ScriptButtonManager& instance() {
        static ScriptButtonManager s_instance;
        return s_instance;
    }

    static QString configPath() {
        return QCoreApplication::applicationDirPath() + "/keymap/buttons.json";
    }

    static QString configDir() {
        return QCoreApplication::applicationDirPath() + "/keymap";
    }

    void load() {
        QWriteLocker locker(&m_lock);
        loadInternal();
    }

    bool save() const {
        QReadLocker locker(&m_lock);
        return saveInternal();
    }

    QVector<ScriptButton> buttons() const {
        QReadLocker locker(&m_lock);
        return m_buttons;
    }

    bool findById(int id, ScriptButton& out) const {
        QReadLocker locker(&m_lock);
        const ScriptButton* b = findByIdInternal(id);
        if (b) { out = *b; return true; }
        return false;
    }

    int nextId() const {
        QReadLocker locker(&m_lock);
        return nextIdInternal();
    }

    bool nameExists(const QString& name, int excludeId = -1) const {
        QReadLocker locker(&m_lock);
        return nameExistsInternal(name, excludeId);
    }

    bool add(const ScriptButton& button) {
        QWriteLocker locker(&m_lock);
        if (findByIdInternal(button.id)) return false;
        m_buttons.append(button);
        saveInternal();
        return true;
    }

    bool remove(int id) {
        QWriteLocker locker(&m_lock);
        for (int i = 0; i < m_buttons.size(); ++i) {
            if (m_buttons[i].id == id) {
                m_buttons.removeAt(i);
                saveInternal();
                return true;
            }
        }
        return false;
    }

    bool rename(int id, const QString& newName) {
        QWriteLocker locker(&m_lock);
        if (nameExistsInternal(newName, id)) return false;
        for (auto& b : m_buttons) {
            if (b.id == id) {
                b.name = newName;
                saveInternal();
                return true;
            }
        }
        return false;
    }

    bool updateCoords(int id, double x, double y) {
        QWriteLocker locker(&m_lock);
        for (auto& b : m_buttons) {
            if (b.id == id) {
                b.x = x;
                b.y = y;
                saveInternal();
                return true;
            }
        }
        return false;
    }

private:
    ScriptButtonManager() { loadInternal(); }
    ~ScriptButtonManager() = default;
    ScriptButtonManager(const ScriptButtonManager&) = delete;
    ScriptButtonManager& operator=(const ScriptButtonManager&) = delete;

    void loadInternal() {
        m_buttons.clear();
        QFile file(configPath());
        if (!file.open(QIODevice::ReadOnly)) return;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (!doc.isArray()) return;
        const QJsonArray arr = doc.array();
        for (const QJsonValue& v : arr) {
            m_buttons.append(ScriptButton::fromJson(v.toObject()));
        }
    }

    bool saveInternal() const {
        QDir dir;
        dir.mkpath(configDir());
        QFile file(configPath());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        QJsonArray arr;
        for (const ScriptButton& b : m_buttons) {
            arr.append(b.toJson());
        }
        file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
        file.close();
        return true;
    }

    const ScriptButton* findByIdInternal(int id) const {
        for (const auto& b : m_buttons) {
            if (b.id == id) return &b;
        }
        return nullptr;
    }

    int nextIdInternal() const {
        int maxId = 0;
        for (const auto& b : m_buttons) {
            if (b.id > maxId) maxId = b.id;
        }
        return maxId + 1;
    }

    bool nameExistsInternal(const QString& name, int excludeId = -1) const {
        for (const auto& b : m_buttons) {
            if (b.name == name && b.id != excludeId) return true;
        }
        return false;
    }

    QVector<ScriptButton> m_buttons;
    mutable QReadWriteLock m_lock;
};

#endif // SCRIPTBUTTONMANAGER_H
