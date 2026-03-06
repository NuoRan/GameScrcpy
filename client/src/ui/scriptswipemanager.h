#ifndef SCRIPTSWIPEMANAGER_H
#define SCRIPTSWIPEMANAGER_H

#include <QString>
#include <QVector>
#include <filesystem>
#include <fstream>
#include <shared_mutex>

#include "StringUtils.h"

#include <nlohmann/json.hpp>

// ---------------------------------------------------------
// 濠婃垵濮╃捄顖氱窞閺佺増宓佺紒鎾寸€?/ Script Swipe Data Structure
// 鐞涖劎銇氭禒搴ゆ崳閻愮懓鍩岀紒鍫㈠仯閻ㄥ嫪绔撮弶鈩冪拨閸斻劏鐭惧?
// ---------------------------------------------------------
struct ScriptSwipe
{
    int id = 0;             // 濠婃垵濮╃紓鏍у娇 (閸烆垯绔? / Swipe ID (unique)
    QString name;           // 婢跺洦鏁為崥宥呯摟 / Label name
    double x0 = 0.3;       // 鐠ч鍋?x (0.0~1.0) / Start x
    double y0 = 0.5;       // 鐠ч鍋?y (0.0~1.0) / Start y
    double x1 = 0.7;       // 缂佸牏鍋?x (0.0~1.0) / End x
    double y1 = 0.5;       // 缂佸牏鍋?y (0.0~1.0) / End y

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

    static ScriptSwipe fromJson(const nlohmann::json& j) {
        ScriptSwipe s;
        s.id = j.value("id", 0);
        s.name = QString::fromStdString(j.value("name", std::string()));
        s.x0 = j.value("x0", 0.3);
        s.y0 = j.value("y0", 0.5);
        s.x1 = j.value("x1", 0.7);
        s.y1 = j.value("y1", 0.5);
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
// 濠婃垵濮╃捄顖氱窞缁狅紕鎮婇崳?- 缁狅紕鎮婇懘姘拱濠婃垵濮╃捄顖氱窞閻ㄥ嫬顤冮崚鐘虫暭閺屻儱鎷伴幐浣风畽閸?
// 缁捐法鈻肩€瑰鍙忛敍姘閺堝鍙曢崗杈ㄦ煙濞夋洖娼庨柅姘崇箖 std::shared_mutex 娣囨繃濮?
// ---------------------------------------------------------
class ScriptSwipeManager
{
public:
    static ScriptSwipeManager& instance() {
        static ScriptSwipeManager s_instance;
        return s_instance;
    }

    static std::string configPath() {
        return strutil::appDirPath() + "/keymap/swipes.json";
    }

    static std::string configDir() {
        return strutil::appDirPath() + "/keymap";
    }

    void load() {
        std::unique_lock<std::shared_mutex> locker(m_lock);
        loadInternal();
    }

    bool save() const {
        std::shared_lock<std::shared_mutex> locker(m_lock);
        return saveInternal();
    }

    QVector<ScriptSwipe> swipes() const {
        std::shared_lock<std::shared_mutex> locker(m_lock);
        return m_swipes;
    }

    bool findById(int id, ScriptSwipe& out) const {
        std::shared_lock<std::shared_mutex> locker(m_lock);
        const ScriptSwipe* s = findByIdInternal(id);
        if (s) { out = *s; return true; }
        return false;
    }

    int nextId() const {
        std::shared_lock<std::shared_mutex> locker(m_lock);
        return nextIdInternal();
    }

    bool nameExists(const QString& name, int excludeId = -1) const {
        std::shared_lock<std::shared_mutex> locker(m_lock);
        return nameExistsInternal(name, excludeId);
    }

    bool add(const ScriptSwipe& swipe) {
        std::unique_lock<std::shared_mutex> locker(m_lock);
        if (findByIdInternal(swipe.id)) return false;
        m_swipes.append(swipe);
        saveInternal();
        return true;
    }

    bool remove(int id) {
        std::unique_lock<std::shared_mutex> locker(m_lock);
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
        std::unique_lock<std::shared_mutex> locker(m_lock);
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
        std::unique_lock<std::shared_mutex> locker(m_lock);
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
        std::ifstream file(configPath());
        if (!file.is_open()) return;
        std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        try {
            auto arr = nlohmann::json::parse(data);
            if (!arr.is_array()) return;
            for (const auto& v : arr) {
                m_swipes.append(ScriptSwipe::fromJson(v));
            }
        } catch (...) {}
    }

    bool saveInternal() const {
        std::filesystem::create_directories(configDir());
        std::ofstream file(configPath());
        if (!file.is_open()) return false;
        nlohmann::json arr = nlohmann::json::array();
        for (const ScriptSwipe& s : m_swipes) {
            arr.push_back(s.toJson());
        }
        std::string out = arr.dump(4);
        file << out;
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
    mutable std::shared_mutex m_lock;
};

#endif // SCRIPTSWIPEMANAGER_H
