#pragma once

#include "gs/ext/Diagnostics.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace gs::ext::test {

inline std::filesystem::path pathOf(std::string_view text)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

class Scratch
{
public:
    Scratch()
        : m_root(std::filesystem::temp_directory_path() /
                 ("gs_ext_" + std::to_string(std::random_device{}()) + std::to_string(std::random_device{}())))
    {
        std::filesystem::create_directories(m_root);
    }

    ~Scratch()
    {
        std::error_code error;
        std::filesystem::remove_all(m_root, error);
    }

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    std::filesystem::path operator/(std::string_view name) const { return m_root / pathOf(name); }

private:
    std::filesystem::path m_root;
};

inline void writeFile(const std::filesystem::path& path, std::string_view data)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
}

inline std::string readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

inline bool reports(const std::vector<Diagnostic>& diagnostics, Code code)
{
    return std::ranges::any_of(diagnostics, [&](const Diagnostic& diagnostic) { return diagnostic.code == code; });
}

inline bool reports(const std::vector<Diagnostic>& diagnostics, Code code, std::string_view path)
{
    return std::ranges::any_of(diagnostics, [&](const Diagnostic& diagnostic) {
        return diagnostic.code == code && diagnostic.path == path;
    });
}

inline std::string listed(const std::vector<Diagnostic>& diagnostics)
{
    std::string text;
    for (const Diagnostic& diagnostic : diagnostics) {
        text += std::string(codeName(diagnostic.code)) + " " + diagnostic.path + "; ";
    }
    return text;
}

}
