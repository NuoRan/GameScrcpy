#include "TestFiles.h"

#include "gs/ext/Archive.h"

#include <doctest/doctest.h>
#include <miniz.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace gs::ext;
using namespace gs::ext::test;
namespace fs = std::filesystem;

namespace {

struct Entry {
    std::string name;
    std::string data;
    std::uint16_t host = 0;
    std::uint32_t attributes = 0;
    std::uint16_t flags = 0;
    bool deflate = false;
    std::optional<std::uint32_t> declaredSize;
};

void put16(std::string& out, std::uint32_t value)
{
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
}

void put32(std::string& out, std::uint32_t value)
{
    put16(out, value & 0xFFFF);
    put16(out, value >> 16);
}

std::string deflated(std::string_view data)
{
    std::size_t size = 0;
    const auto flags = static_cast<int>(
        tdefl_create_comp_flags_from_zip_params(MZ_DEFAULT_LEVEL, -MZ_DEFAULT_WINDOW_BITS, MZ_DEFAULT_STRATEGY));
    void* packed = tdefl_compress_mem_to_heap(data.data(), data.size(), &size, flags);
    std::string result(static_cast<const char*>(packed), size);
    mz_free(packed);
    return result;
}

std::string zipOf(const std::vector<Entry>& entries)
{
    std::string body;
    std::string directory;
    for (const Entry& entry : entries) {
        const std::string payload = entry.deflate ? deflated(entry.data) : entry.data;
        const auto crc = static_cast<std::uint32_t>(
            mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(entry.data.data()), entry.data.size()));
        const std::uint32_t size = entry.declaredSize.value_or(static_cast<std::uint32_t>(entry.data.size()));
        const std::uint32_t method = entry.deflate ? 8 : 0;
        const auto offset = static_cast<std::uint32_t>(body.size());
        const auto nameSize = static_cast<std::uint32_t>(entry.name.size());
        const auto payloadSize = static_cast<std::uint32_t>(payload.size());

        put32(body, 0x04034B50);
        put16(body, 20);
        put16(body, entry.flags);
        put16(body, method);
        put16(body, 0);
        put16(body, 0x21);
        put32(body, crc);
        put32(body, payloadSize);
        put32(body, size);
        put16(body, nameSize);
        put16(body, 0);
        body += entry.name;
        body += payload;

        put32(directory, 0x02014B50);
        put16(directory, (std::uint32_t{entry.host} << 8) | 20);
        put16(directory, 20);
        put16(directory, entry.flags);
        put16(directory, method);
        put16(directory, 0);
        put16(directory, 0x21);
        put32(directory, crc);
        put32(directory, payloadSize);
        put32(directory, size);
        put16(directory, nameSize);
        put16(directory, 0);
        put16(directory, 0);
        put16(directory, 0);
        put16(directory, 0);
        put32(directory, entry.attributes);
        put32(directory, offset);
        directory += entry.name;
    }
    std::string zip = body + directory;
    put32(zip, 0x06054B50);
    put16(zip, 0);
    put16(zip, 0);
    put16(zip, static_cast<std::uint32_t>(entries.size()));
    put16(zip, static_cast<std::uint32_t>(entries.size()));
    put32(zip, static_cast<std::uint32_t>(directory.size()));
    put32(zip, static_cast<std::uint32_t>(body.size()));
    put16(zip, 0);
    return zip;
}

}

TEST_CASE("a packed directory lists, reads and unpacks to the same files")
{
    Scratch scratch;
    const std::string manifest = R"({"id":"alice.crt"})";
    const std::string shader = std::string(5000, ' ') + "float4 main() : SV_Target { return 0; }";
    writeFile(scratch / "source/manifest.json", manifest);
    writeFile(scratch / "source/shaders/crt.hlsl", shader);
    writeFile(scratch / "source/说明/读我.txt", "hello");
    const fs::path archive = scratch / "crt.gsext";
    REQUIRE(packDirectory(scratch / "source", archive).empty());

    const ArchiveListing listing = inspectArchive(archive);
    INFO(listed(listing.problems));
    CHECK(listing.ok());
    CHECK(listing.files == std::vector<ArchiveEntry>{{"manifest.json", manifest.size()},
                                                     {"shaders/crt.hlsl", shader.size()},
                                                     {"说明/读我.txt", 5}});

    CHECK(readArchiveFile(archive, "manifest.json", 1024) == manifest);
    CHECK(readArchiveFile(archive, "说明/读我.txt", 5) == "hello");
    CHECK_FALSE(readArchiveFile(archive, "manifest.json", manifest.size() - 1).has_value());
    CHECK_FALSE(readArchiveFile(archive, "Manifest.json", 1024).has_value());
    CHECK_FALSE(readArchiveFile(archive, "shaders", 1024).has_value());
    CHECK_FALSE(readArchiveFile(archive, "../manifest.json", 1024).has_value());

    REQUIRE(extractArchive(archive, scratch / "unpacked").empty());
    CHECK(readFile(scratch / "unpacked/manifest.json") == manifest);
    CHECK(readFile(scratch / "unpacked/shaders/crt.hlsl") == shader);
    CHECK(readFile(scratch / "unpacked/说明/读我.txt") == "hello");
}

TEST_CASE("packing over an existing file replaces it only with a complete package")
{
    Scratch scratch;
    writeFile(scratch / "source/a.txt", "alpha");
    const fs::path archive = scratch / "out.gsext";
    writeFile(archive, "previous bytes");
    REQUIRE(packDirectory(scratch / "source", archive).empty());
    CHECK(inspectArchive(archive).ok());
    CHECK_FALSE(fs::exists(scratch / "out.gsext.tmp"));

    const std::string before = readFile(archive);
    CHECK(reports(packDirectory(scratch / "source", archive, {.maxFileBytes = 2}), Code::ArchiveEntryTooLarge));
    CHECK(readFile(archive) == before);
    CHECK_FALSE(fs::exists(scratch / "out.gsext.tmp"));
}

TEST_CASE("packing the same files again gives the same bytes")
{
    Scratch scratch;
    writeFile(scratch / "source/manifest.json", "{}");
    writeFile(scratch / "source/b/c.txt", "c");
    REQUIRE(packDirectory(scratch / "source", scratch / "first.gsext").empty());
    const fs::path touched = scratch / "source/manifest.json";
    fs::last_write_time(touched, fs::last_write_time(touched) - std::chrono::hours(48));
    REQUIRE(packDirectory(scratch / "source", scratch / "second.gsext").empty());
    CHECK(readFile(scratch / "first.gsext") == readFile(scratch / "second.gsext"));
}

TEST_CASE("archive paths must stay inside the package and be valid file names on Windows")
{
    for (const char* path :
         {"manifest.json", "shaders/crt.hlsl", "shaders/", "con-tent.txt", "console/a.txt", "a.b.c", "说明/读我.txt"}) {
        CAPTURE(path);
        CHECK(safeArchivePath(path));
    }
    for (const char* path : {"",      "/",        "/etc/passwd",  "../x",     "a/../../x", "./x",  "a/./b",
                             "a//b",  "C:/x",     "c:x",          "a\\b",     "con",       "CON.txt",
                             "dir/Nul.json", "com1", "LPT9.dat", "COM\xC2\xB9.txt", "con .txt", "dir/Aux  .json",
                             "CONIN$", "conout$.log", "a.", "a/b ", "tab\there",
                             "a*b",   "a?b",      "a\"b",         "a<b",      "a>b",       "a|b",  "\x7F"}) {
        CAPTURE(path);
        CHECK_FALSE(safeArchivePath(path));
    }
    CHECK(safeArchivePath(std::string(255, 'a')));
    CHECK_FALSE(safeArchivePath(std::string(256, 'a')));
    CHECK(safeArchivePath(std::string(200, 'a') + "/" + std::string(59, 'b')));
    CHECK_FALSE(safeArchivePath(std::string(200, 'a') + "/" + std::string(60, 'b')));
}

TEST_CASE("an archive that escapes, links, is encrypted or has clashing names is refused and leaves nothing behind")
{
    struct Case {
        const char* label;
        std::vector<Entry> entries;
        Code code;
    };
    const std::vector<Case> cases{
        {"traversal", {{.name = "../evil.txt", .data = "x"}}, Code::ArchiveUnsafePath},
        {"absolute", {{.name = "/evil.txt", .data = "x"}}, Code::ArchiveUnsafePath},
        {"backslash", {{.name = "..\\evil.txt", .data = "x"}}, Code::ArchiveUnsafePath},
        {"drive", {{.name = "C:/evil.txt", .data = "x"}}, Code::ArchiveUnsafePath},
        {"reserved name", {{.name = "aux.json", .data = "x"}}, Code::ArchiveUnsafePath},
        {"symbolic link", {{.name = "shaders", .data = "../../..", .host = 3, .attributes = 0120777U << 16}},
         Code::ArchiveLink},
        {"encrypted", {{.name = "manifest.json", .data = "x", .flags = 1}}, Code::ArchiveEncrypted},
        {"same name", {{.name = "Readme.txt", .data = "a"}, {.name = "README.txt", .data = "b"}},
         Code::ArchiveNameClash},
        {"file and folder", {{.name = "data", .data = "a"}, {.name = "Data/x.txt", .data = "b"}},
         Code::ArchiveNameClash},
    };
    for (const Case& item : cases) {
        CAPTURE(item.label);
        Scratch scratch;
        const fs::path archive = scratch / "bad.gsext";
        writeFile(archive, zipOf(item.entries));
        const ArchiveListing listing = inspectArchive(archive);
        INFO(listed(listing.problems));
        CHECK(reports(listing.problems, item.code));
        CHECK(reports(extractArchive(archive, scratch / "out/staging"), item.code));
        CHECK_FALSE(fs::exists(scratch / "out/staging"));
        CHECK_FALSE(fs::exists(scratch / "evil.txt"));
    }
}

TEST_CASE("an entry that unpacks to more than it declares is stopped and leaves nothing behind")
{
    Scratch scratch;
    const fs::path archive = scratch / "bomb.gsext";
    writeFile(archive, zipOf({{.name = "big.bin",
                               .data = std::string(std::size_t{8} << 20, '\0'),
                               .deflate = true,
                               .declaredSize = 1000}}));
    const ArchiveListing listing = inspectArchive(archive);
    INFO(listed(listing.problems));
    CHECK(listing.ok());
    CHECK(listing.files == std::vector<ArchiveEntry>{{"big.bin", 1000}});

    CHECK(reports(extractArchive(archive, scratch / "out"), Code::ArchiveCorrupt));
    CHECK_FALSE(fs::exists(scratch / "out"));
    CHECK(reports(extractArchive(archive, scratch / "out", {.maxFileBytes = 2000}), Code::ArchiveEntryTooLarge));
    CHECK_FALSE(fs::exists(scratch / "out"));
    CHECK_FALSE(readArchiveFile(archive, "big.bin", 4096).has_value());
}

TEST_CASE("limits on entries, file size, total size and compression ratio hold when packing and unpacking")
{
    Scratch scratch;
    for (const char* name : {"source/a.txt", "source/b.txt", "source/c.txt"}) {
        writeFile(scratch / name, std::string(10, 'x'));
    }
    const fs::path archive = scratch / "three.gsext";
    REQUIRE(packDirectory(scratch / "source", archive).empty());
    CHECK(inspectArchive(archive).ok());
    CHECK(reports(inspectArchive(archive, {.maxEntries = 2}).problems, Code::ArchiveTooManyEntries));
    CHECK(reports(inspectArchive(archive, {.maxFileBytes = 9}).problems, Code::ArchiveEntryTooLarge));
    CHECK(reports(inspectArchive(archive, {.maxTotalBytes = 25}).problems, Code::ArchiveTooLarge));
    CHECK(reports(extractArchive(archive, scratch / "out", {.maxTotalBytes = 25}), Code::ArchiveTooLarge));
    CHECK_FALSE(fs::exists(scratch / "out"));

    const fs::path limited = scratch / "limited.gsext";
    CHECK(reports(packDirectory(scratch / "source", limited, {.maxEntries = 2}), Code::ArchiveTooManyEntries));
    CHECK(reports(packDirectory(scratch / "source", limited, {.maxFileBytes = 9}), Code::ArchiveEntryTooLarge));
    CHECK(reports(packDirectory(scratch / "source", limited, {.maxTotalBytes = 25}), Code::ArchiveTooLarge));
    CHECK_FALSE(fs::exists(limited));

    writeFile(scratch / "zeros/zeros.bin", std::string(std::size_t{4} << 20, '\0'));
    const fs::path zeros = scratch / "zeros.gsext";
    REQUIRE(packDirectory(scratch / "zeros", zeros).empty());
    CHECK(reports(inspectArchive(zeros).problems, Code::ArchiveTooLarge));
    CHECK(inspectArchive(zeros, {.maxRatio = 2000}).ok());
}

TEST_CASE("a file that is not an archive, or is missing, is reported as unreadable")
{
    Scratch scratch;
    const fs::path notes = scratch / "notes.gsext";
    writeFile(notes, "not a zip at all");
    CHECK(reports(inspectArchive(notes).problems, Code::ArchiveUnreadable));
    CHECK(reports(inspectArchive(scratch / "missing.gsext").problems, Code::ArchiveUnreadable));
    CHECK(reports(extractArchive(notes, scratch / "out"), Code::ArchiveUnreadable));
    CHECK_FALSE(fs::exists(scratch / "out"));
    CHECK_FALSE(readArchiveFile(notes, "manifest.json", 1024).has_value());
    CHECK(reports(packDirectory(scratch / "nowhere", scratch / "nowhere.gsext"), Code::ArchiveUnreadable));
    CHECK_FALSE(fs::exists(scratch / "nowhere.gsext"));
}

TEST_CASE("archive diagnostics have stable names and a fix hint")
{
    CHECK(codeName(Code::ArchiveUnsafePath) == "archive-unsafe-path");
    CHECK(codeName(Code::ArchiveNameClash) == "archive-name-clash");
    for (const Code code : {Code::ArchiveUnreadable, Code::ArchiveTooManyEntries, Code::ArchiveEntryTooLarge,
                            Code::ArchiveTooLarge, Code::ArchiveUnsafePath, Code::ArchiveLink, Code::ArchiveEncrypted,
                            Code::ArchiveNameClash, Code::ArchiveCorrupt, Code::ArchiveWriteFailed}) {
        CAPTURE(codeName(code));
        CHECK(codeName(code).starts_with("archive-"));
        CHECK_FALSE(fixHint(code).empty());
    }
}
