#include "ImageSection.h"

namespace gs::exthost::detail {

ImageSection::~ImageSection()
{
    if (m_view != nullptr) {
        UnmapViewOfFile(m_view);
    }
    if (m_mapping != nullptr) {
        CloseHandle(m_mapping);
    }
}

std::shared_ptr<ImageSection> ImageSection::create(std::uint64_t bytes)
{
    if (bytes == 0 || bytes > kMostImageSectionBytes) {
        return nullptr;
    }
    const std::uint64_t total = (bytes + kImageSectionStep - 1) / kImageSectionStep * kImageSectionStep;
    auto made = std::make_shared<ImageSection>();
    made->m_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, static_cast<DWORD>(total >> 32),
                                         static_cast<DWORD>(total & 0xFFFFFFFFU), nullptr);
    if (made->m_mapping == nullptr) {
        return nullptr;
    }
    made->m_view = MapViewOfFile(made->m_mapping, FILE_MAP_WRITE, 0, 0, static_cast<SIZE_T>(total));
    if (made->m_view == nullptr) {
        return nullptr;
    }
    made->m_bytes = total;
    return made;
}

std::shared_ptr<ImageSection> ImageSection::open(HANDLE mapping, std::uint64_t bytes)
{
    auto made = std::make_shared<ImageSection>();
    made->m_mapping = mapping;
    if (mapping == nullptr || bytes == 0 || bytes > kMostImageSectionBytes) {
        return nullptr;
    }
    made->m_view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, static_cast<SIZE_T>(bytes));
    if (made->m_view == nullptr) {
        return nullptr;
    }
    made->m_bytes = bytes;
    return made;
}

}
