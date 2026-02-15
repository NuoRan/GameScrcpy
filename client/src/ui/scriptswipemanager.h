#ifndef SCRIPTSWIPEMANAGER_H
#define SCRIPTSWIPEMANAGER_H

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
// 滑动路径数据结构 / Script Swipe Data Structure
// 表示从起点到终点的一条滑动路径
// ---------------------------------------------------------
struct ScriptSwipe
{
    int id = 0;             // 滑动编号 (唯一) / Swipe ID (unique)
    QString name;           // 备注名字 / Label name
    double x0 = 0.3;       // 起点 x (0.0~1.0) / Start x
    double y0 = 0.5;       // 起点 y (0.0~1.0) / Start y
    double x1 = 0.7;       // 终点 x (0.0~1.0) / End x
    double y1 = 0.5;       // 终点 y (0.0~1.0) / End y

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["id"] = id;
        obj["name"] = name;
        obj["x0"] = x0;
        obj["y0"] = y0;
        obj["x1"] = x1;
        obj["y1"] = y1;
        return obj;
    }

    static ScriptSwipe fromJson(const QJsonObject& obj) {
        ScriptSwipe s;
        s.id = obj["id"].toInt();
        s.name = obj["name"].toString();
        s.x0 = obj["x0"].toDouble(0.3);
        s.y0 = obj["y0"].toDouble(0.5);
        s.x1 = obj["x1"].toDouble(0.7);
        s.y1 = obj["y1"].toDouble(0.5);
        return s;
    }

    QString coordString() const {
        return QString("%1, %2, %3, %4")
            .arg(QString::number(x0, 'f', 4))
            .arg(QString::number(y0, 'f', 4))
            .arg(QString::number(x1, 'f', 4))
            .arg(QString::number(y1, 'f', 4));
    }
};

// ---------------------------------------------------------
// 滑动路径管理器 - 管理脚本滑动路径的增删改查和持久化
// 线程安全：所有公共方法均通过 QReadWriteLock 保护
// ---------------------------------------------------------
class ScriptSwipeManager : public QObject
{
    Q_OBJECT
public:
    static ScriptSwipeManager& instance() {
        static ScriptSwipeManager s_instance;
        return s_instance;
    }

    static QString configPath() {
        return QCoreApplication::applicationDirPath() + "/keymap/swipes.json";
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

    QVector<ScriptSwipe> swipes() const {
        QReadLocker locker(&m_lock);
        return m_swipes;
    }

    bool findById(int id, ScriptSwipe& out) const {
        QReadLocker locker(&m_lock);
        const ScriptSwipe* s = findByIdInternal(id);
        if (s) { out = *s; return true; }
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

    bool add(const ScriptSwipe& swipe) {
        QWriteLocker locker(&m_lock);
        if (findByIdInternal(swipe.id)) return false;
        m_swipes.append(swipe);
        saveInternal();
        return true;
    }

    bool remove(int id) {
        QWriteLocker locker(&m_lock);
        for (int i = 0; i < m_swipes.size(); ++i) {
            if (m_swipes[i].id == id) {
                m_swipes.removeAt(i);
                saveInternal();
                return true;
            }
        }
        return false;
    }

    bool rename(int id, const QString& newName) {
        QWriteLocker locker(&m_lock);
        if (nameExistsInternal(newName, id)) return false;
        for (auto& s : m_swipes) {
            if (s.id == id) {
                s.name = newName;
                saveInternal();
                return true;
            }
        }
        return false;
    }

    bool updateCoords(int id, double x0, double y0, double x1, double y1) {
        QWriteLocker locker(&m_lock);
        for (auto& s : m_swipes) {
            if (s.id == id) {
                s.x0 = x0;
                s.y0 = y0;
                s.x1 = x1;
                s.y1 = y1;
                saveInternal();
                return true;
            }
        }
        return false;
    }

private:
    ScriptSwipeManager() { loadInternal(); }
    ~ScriptSwipeManager() = default;
    ScriptSwipeManager(const ScriptSwipeManager&) = delete;
    ScriptSwipeManager& operator=(const ScriptSwipeManager&) = delete;

    void loadInternal() {
        m_swipes.clear();
        QFile file(configPath());
        if (!file.open(QIODevice::ReadOnly)) return;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (!doc.isArray()) return;
        const QJsonArray arr = doc.array();
        for (const QJsonValue& v : arr) {
            m_swipes.append(ScriptSwipe::fromJson(v.toObject()));
        }
    }

    bool saveInternal() const {
        QDir dir;
        dir.mkpath(configDir());
        QFile file(configPath());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        QJsonArray arr;
        for (const ScriptSwipe& s : m_swipes) {
            arr.append(s.toJson());
        }
        file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
        file.close();
        return true;
    }

    const ScriptSwipe* findByIdInternal(int id) const {
        for (const auto& s : m_swipes) {
            if (s.id == id) return &s;
        }
        return nullptr;
    }

    int nextIdInternal() const {
        int maxId = 0;
        for (const auto& s : m_swipes) {
            if (s.id > maxId) maxId = s.id;
        }
        return maxId + 1;
    }

    bool nameExistsInternal(const QString& name, int excludeId = -1) const {
        for (const auto& s : m_swipes) {
            if (s.name == name && s.id != excludeId) return true;
        }
        return false;
    }

    QVector<ScriptSwipe> m_swipes;
    mutable QReadWriteLock m_lock;
};

#endif // SCRIPTSWIPEMANAGER_H
