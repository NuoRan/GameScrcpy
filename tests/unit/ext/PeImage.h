#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gs::ext::test {

inline constexpr std::uint64_t kPeSectionRva = 0x1000;
inline constexpr std::size_t kPeSectionFile = 0x200;

struct PeModule {
    std::string name;
    std::vector<std::string> functions;
};

struct PeLayout {
    bool wide = true;
    std::vector<PeModule> imports;
    std::vector<PeModule> delayed;
    bool virtualDelay = false;
    std::uint16_t machine = 0;
    std::vector<std::string> exports;
};

class PeBytes
{
public:
    [[nodiscard]] std::size_t size() const noexcept { return m_data.size(); }

    std::size_t reserve(std::size_t count)
    {
        const std::size_t at = m_data.size();
        m_data.resize(at + count, '\0');
        return at;
    }

    void put(std::size_t at, std::uint64_t value, std::size_t width)
    {
        for (std::size_t k = 0; k < width; ++k) {
            m_data[at + k] = static_cast<char>((value >> (8 * k)) & 0xff);
        }
    }

    std::size_t text(std::string_view value)
    {
        const std::size_t at = reserve(value.size() + 1);
        m_data.replace(at, value.size(), value);
        return at;
    }

    void align(std::size_t step)
    {
        while (m_data.size() % step != 0) {
            m_data.push_back('\0');
        }
    }

    [[nodiscard]] std::string& data() noexcept { return m_data; }

private:
    std::string m_data;
};

inline std::string buildPe(const PeLayout& layout)
{
    const std::uint64_t base = layout.wide ? 0x180000000ULL : 0x10000000ULL;
    const std::size_t width = layout.wide ? 8 : 4;
    const std::uint64_t ordinalFlag = std::uint64_t{1} << (layout.wide ? 63 : 31);
    const auto rva = [](std::size_t offset) { return kPeSectionRva + offset; };

    PeBytes section;
    const std::size_t importTable = layout.imports.empty() ? 0 : section.reserve(20 * (layout.imports.size() + 1));
    const std::size_t delayTable = layout.delayed.empty() ? 0 : section.reserve(32 * (layout.delayed.size() + 1));
    const std::size_t exportTable = layout.exports.empty() ? 0 : section.reserve(40);
    const auto thunkOf = [&](const std::string& function, bool virtualAddress) -> std::uint64_t {
        if (function.starts_with('#')) {
            return std::stoull(function.substr(1)) | ordinalFlag;
        }
        section.align(2);
        const std::size_t hint = section.reserve(2);
        section.text(function);
        return virtualAddress ? base + rva(hint) : rva(hint);
    };
    for (std::size_t m = 0; m < layout.imports.size(); ++m) {
        const PeModule& module = layout.imports[m];
        const std::size_t lookup = section.reserve(width * (module.functions.size() + 1));
        const std::size_t address = section.reserve(width * (module.functions.size() + 1));
        for (std::size_t f = 0; f < module.functions.size(); ++f) {
            const std::uint64_t thunk = thunkOf(module.functions[f], false);
            section.put(lookup + width * f, thunk, width);
            section.put(address + width * f, thunk, width);
        }
        const std::size_t name = section.text(module.name);
        const std::size_t descriptor = importTable + 20 * m;
        section.put(descriptor, rva(lookup), 4);
        section.put(descriptor + 12, rva(name), 4);
        section.put(descriptor + 16, rva(address), 4);
    }
    for (std::size_t m = 0; m < layout.delayed.size(); ++m) {
        const PeModule& module = layout.delayed[m];
        const std::size_t handle = section.reserve(8);
        const std::size_t names = section.reserve(width * (module.functions.size() + 1));
        const std::size_t address = section.reserve(width * (module.functions.size() + 1));
        for (std::size_t f = 0; f < module.functions.size(); ++f) {
            const std::uint64_t thunk = thunkOf(module.functions[f], layout.virtualDelay);
            section.put(names + width * f, thunk, width);
            section.put(address + width * f, base + rva(handle), width);
        }
        const std::size_t name = section.text(module.name);
        const auto field = [&](std::size_t offset) { return layout.virtualDelay ? base + rva(offset) : rva(offset); };
        const std::size_t descriptor = delayTable + 32 * m;
        section.put(descriptor, layout.virtualDelay ? 0 : 1, 4);
        section.put(descriptor + 4, field(name), 4);
        section.put(descriptor + 8, field(handle), 4);
        section.put(descriptor + 12, field(address), 4);
        section.put(descriptor + 16, field(names), 4);
    }
    if (!layout.exports.empty()) {
        const std::size_t count = layout.exports.size();
        const std::size_t functions = section.reserve(4 * count);
        const std::size_t names = section.reserve(4 * count);
        const std::size_t ordinals = section.reserve(2 * count);
        for (std::size_t index = 0; index < count; ++index) {
            section.put(functions + 4 * index, rva(functions), 4);
            section.put(names + 4 * index, rva(section.text(layout.exports[index])), 4);
            section.put(ordinals + 2 * index, index, 2);
        }
        const std::size_t library = section.text("fixture.dll");
        section.put(exportTable + 12, rva(library), 4);
        section.put(exportTable + 16, 1, 4);
        section.put(exportTable + 20, count, 4);
        section.put(exportTable + 24, count, 4);
        section.put(exportTable + 28, rva(functions), 4);
        section.put(exportTable + 32, rva(names), 4);
        section.put(exportTable + 36, rva(ordinals), 4);
    }
    const std::size_t used = section.size();
    section.align(0x200);

    PeBytes image;
    image.reserve(kPeSectionFile);
    image.data()[0] = 'M';
    image.data()[1] = 'Z';
    image.put(0x3C, 0x40, 4);
    image.data()[0x40] = 'P';
    image.data()[0x41] = 'E';
    const std::size_t coff = 0x44;
    const std::size_t optionalSize = layout.wide ? 240 : 224;
    image.put(coff, layout.machine != 0 ? layout.machine : (layout.wide ? 0x8664 : 0x014C), 2);
    image.put(coff + 2, 1, 2);
    image.put(coff + 16, optionalSize, 2);
    image.put(coff + 18, 0x2022, 2);
    const std::size_t optional = coff + 20;
    image.put(optional, layout.wide ? 0x20B : 0x10B, 2);
    if (layout.wide) {
        image.put(optional + 24, base, 8);
    } else {
        image.put(optional + 28, base, 4);
    }
    image.put(optional + 32, 0x1000, 4);
    image.put(optional + 36, 0x200, 4);
    image.put(optional + 40, 6, 2);
    image.put(optional + 48, 6, 2);
    image.put(optional + 56, kPeSectionRva + ((used + 0xFFF) & ~std::size_t{0xFFF}), 4);
    image.put(optional + 60, kPeSectionFile, 4);
    image.put(optional + 68, 2, 2);
    image.put(optional + (layout.wide ? 108 : 92), 16, 4);
    const std::size_t directories = optional + (layout.wide ? 112 : 96);
    if (!layout.exports.empty()) {
        image.put(directories, rva(exportTable), 4);
        image.put(directories + 4, 40, 4);
    }
    if (!layout.imports.empty()) {
        image.put(directories + 8, rva(importTable), 4);
        image.put(directories + 12, 20 * (layout.imports.size() + 1), 4);
    }
    if (!layout.delayed.empty()) {
        image.put(directories + 8 * 13, rva(delayTable), 4);
        image.put(directories + 8 * 13 + 4, 32 * (layout.delayed.size() + 1), 4);
    }
    const std::size_t header = optional + optionalSize;
    image.data().replace(header, 6, ".idata");
    image.put(header + 8, used, 4);
    image.put(header + 12, kPeSectionRva, 4);
    image.put(header + 16, section.size(), 4);
    image.put(header + 20, kPeSectionFile, 4);
    image.put(header + 36, 0xC0000040, 4);
    return image.data() + section.data();
}

}
