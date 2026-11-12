#include "gs/ext/Archive.h"

#include <gs/platform/AtomicFile.h>
#include <miniz.h>

#include <algorithm>
#include <array>
#include <ctime>
#include <fstream>
#include <functional>
#include <iterator>
#include <set>
#include <system_error>
#include <utility>

namespace gs::ext {

namespace {

constexpr std::size_t kMaxPathLength = 260;
constexpr std::size_t kMaxNameLength = 255;
constexpr std::uint16_t kUnixHost = 3;
constexpr std::uint32_t kFileTypeMask = 0170000;
constexpr std::uint32_t kSymbolicLink = 0120000;
constexpr std::time_t kPackedTime = 315532800;
constexpr std::string_view kForbidden = "\\:*?\"<>|";
constexpr std::array<std::string_view, 32> kReservedNames{
    "con",  "prn",  "aux",  "nul",  "conin$", "conout$", "com0", "com1", "com2", "com3", "com4", "com5",
    "com6", "com7", "com8", "com9", "com\xC2\xB9", "com\xC2\xB2", "com\xC2\xB3", "lpt0", "lpt1", "lpt2",
    "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9", "lpt\xC2\xB9", "lpt\xC2\xB2", "lpt\xC2\xB3"};

std::string lowered(std::string_view text)
{
    std::string result(text);
    std::ranges::transform(result, result.begin(),
                           [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    return result;
}

std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::filesystem::path pathOf(std::string_view text)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

bool reservedSegment(std::string_view segment)
{
    std::string_view stem = segment.substr(0, segment.find('.'));
    while (stem.ends_with(' ')) {
        stem.remove_suffix(1);
    }
    return std::ranges::find(kReservedNames, lowered(stem)) != kReservedNames.end();
}

bool isLink(const mz_zip_archive_file_stat& stat)
{
    return (stat.m_version_made_by >> 8) == kUnixHost &&
           ((stat.m_external_attr >> 16) & kFileTypeMask) == kSymbolicLink;
}

bool within(const std::filesystem::path& root, const std::filesystem::path& target)
{
    const std::filesystem::path relative = target.lexically_relative(root);
    return !relative.empty() && relative.is_relative() && relative != "." && *relative.begin() != "..";
}

void report(std::vector<Diagnostic>& problems, Code code, std::string path, std::string detail = {})
{
    problems.push_back({Severity::Error, code, std::move(path), std::move(detail)});
}

class Zip
{
public:
    Zip() = default;
    Zip(const Zip&) = delete;
    Zip& operator=(const Zip&) = delete;

    ~Zip()
    {
        if (m_reading) {
            mz_zip_reader_end(&m_archive);
        } else if (m_writing) {
            mz_zip_writer_end(&m_archive);
        }
    }

    bool read(const std::filesystem::path& file)
    {
        m_reading = mz_zip_reader_init_file(&m_archive, utf8(file).c_str(), 0) != 0;
        return m_reading;
    }

    bool write(const std::filesystem::path& file)
    {
        m_writing = mz_zip_writer_init_file(&m_archive, utf8(file).c_str(), 0) != 0;
        return m_writing;
    }

    bool finishWriting()
    {
        const bool finalized = mz_zip_writer_finalize_archive(&m_archive) != 0;
        const bool ended = mz_zip_writer_end(&m_archive) != 0;
        m_writing = false;
        return finalized && ended;
    }

    [[nodiscard]] mz_zip_archive* get() noexcept { return &m_archive; }

private:
    mz_zip_archive m_archive{};
    bool m_reading = false;
    bool m_writing = false;
};

struct Sink {
    std::ofstream* out = nullptr;
    const ArchiveLimits* limits = nullptr;
    std::uint64_t* total = nullptr;
    std::uint64_t written = 0;
    bool fileTooLarge = false;
    bool tooMuch = false;
};

std::size_t writeChunk(void* opaque, mz_uint64, const void* data, std::size_t size)
{
    Sink& sink = *static_cast<Sink*>(opaque);
    sink.fileTooLarge = sink.written + size > sink.limits->maxFileBytes;
    sink.tooMuch = *sink.total + size > sink.limits->maxTotalBytes;
    if (sink.fileTooLarge || sink.tooMuch) {
        return 0;
    }
    sink.out->write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!*sink.out) {
        return 0;
    }
    sink.written += size;
    *sink.total += size;
    return size;
}

struct Collected {
    std::string* text = nullptr;
    std::uint64_t maxBytes = 0;
};

std::size_t collectChunk(void* opaque, mz_uint64, const void* data, std::size_t size)
{
    Collected& collected = *static_cast<Collected*>(opaque);
    if (collected.text->size() + size > collected.maxBytes) {
        return 0;
    }
    collected.text->append(static_cast<const char*>(data), size);
    return size;
}

}

bool safeArchivePath(std::string_view path)
{
    if (path.ends_with('/')) {
        path.remove_suffix(1);
    }
    if (path.empty() || path.size() > kMaxPathLength || path.front() == '/') {
        return false;
    }
    const bool printable = std::ranges::none_of(path, [](char c) {
        const auto code = static_cast<unsigned char>(c);
        return code < 0x20 || code == 0x7F || kForbidden.find(c) != std::string_view::npos;
    });
    if (!printable) {
        return false;
    }
    std::size_t start = 0;
    while (true) {
        const std::size_t end = path.find('/', start);
        const std::string_view segment =
            path.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (segment.empty() || segment.size() > kMaxNameLength || segment.back() == '.' || segment.back() == ' ' ||
            reservedSegment(segment)) {
            return false;
        }
        if (end == std::string_view::npos) {
            return true;
        }
        start = end + 1;
    }
}

ArchiveListing inspectArchive(const std::filesystem::path& archive, const ArchiveLimits& limits)
{
    ArchiveListing listing;
    std::error_code error;
    const std::uintmax_t packed = std::filesystem::file_size(archive, error);
    Zip zip;
    if (error || !zip.read(archive)) {
        report(listing.problems, Code::ArchiveUnreadable, "$", utf8(archive));
        return listing;
    }
    const mz_uint count = mz_zip_reader_get_num_files(zip.get());
    if (count > limits.maxEntries) {
        report(listing.problems, Code::ArchiveTooManyEntries, "$", std::to_string(count));
        return listing;
    }
    std::set<std::string, std::less<>> names;
    std::set<std::string, std::less<>> folders;
    std::uint64_t unpacked = 0;
    for (mz_uint index = 0; index < count; ++index) {
        mz_zip_archive_file_stat stat{};
        if (mz_zip_reader_file_stat(zip.get(), index, &stat) == 0) {
            report(listing.problems, Code::ArchiveCorrupt, "$", "entry " + std::to_string(index));
            continue;
        }
        std::string name = stat.m_filename;
        if (!safeArchivePath(name)) {
            report(listing.problems, Code::ArchiveUnsafePath, name);
            continue;
        }
        if (isLink(stat)) {
            report(listing.problems, Code::ArchiveLink, name);
            continue;
        }
        if (stat.m_is_encrypted != 0) {
            report(listing.problems, Code::ArchiveEncrypted, name);
            continue;
        }
        if (stat.m_is_supported == 0) {
            report(listing.problems, Code::ArchiveCorrupt, name, "unsupported compression");
            continue;
        }
        if (name.ends_with('/')) {
            name.pop_back();
        }
        std::string key = lowered(name);
        for (std::size_t slash = key.find('/'); slash != std::string::npos; slash = key.find('/', slash + 1)) {
            folders.insert(key.substr(0, slash));
        }
        if (!names.insert(key).second) {
            report(listing.problems, Code::ArchiveNameClash, name);
            continue;
        }
        if (stat.m_is_directory != 0) {
            folders.insert(std::move(key));
            continue;
        }
        if (stat.m_uncomp_size > limits.maxFileBytes) {
            report(listing.problems, Code::ArchiveEntryTooLarge, name, std::to_string(stat.m_uncomp_size));
            continue;
        }
        unpacked += stat.m_uncomp_size;
        listing.files.push_back({std::move(name), stat.m_uncomp_size});
    }
    for (const ArchiveEntry& file : listing.files) {
        if (folders.contains(lowered(file.path))) {
            report(listing.problems, Code::ArchiveNameClash, file.path);
        }
    }
    if (unpacked > limits.maxTotalBytes || (packed > 0 && unpacked / packed > limits.maxRatio)) {
        report(listing.problems, Code::ArchiveTooLarge, "$", std::to_string(unpacked));
    }
    std::ranges::sort(listing.files, {}, &ArchiveEntry::path);
    return listing;
}

std::vector<Diagnostic> extractArchive(const std::filesystem::path& archive, const std::filesystem::path& destination,
                                       const ArchiveLimits& limits)
{
    ArchiveListing listing = inspectArchive(archive, limits);
    if (!listing.ok()) {
        return std::move(listing.problems);
    }
    std::vector<Diagnostic> problems;
    Zip zip;
    if (!zip.read(archive)) {
        report(problems, Code::ArchiveUnreadable, "$", utf8(archive));
        return problems;
    }
    std::error_code error;
    const std::filesystem::path root = std::filesystem::absolute(destination, error).lexically_normal();
    if (!error) {
        std::filesystem::create_directories(root, error);
    }
    if (error) {
        report(problems, Code::ArchiveWriteFailed, "$", error.message());
        return problems;
    }
    std::uint64_t total = 0;
    for (const ArchiveEntry& entry : listing.files) {
        const std::filesystem::path target = (root / pathOf(entry.path)).lexically_normal();
        if (!within(root, target)) {
            report(problems, Code::ArchiveUnsafePath, entry.path);
            break;
        }
        std::filesystem::create_directories(target.parent_path(), error);
        const int index = mz_zip_reader_locate_file(zip.get(), entry.path.c_str(), nullptr, MZ_ZIP_FLAG_CASE_SENSITIVE);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (error || index < 0 || !out) {
            report(problems, Code::ArchiveWriteFailed, entry.path, error ? error.message() : std::string());
            break;
        }
        Sink sink{.out = &out, .limits = &limits, .total = &total};
        const bool extracted =
            mz_zip_reader_extract_to_callback(zip.get(), static_cast<mz_uint>(index), writeChunk, &sink, 0) != 0;
        out.close();
        if (!extracted || !out) {
            const Code code = sink.fileTooLarge ? Code::ArchiveEntryTooLarge
                              : sink.tooMuch    ? Code::ArchiveTooLarge
                              : extracted       ? Code::ArchiveWriteFailed
                                                : Code::ArchiveCorrupt;
            report(problems, code, entry.path, mz_zip_get_error_string(mz_zip_get_last_error(zip.get())));
            break;
        }
    }
    if (!problems.empty()) {
        std::filesystem::remove_all(root, error);
    }
    return problems;
}

std::optional<std::string> readArchiveFile(const std::filesystem::path& archive, std::string_view path,
                                           std::uint64_t maxBytes)
{
    if (!safeArchivePath(path) || path.ends_with('/')) {
        return std::nullopt;
    }
    Zip zip;
    if (!zip.read(archive)) {
        return std::nullopt;
    }
    const std::string name(path);
    const int index = mz_zip_reader_locate_file(zip.get(), name.c_str(), nullptr, MZ_ZIP_FLAG_CASE_SENSITIVE);
    mz_zip_archive_file_stat stat{};
    if (index < 0 || mz_zip_reader_file_stat(zip.get(), static_cast<mz_uint>(index), &stat) == 0 ||
        stat.m_is_directory != 0 || stat.m_is_encrypted != 0 || isLink(stat) || stat.m_uncomp_size > maxBytes) {
        return std::nullopt;
    }
    std::string text;
    text.reserve(static_cast<std::size_t>(stat.m_uncomp_size));
    Collected collected{.text = &text, .maxBytes = maxBytes};
    if (mz_zip_reader_extract_to_callback(zip.get(), static_cast<mz_uint>(index), collectChunk, &collected, 0) == 0) {
        return std::nullopt;
    }
    return text;
}

ArchiveListing inspectDirectory(const std::filesystem::path& directory, const ArchiveLimits& limits,
                                const std::vector<std::string>& skipped)
{
    ArchiveListing listing;
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        report(listing.problems, Code::ArchiveUnreadable, "$", utf8(directory));
        return listing;
    }
    std::uint64_t total = 0;
    for (auto at = std::filesystem::recursive_directory_iterator(directory, error);
         !error && at != std::filesystem::recursive_directory_iterator(); at.increment(error)) {
        const std::string leaf = utf8(at->path().filename());
        if (leaf.starts_with('.') || std::ranges::find(skipped, leaf) != skipped.end()) {
            at.disable_recursion_pending();
            continue;
        }
        const std::string name = utf8(at->path().lexically_relative(directory));
        if (at->is_symlink(error)) {
            report(listing.problems, Code::ArchiveLink, name);
            continue;
        }
        if (!at->is_regular_file(error)) {
            continue;
        }
        if (!safeArchivePath(name)) {
            report(listing.problems, Code::ArchiveUnsafePath, name);
            continue;
        }
        const std::uint64_t size = at->file_size(error);
        if (error) {
            break;
        }
        if (size > limits.maxFileBytes) {
            report(listing.problems, Code::ArchiveEntryTooLarge, name, std::to_string(size));
            continue;
        }
        total += size;
        listing.files.push_back({name, size});
        if (listing.files.size() > limits.maxEntries) {
            report(listing.problems, Code::ArchiveTooManyEntries, "$", std::to_string(listing.files.size()));
            break;
        }
    }
    if (error) {
        report(listing.problems, Code::ArchiveUnreadable, "$", error.message());
    }
    if (total > limits.maxTotalBytes) {
        report(listing.problems, Code::ArchiveTooLarge, "$", std::to_string(total));
    }
    std::ranges::sort(listing.files, {}, &ArchiveEntry::path);
    return listing;
}

std::vector<Diagnostic> packDirectory(const std::filesystem::path& directory, const std::filesystem::path& archive,
                                      const ArchiveLimits& limits, const std::vector<std::string>& skipped)
{
    return packDirectory(directory, archive, limits, skipped, {});
}

std::vector<Diagnostic> packDirectory(const std::filesystem::path& directory, const std::filesystem::path& archive,
                                      const ArchiveLimits& limits, const std::vector<std::string>& skipped,
                                      const std::vector<PackedFile>& added)
{
    ArchiveListing listing = inspectDirectory(directory, limits, skipped);
    if (!listing.ok()) {
        return std::move(listing.problems);
    }
    std::vector<Diagnostic> problems;
    std::vector<std::pair<std::string, const PackedFile*>> entries;
    for (const ArchiveEntry& entry : listing.files) {
        if (std::ranges::none_of(added, [&entry](const PackedFile& file) { return file.path == entry.path; })) {
            entries.emplace_back(entry.path, nullptr);
        }
    }
    for (const PackedFile& file : added) {
        if (!safeArchivePath(file.path)) {
            report(problems, Code::ArchiveUnsafePath, file.path);
            return problems;
        }
        entries.emplace_back(file.path, &file);
    }
    std::ranges::sort(entries, {}, &std::pair<std::string, const PackedFile*>::first);
    std::filesystem::path temporary = archive;
    temporary += ".tmp";
    Zip zip;
    if (!zip.write(temporary)) {
        report(problems, Code::ArchiveWriteFailed, "$", utf8(archive));
        return problems;
    }
    std::uint64_t total = 0;
    std::time_t stamp = kPackedTime;
    for (const auto& [name, given] : entries) {
        std::string data;
        if (given != nullptr) {
            data = given->data;
        } else {
            std::ifstream in(directory / pathOf(name), std::ios::binary);
            data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            if (!in.is_open() || in.bad()) {
                report(problems, Code::ArchiveUnreadable, name);
                break;
            }
        }
        if (data.size() > limits.maxFileBytes) {
            report(problems, Code::ArchiveEntryTooLarge, name, std::to_string(data.size()));
            break;
        }
        total += data.size();
        if (total > limits.maxTotalBytes) {
            report(problems, Code::ArchiveTooLarge, "$", std::to_string(total));
            break;
        }
        if (mz_zip_writer_add_mem_ex_v2(zip.get(), name.c_str(), data.data(), data.size(), nullptr, 0, MZ_DEFAULT_LEVEL,
                                        0, 0, &stamp, nullptr, 0, nullptr, 0) == 0) {
            report(problems, Code::ArchiveWriteFailed, name);
            break;
        }
    }
    const bool finished = zip.finishWriting();
    if (problems.empty() && !finished) {
        report(problems, Code::ArchiveWriteFailed, "$", utf8(archive));
    }
    if (problems.empty()) {
        if (const std::error_code replaced = platform::replaceFile(temporary, archive)) {
            report(problems, Code::ArchiveWriteFailed, "$", replaced.message());
        }
    }
    if (!problems.empty()) {
        std::error_code error;
        std::filesystem::remove(temporary, error);
    }
    return problems;
}

}
