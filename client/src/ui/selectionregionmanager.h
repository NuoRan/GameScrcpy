#ifndef SELECTIONREGIONMANAGER_H
#define SELECTIONREGIONMANAGER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <QRectF>
#include <QReadWriteLock>

// ---------------------------------------------------------
// 选区数据结构 / Selection Region Data Structure
// ---------------------------------------------------------
struct SelectionRegion
{
    int id = 0;             // 选区编号 (唯一) / Region ID (unique)
    QString name;           // 备注名字 / Label name
    double x0 = 0.0;       // 左上角 x (0.0~1.0) / Top-left x
    double y0 = 0.0;       // 左上角 y (0.0~1.0) / Top-left y
    double x1 = 1.0;       // 右下角 x (0.0~1.0) / Bottom-right x
    double y1 = 1.0;       // 右下角 y (0.0~1.0) / Bottom-right y

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

    static SelectionRegion fromJson(const QJsonObject& obj) {
        SelectionRegion r;
        r.id = obj["id"].toInt();
        r.name = obj["name"].toString();
        r.x0 = obj["x0"].toDouble();
        r.y0 = obj["y0"].toDouble();
        r.x1 = obj["x1"].toDouble(1.0);
        r.y1 = obj["y1"].toDouble(1.0);
        return r;
    }

    QString coordString() const {
        return QString("%1, %2, %3, %4")
            .arg(QString::number(x0, 'f', 3))
            .arg(QString::number(y0, 'f', 3))
            .arg(QString::number(x1, 'f', 3))
            .arg(QString::number(y1, 'f', 3));
    }
};

// ---------------------------------------------------------
// 选区管理器 - 管理自定义选区的增删改查和持久化
// 线程安全：所有公共方法均通过 QReadWriteLock 保护
// ---------------------------------------------------------
class SelectionRegionManager : public QObject
{
    Q_OBJECT
public:
    static SelectionRegionManager& instance() {
        static SelectionRegionManager s_instance;
        return s_instance;
    }

    // 获取配置文件路径
    static QString configPath() {
        return QCoreApplication::applicationDirPath() + "/keymap/regions.json";
    }

    // 获取配置文件所在目录
    static QString configDir() {
        return QCoreApplication::applicationDirPath() + "/keymap";
    }

    // 加载
    void load() {
        QWriteLocker locker(&m_lock);
        loadInternal();
    }

    // 保存
    bool save() const {
        QReadLocker locker(&m_lock);
        return saveInternal();
    }

    // 获取所有选区（拷贝，线程安全）
    QVector<SelectionRegion> regions() const {
        QReadLocker locker(&m_lock);
        return m_regions;
    }

    // 按 ID 查找选区，返回拷贝以避免指针悬挂
    bool findById(int id, SelectionRegion& out) const {
        QReadLocker locker(&m_lock);
        const SelectionRegion* r = findByIdInternal(id);
        if (r) { out = *r; return true; }
        return false;
    }

    // 按名字查找选区
    bool findByName(const QString& name, SelectionRegion& out) const {
        QReadLocker locker(&m_lock);
        const SelectionRegion* r = findByNameInternal(name);
        if (r) { out = *r; return true; }
        return false;
    }

    // 生成下一个可用 ID
    int nextId() const {
        QReadLocker locker(&m_lock);
        return nextIdInternal();
    }

    // 检查名字是否已存在
    bool nameExists(const QString& name, int excludeId = -1) const {
        QReadLocker locker(&m_lock);
        return nameExistsInternal(name, excludeId);
    }

    // 检查 ID 是否已存在
    bool idExists(int id) const {
        QReadLocker locker(&m_lock);
        return findByIdInternal(id) != nullptr;
    }

    // 添加选区
    bool add(const SelectionRegion& region) {
        QWriteLocker locker(&m_lock);
        if (findByIdInternal(region.id) || nameExistsInternal(region.name)) return false;
        m_regions.append(region);
        saveInternal();
        return true;
    }

    // 删除选区
    bool remove(int id) {
        QWriteLocker locker(&m_lock);
        for (int i = 0; i < m_regions.size(); ++i) {
            if (m_regions[i].id == id) {
                m_regions.removeAt(i);
                saveInternal();
                return true;
            }
        }
        return false;
    }

    // 重命名选区
    bool rename(int id, const QString& newName) {
        QWriteLocker locker(&m_lock);
        if (nameExistsInternal(newName, id)) return false;
        for (auto& r : m_regions) {
            if (r.id == id) {
                r.name = newName;
                saveInternal();
                return true;
            }
        }
        return false;
    }

    // 更新选区坐标
    bool updateCoords(int id, double x0, double y0, double x1, double y1) {
        QWriteLocker locker(&m_lock);
        for (auto& r : m_regions) {
            if (r.id == id) {
                r.x0 = x0;
                r.y0 = y0;
                r.x1 = x1;
                r.y1 = y1;
                saveInternal();
                return true;
            }
        }
        return false;
    }

    // 导入选区 (从 JSON 文件)
    int importFromFile(const QString& filePath) {
        QWriteLocker locker(&m_lock);
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) return 0;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (!doc.isArray()) return 0;
        int count = 0;
        const QJsonArray arr = doc.array();
        for (const QJsonValue& v : arr) {
            SelectionRegion r = SelectionRegion::fromJson(v.toObject());
            r.id = nextIdInternal();
            QString baseName = r.name;
            int suffix = 1;
            while (nameExistsInternal(r.name)) {
                r.name = QString("%1_%2").arg(baseName).arg(suffix++);
            }
            m_regions.append(r);
            ++count;
        }
        if (count > 0) saveInternal();
        return count;
    }

    // 反转排序
    void reverseOrder() {
        QWriteLocker locker(&m_lock);
        std::reverse(m_regions.begin(), m_regions.end());
        saveInternal();
    }

private:
    SelectionRegionManager() { loadInternal(); }
    ~SelectionRegionManager() = default;
    SelectionRegionManager(const SelectionRegionManager&) = delete;
    SelectionRegionManager& operator=(const SelectionRegionManager&) = delete;

    // ---- 内部无锁方法（调用方必须已持有锁）----

    void loadInternal() {
        m_regions.clear();
        QFile file(configPath());
        if (!file.open(QIODevice::ReadOnly)) return;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (!doc.isArray()) return;
        const QJsonArray arr = doc.array();
        for (const QJsonValue& v : arr) {
            m_regions.append(SelectionRegion::fromJson(v.toObject()));
        }
    }

    bool saveInternal() const {
        QDir dir;
        dir.mkpath(configDir());
        QFile file(configPath());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        QJsonArray arr;
        for (const SelectionRegion& r : m_regions) {
            arr.append(r.toJson());
        }
        file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
        file.close();
        return true;
    }

    const SelectionRegion* findByIdInternal(int id) const {
        for (const auto& r : m_regions) {
            if (r.id == id) return &r;
        }
        return nullptr;
    }

    const SelectionRegion* findByNameInternal(const QString& name) const {
        for (const auto& r : m_regions) {
            if (r.name == name) return &r;
        }
        return nullptr;
    }

    int nextIdInternal() const {
        int maxId = 0;
        for (const auto& r : m_regions) {
            if (r.id > maxId) maxId = r.id;
        }
        return maxId + 1;
    }

    bool nameExistsInternal(const QString& name, int excludeId = -1) const {
        for (const auto& r : m_regions) {
            if (r.name == name && r.id != excludeId) return true;
        }
        return false;
    }

    QVector<SelectionRegion> m_regions;
    mutable QReadWriteLock m_lock;
};

#endif // SELECTIONREGIONMANAGER_H
