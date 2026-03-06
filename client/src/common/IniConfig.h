#pragma once
// IniConfig.h — Pure C++ INI file reader/writer
// Replaces QSettings for INI format files.
// UTF-8 encoding, atomic write (write to .tmp then rename).
// Thread-safe with std::recursive_mutex.
// C++17.

#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

class IniConfig {
public:
    IniConfig() = default;
    explicit IniConfig(const std::string& filePath);
    explicit IniConfig(const std::wstring& filePath);
    ~IniConfig() = default;

    // Non-copyable but movable
    IniConfig(const IniConfig&) = delete;
    IniConfig& operator=(const IniConfig&) = delete;
    IniConfig(IniConfig&&) noexcept = default;
    IniConfig& operator=(IniConfig&&) noexcept = default;

    // Load from file (called automatically by constructor)
    bool load(const std::string& filePath);
    bool load(const std::wstring& filePath);

    // Save to file (atomic: writes to .tmp then renames)
    bool sync();

    // ----- Value access -----

    // Get a string value. Returns defaultValue if key not found.
    // Key format: "section/key" (e.g. "common/language")
    std::string getString(const std::string& key, const std::string& defaultValue = "") const;

    // Get typed values
    int getInt(const std::string& key, int defaultValue = 0) const;
    bool getBool(const std::string& key, bool defaultValue = false) const;
    double getDouble(const std::string& key, double defaultValue = 0.0) const;
    unsigned int getUInt(const std::string& key, unsigned int defaultValue = 0) const;

    // Set values
    void setString(const std::string& key, const std::string& value);
    void setInt(const std::string& key, int value);
    void setBool(const std::string& key, bool value);
    void setDouble(const std::string& key, double value);
    void setUInt(const std::string& key, unsigned int value);

    // ----- Query -----

    bool contains(const std::string& key) const;
    void remove(const std::string& key);
    void clear();

    // Get all keys (format: "section/key")
    std::vector<std::string> allKeys() const;

    // Get all keys within a section
    std::vector<std::string> keysInSection(const std::string& section) const;

    // ----- Rect support (for compatibility with QSettings storing QRect) -----
    // Stored as "section/key" = "@Rect(x y w h)" or 4 sub-keys
    struct Rect {
        int x = 0, y = 0, w = 0, h = 0;
        bool isValid() const { return w > 0 && h > 0; }
    };
    Rect getRect(const std::string& key) const;
    void setRect(const std::string& key, int x, int y, int w, int h);

    // ----- String list support -----
    std::vector<std::string> getStringList(const std::string& key) const;
    void setStringList(const std::string& key, const std::vector<std::string>& list);

    // Get the file path
    const std::filesystem::path& filePath() const { return m_filePath; }

private:
    // Parse "section/key" into section and key parts
    static void splitKey(const std::string& fullKey, std::string& section, std::string& key);

    // Internal storage: section -> (key -> value)
    // Using ordered map to preserve section/key ordering in output
    using Section = std::map<std::string, std::string>;
    std::map<std::string, Section> m_data;

    std::filesystem::path m_filePath;
    mutable std::recursive_mutex m_mutex;
    bool m_dirty = false;
};
