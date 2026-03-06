// IniConfig.cpp — Pure C++ INI file reader/writer implementation
// UTF-8, atomic write, thread-safe.

#include "IniConfig.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

// Trim whitespace from both ends
static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// ----- Construction -----

IniConfig::IniConfig(const std::string& filePath) {
    load(filePath);
}

IniConfig::IniConfig(const std::wstring& filePath) {
    load(filePath);
}

bool IniConfig::load(const std::string& filePath) {
    m_filePath = std::filesystem::u8path(filePath);
    return load(m_filePath.wstring());
}

bool IniConfig::load(const std::wstring& filePath) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_filePath = filePath;
    m_data.clear();
    m_dirty = false;

    std::ifstream file(m_filePath, std::ios::binary);
    if (!file.is_open()) {
        return false; // File doesn't exist yet, that's OK
    }

    std::string currentSection = "General"; // Default section (compatible with QSettings)
    std::string line;

    // Skip UTF-8 BOM if present
    char bom[3];
    file.read(bom, 3);
    if (!(bom[0] == '\xEF' && bom[1] == '\xBB' && bom[2] == '\xBF')) {
        file.seekg(0);
    }

    while (std::getline(file, line)) {
        // Remove \r if present (Windows line endings)
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        std::string trimmed = trim(line);

        // Skip empty lines and comments
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
            continue;
        }

        // Section header
        if (trimmed[0] == '[') {
            size_t end = trimmed.find(']');
            if (end != std::string::npos) {
                currentSection = trim(trimmed.substr(1, end - 1));
            }
            continue;
        }

        // Key = Value
        size_t eqPos = trimmed.find('=');
        if (eqPos != std::string::npos) {
            std::string key = trim(trimmed.substr(0, eqPos));
            std::string value = trim(trimmed.substr(eqPos + 1));
            if (!key.empty()) {
                m_data[currentSection][key] = value;
            }
        }
    }

    return true;
}

bool IniConfig::sync() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (m_filePath.empty()) return false;

    // Ensure parent directory exists
    auto parentDir = m_filePath.parent_path();
    if (!parentDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parentDir, ec);
    }

    // Write to temporary file first
    auto tmpPath = m_filePath;
    tmpPath += L".tmp";

    {
        std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return false;

        for (auto& [section, keys] : m_data) {
            if (keys.empty()) continue;

            file << "[" << section << "]\n";
            for (auto& [key, value] : keys) {
                file << key << "=" << value << "\n";
            }
            file << "\n";
        }
        file.flush();
        if (!file.good()) return false;
    }

    // Atomic rename
    std::error_code ec;
    std::filesystem::rename(tmpPath, m_filePath, ec);
    if (ec) {
        // Fallback: copy then delete (cross-device rename may fail)
        std::filesystem::copy_file(tmpPath, m_filePath,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(tmpPath, ec);
    }

    m_dirty = false;
    return true;
}

// ----- Key splitting -----

void IniConfig::splitKey(const std::string& fullKey, std::string& section, std::string& key) {
    size_t slashPos = fullKey.find('/');
    if (slashPos != std::string::npos) {
        section = fullKey.substr(0, slashPos);
        key = fullKey.substr(slashPos + 1);
        // Handle nested keys like "device/serial/nickName" -> section="device", key="serial/nickName"
        // But QSettings uses the first part as section, rest as key with %2F encoding
        // For simplicity, we keep section as first part and key as the rest
    } else {
        section = "General";
        key = fullKey;
    }
}

// ----- String access -----

std::string IniConfig::getString(const std::string& fullKey, const std::string& defaultValue) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::string section, key;
    splitKey(fullKey, section, key);

    auto sit = m_data.find(section);
    if (sit == m_data.end()) return defaultValue;

    auto kit = sit->second.find(key);
    if (kit == sit->second.end()) return defaultValue;

    return kit->second;
}

int IniConfig::getInt(const std::string& fullKey, int defaultValue) const {
    std::string val = getString(fullKey, "");
    if (val.empty()) return defaultValue;
    try {
        return std::stoi(val);
    } catch (...) {
        return defaultValue;
    }
}

bool IniConfig::getBool(const std::string& fullKey, bool defaultValue) const {
    std::string val = getString(fullKey, "");
    if (val.empty()) return defaultValue;
    // Handle various boolean representations
    if (val == "true" || val == "1" || val == "yes" || val == "on") return true;
    if (val == "false" || val == "0" || val == "no" || val == "off") return false;
    return defaultValue;
}

double IniConfig::getDouble(const std::string& fullKey, double defaultValue) const {
    std::string val = getString(fullKey, "");
    if (val.empty()) return defaultValue;
    try {
        return std::stod(val);
    } catch (...) {
        return defaultValue;
    }
}

unsigned int IniConfig::getUInt(const std::string& fullKey, unsigned int defaultValue) const {
    std::string val = getString(fullKey, "");
    if (val.empty()) return defaultValue;
    try {
        unsigned long result = std::stoul(val);
        return static_cast<unsigned int>(result);
    } catch (...) {
        return defaultValue;
    }
}

// ----- Set values -----

void IniConfig::setString(const std::string& fullKey, const std::string& value) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::string section, key;
    splitKey(fullKey, section, key);
    m_data[section][key] = value;
    m_dirty = true;
}

void IniConfig::setInt(const std::string& fullKey, int value) {
    setString(fullKey, std::to_string(value));
}

void IniConfig::setBool(const std::string& fullKey, bool value) {
    setString(fullKey, value ? "true" : "false");
}

void IniConfig::setDouble(const std::string& fullKey, double value) {
    setString(fullKey, std::to_string(value));
}

void IniConfig::setUInt(const std::string& fullKey, unsigned int value) {
    setString(fullKey, std::to_string(value));
}

// ----- Query -----

bool IniConfig::contains(const std::string& fullKey) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::string section, key;
    splitKey(fullKey, section, key);

    auto sit = m_data.find(section);
    if (sit == m_data.end()) return false;
    return sit->second.count(key) > 0;
}

void IniConfig::remove(const std::string& fullKey) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::string section, key;
    splitKey(fullKey, section, key);

    auto sit = m_data.find(section);
    if (sit != m_data.end()) {
        // If key is empty, remove entire section
        if (key.empty()) {
            m_data.erase(sit);
        } else {
            sit->second.erase(key);
            // Remove empty section
            if (sit->second.empty()) {
                m_data.erase(sit);
            }
        }
        m_dirty = true;
    }
}

void IniConfig::clear() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_data.clear();
    m_dirty = true;
}

std::vector<std::string> IniConfig::allKeys() const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<std::string> result;
    for (auto& [section, keys] : m_data) {
        for (auto& [key, val] : keys) {
            result.push_back(section + "/" + key);
        }
    }
    return result;
}

std::vector<std::string> IniConfig::keysInSection(const std::string& section) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<std::string> result;
    auto sit = m_data.find(section);
    if (sit != m_data.end()) {
        for (auto& [key, val] : sit->second) {
            result.push_back(key);
        }
    }
    return result;
}

// ----- Rect support -----

IniConfig::Rect IniConfig::getRect(const std::string& fullKey) const {
    std::string val = getString(fullKey, "");
    if (val.empty()) return {};

    // Parse QSettings-style "@Rect(x y w h)" format
    if (val.find("@Rect(") == 0) {
        Rect r;
        if (sscanf_s(val.c_str(), "@Rect(%d %d %d %d)", &r.x, &r.y, &r.w, &r.h) == 4) {
            return r;
        }
    }

    // Try simple "x,y,w,h" format
    Rect r;
    if (sscanf_s(val.c_str(), "%d,%d,%d,%d", &r.x, &r.y, &r.w, &r.h) == 4) {
        return r;
    }

    return {};
}

void IniConfig::setRect(const std::string& fullKey, int x, int y, int w, int h) {
    // Use QSettings-compatible format
    char buf[64];
    snprintf(buf, sizeof(buf), "@Rect(%d %d %d %d)", x, y, w, h);
    setString(fullKey, buf);
}

// ----- String list support -----

std::vector<std::string> IniConfig::getStringList(const std::string& fullKey) const {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::vector<std::string> result;

    // QSettings stores lists as key\1\size=N, key\1=val, key\2=val, etc.
    // We support a simpler approach: numbered sub-keys
    std::string section, key;
    splitKey(fullKey, section, key);

    auto sit = m_data.find(section);
    if (sit == m_data.end()) return result;

    // Check for "key\size" (QSettings format)
    auto sizeKey = key + "\\size";
    auto sizeIt = sit->second.find(sizeKey);
    if (sizeIt != sit->second.end()) {
        int count = 0;
        try { count = std::stoi(sizeIt->second); } catch (...) {}
        for (int i = 1; i <= count; ++i) {
            auto itemKey = key + "\\" + std::to_string(i);
            auto it = sit->second.find(itemKey);
            if (it != sit->second.end()) {
                result.push_back(it->second);
            }
        }
        return result;
    }

    // Fallback: single value
    auto it = sit->second.find(key);
    if (it != sit->second.end() && !it->second.empty()) {
        result.push_back(it->second);
    }

    return result;
}

void IniConfig::setStringList(const std::string& fullKey, const std::vector<std::string>& list) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::string section, key;
    splitKey(fullKey, section, key);

    // Remove old entries
    auto sit = m_data.find(section);
    if (sit != m_data.end()) {
        // Remove any existing numbered keys
        std::vector<std::string> toRemove;
        for (auto& [k, v] : sit->second) {
            if (k.find(key + "\\") == 0) {
                toRemove.push_back(k);
            }
        }
        for (auto& k : toRemove) {
            sit->second.erase(k);
        }
    }

    // Write new entries
    m_data[section][key + "\\size"] = std::to_string(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
        m_data[section][key + "\\" + std::to_string(i + 1)] = list[i];
    }
    m_dirty = true;
}
