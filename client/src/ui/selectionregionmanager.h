#ifndef SELECTIONREGIONMANAGER_H
#define SELECTIONREGIONMANAGER_H

#include <QString>
#include <QVector>
#include <QRectF>
#include <filesystem>
#include <fstream>
#include <shared_mutex>

#include "StringUtils.h"

#include <nlohmann/json.hpp>

// ---------------------------------------------------------
// 闁灏弫鐗堝祦缂佹挻鐎?/ Selection Region Data Structure
// ---------------------------------------------------------
struct SelectionRegion
{
    int id = 0;             // 闁灏紓鏍у娇 (閸烆垯绔? / Region ID (unique)
    QString name;           // 婢跺洦鏁為崥宥呯摟 / Label name
    double x0 = 0.0;       // 瀹革缚绗傜憴?x (0.0~1.0) / Top-left x
    double y0 = 0.0;       // 瀹革缚绗傜憴?y (0.0~1.0) / Top-left y
    double x1 = 1.0;       // 閸欏厖绗呯憴?x (0.0~1.0) / Bottom-right x
    double y1 = 1.0;       // 閸欏厖绗呯憴?y (0.0~1.0) / Bottom-right y

    nlohmann::json toJson() const {
        return {
            {"id", id},
            {"name", name.toStdString()},
            {"x0", x0},
            {"y0", y0},
            {"x1", x1},
            {"y1", y1}
        };
    }

    static SelectionRegion fromJson(const nlohmann::json& j) {
        SelectionRegion r;
        r.id = j.value("id", 0);
        r.name = QString::fromStdString(j.value("name", std::string()));
        r.x0 = j.value("x0", 0.0);
        r.y0 = j.value("y0", 0.0);
        r.x1 = j.value("x1", 1.0);
        r.y1 = j.value("y1", 1.0);
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
// 闁灏粻锛勬倞閸?- 缁狅紕鎮婇懛顏勭暰娑斿鈧灏惃鍕杻閸掔姵鏁奸弻銉ユ嫲閹镐椒绠欓崠?
// 缁捐法鈻肩€瑰鍙忛敍姘閺堝鍙曢崗杈ㄦ煙濞夋洖娼庨柅姘崇箖 std::shared_mutex 娣囨繃濮?
// ---------------------------------------------------------
class SelectionRegionManager
{
public:
    static SelectionRegionManager& instance() {
        static SelectionRegionManager s_instance;
        return s_instance;
    }

    // 閼惧嘲褰囬柊宥囩枂閺傚洣娆㈢捄顖氱窞
    static std::string configPath() {
        return strutil::appDirPath() + "/keymap/regions.json";
    }

    // 閼惧嘲褰囬柊宥囩枂閺傚洣娆㈤幍鈧崷銊ф窗瑜?
    static std::string configDir() {
        return strutil::appDirPath() + "/keymap";
    }

    // 閸旂姾娴?
    void load() {
        std::unique_lock<std::shared_mutex> locker(m_lock);
        loadInternal();
    }

    // 娣囨繂鐡?
    bool save() const {
        std::shared_lock<std::shared_mutex> locker(m_lock);
        return saveInternal();
    }

    // 閼惧嘲褰囬幍鈧張澶愨偓澶婂隘閿涘牊瀚圭拹婵撶礉缁捐法鈻肩€瑰鍙忛敍?
    QVector<SelectionRegion> regions() const {
        std::shared_lock<std::shared_mutex> locker(m_lock);
        return m_regions;
    }

    // 閹?ID 閺屻儲澹橀柅澶婂隘閿涘矁绻戦崶鐐村鐠愭繀浜掗柆鍨帳閹稿洭鎷￠幃顒佸瘯
    bool findById(int id, SelectionRegion& out) const {
        std::shared_lock<std::shared_mutex> locker(m_lock);
        const SelectionRegion* r = findByIdInternal(id);
        if (r) { out = *r; return true; }
        return false;
    }

    // 閹稿鎮曠€涙鐓￠幍楣冣偓澶婂隘
    bool findByName(const QString& name, SelectionRegion& out) const {
        std::shared_lock<std::shared_mutex> locker(m_lock);
        const SelectionRegion* r = findByNameInternal(name);
        if (r) { out = *r; return true; }
        return false;
    }

    // 閻㈢喐鍨氭稉瀣╃娑擃亜褰查悽?ID
    int nextId() const {
        std::shared_lock<std::shared_mutex> locker(m_lock);
        return nextIdInternal();
    }

    // 濡偓閺屻儱鎮曠€涙妲搁崥锕€鍑＄€涙ê婀?
    bool nameExists(const QString& name, int excludeId = -1) const {
        std::shared_lock<std::shared_mutex> locker(m_lock);
        return nameExistsInternal(name, excludeId);
    }

    // 濡偓閺?ID 閺勵垰鎯佸鎻掔摠閸?
    bool idExists(int id) const {
        std::shared_lock<std::shared_mutex> locker(m_lock);
        return findByIdInternal(id) != nullptr;
    }

    // 濞ｈ濮為柅澶婂隘
    bool add(const SelectionRegion& region) {
        std::unique_lock<std::shared_mutex> locker(m_lock);
        if (findByIdInternal(region.id) || nameExistsInternal(region.name)) return false;
        m_regions.append(region);
        saveInternal();
        return true;
    }

    // 閸掔娀娅庨柅澶婂隘
    bool remove(int id) {
        std::unique_lock<std::shared_mutex> locker(m_lock);
        for (int i = 0; i < m_regions.size(); ++i) {
            if (m_regions[i].id == id) {
                m_regions.removeAt(i);
                saveInternal();
                return true;
            }
        }
        return false;
    }

    // 闁插秴鎳￠崥宥夆偓澶婂隘
    bool rename(int id, const QString& newName) {
        std::unique_lock<std::shared_mutex> locker(m_lock);
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

    // 閺囧瓨鏌婇柅澶婂隘閸ф劖鐖?
    bool updateCoords(int id, double x0, double y0, double x1, double y1) {
        std::unique_lock<std::shared_mutex> locker(m_lock);
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

    // 鐎电厧鍙嗛柅澶婂隘 (娴?JSON 閺傚洣娆?
    int importFromFile(const QString& filePath) {
        std::unique_lock<std::shared_mutex> locker(m_lock);
        std::ifstream file(filePath.toStdString());
        if (!file.is_open()) return 0;
        std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        try {
            auto arr = nlohmann::json::parse(data);
            if (!arr.is_array()) return 0;
            int count = 0;
            for (const auto& v : arr) {
                SelectionRegion r = SelectionRegion::fromJson(v);
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
        } catch (...) {
            return 0;
        }
    }

    // 閸欏秷娴嗛幒鎺戠碍
    void reverseOrder() {
        std::unique_lock<std::shared_mutex> locker(m_lock);
        std::reverse(m_regions.begin(), m_regions.end());
        saveInternal();
    }

private:
    SelectionRegionManager() { loadInternal(); }
    ~SelectionRegionManager() = default;
    SelectionRegionManager(const SelectionRegionManager&) = delete;
    SelectionRegionManager& operator=(const SelectionRegionManager&) = delete;

    // ---- 閸愬懘鍎撮弮鐘绘敚閺傝纭堕敍鍫ｇ殶閻劍鏌熻箛鍛淬€忓鍙夊瘮閺堝鏀ｉ敍?---

    void loadInternal() {
        m_regions.clear();
        std::ifstream file(configPath());
        if (!file.is_open()) return;
        std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        try {
            auto arr = nlohmann::json::parse(data);
            if (!arr.is_array()) return;
            for (const auto& v : arr) {
                m_regions.append(SelectionRegion::fromJson(v));
            }
        } catch (...) {}
    }

    bool saveInternal() const {
        std::filesystem::create_directories(configDir());
        std::ofstream file(configPath());
        if (!file.is_open()) return false;
        nlohmann::json arr = nlohmann::json::array();
        for (const SelectionRegion& r : m_regions) {
            arr.push_back(r.toJson());
        }
        std::string out = arr.dump(4);
        file << out;
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
    mutable std::shared_mutex m_lock;
};

#endif // SELECTIONREGIONMANAGER_H
