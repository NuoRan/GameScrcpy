#ifndef SCRIPTBUTTONMANAGER_H
#define SCRIPTBUTTONMANAGER_H

#include <QString>
#include <QVector>
#include <filesystem>
#include <fstream>
#include <shared_mutex>

#include "StringUtils.h"

#include <nlohmann/json.hpp>

// ---------------------------------------------------------
// 閾忔碍瀚欓幐澶愭尦閺佺増宓佺紒鎾寸€?/ Script Button Data Structure
// 鐞涖劎銇氶悽濠氭桨娑撳﹦娈戞稉鈧稉顏勬祼鐎规矮缍呯純顔惧仯
// ---------------------------------------------------------
struct ScriptButton
{
    int id = 0;             // 閹稿鎸崇紓鏍у娇 (閸烆垯绔? / Button ID (unique)
    QString name;           // 婢跺洦鏁為崥宥呯摟 / Label name
    double x = 0.5;         // x 閸ф劖鐖?(0.0~1.0) / Normalized x
    double y = 0.5;         // y 閸ф劖鐖?(0.0~1.0) / Normalized y

    nlohmann::json toJson() const {
        return {
            {"id", id},
            {"name", name.toStdString()},
            {"x", x},
            {"y", y}
        };
    }

    static ScriptButton fromJson(const nlohmann::json& j) {
        ScriptButton b;
        b.id = j.value("id", 0);
        b.name = QString::fromStdString(j.value("name", std::string()));
        b.x = j.value("x", 0.5);
        b.y = j.value("y", 0.5);
        return b;
    }

    QString coordString() const {
        return QString("%1, %2")
            .arg(QString::number(x, 'f', 4))
            .arg(QString::number(y, 'f', 4));
    }
};

// ---------------------------------------------------------
// 閾忔碍瀚欓幐澶愭尦缁狅紕鎮婇崳?- 缁狅紕鎮婇懘姘拱閾忔碍瀚欓幐澶愭尦閻ㄥ嫬顤冮崚鐘虫暭閺屻儱鎷伴幐浣风畽閸?
// 缁捐法鈻肩€瑰鍙忛敍姘閺堝鍙曢崗杈ㄦ煙濞夋洖娼庨柅姘崇箖 std::shared_mutex 娣囨繃濮?
// ---------------------------------------------------------
class ScriptButtonManager
{
public:
    static ScriptButtonManager& instance() {
        static ScriptButtonManager s_instance;
        return s_instance;
    }

    static std::string configPath() {
        return strutil::appDirPath() + "/keymap/buttons.json";
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

    QVector<ScriptButton> buttons() const {
        std::shared_lock<std::shared_mutex> locker(m_lock);
        return m_buttons;
    }

    bool findById(int id, ScriptButton& out) const {
        std::shared_lock<std::shared_mutex> locker(m_lock);
        const ScriptButton* b = findByIdInternal(id);
        if (b) { out = *b; return true; }
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

    bool add(const ScriptButton& button) {
        std::unique_lock<std::shared_mutex> locker(m_lock);
        if (findByIdInternal(button.id)) return false;
        m_buttons.append(button);
        saveInternal();
        return true;
    }

    bool remove(int id) {
        std::unique_lock<std::shared_mutex> locker(m_lock);
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
        std::unique_lock<std::shared_mutex> locker(m_lock);
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
        std::unique_lock<std::shared_mutex> locker(m_lock);
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
        std::ifstream file(configPath());
        if (!file.is_open()) return;
        std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        try {
            auto arr = nlohmann::json::parse(data);
            if (!arr.is_array()) return;
            for (const auto& v : arr) {
                m_buttons.append(ScriptButton::fromJson(v));
            }
        } catch (...) {}
    }

    bool saveInternal() const {
        std::filesystem::create_directories(configDir());
        std::ofstream file(configPath());
        if (!file.is_open()) return false;
        nlohmann::json arr = nlohmann::json::array();
        for (const ScriptButton& b : m_buttons) {
            arr.push_back(b.toJson());
        }
        std::string out = arr.dump(4);
        file << out;
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
    mutable std::shared_mutex m_lock;
};

#endif // SCRIPTBUTTONMANAGER_H
