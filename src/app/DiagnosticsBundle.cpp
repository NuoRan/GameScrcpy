#include "DiagnosticsBundle.h"

#include "AppInfo.h"
#include "AppSettings.h"
#include "CrashReport.h"

#include <gs/platform/AtomicFile.h>

#include <miniz.h>

#include <QAbstractItemModel>
#include <QByteArray>
#include <QClipboard>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QHash>
#include <QJSEngine>
#include <QSysInfo>
#include <QUrl>
#include <QVariant>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <format>
#include <fstream>
#include <iterator>
#include <new>
#include <system_error>
#include <utility>

namespace gs::app {

namespace diagnostics {

namespace {

constexpr std::size_t kNone = std::string_view::npos;
constexpr std::size_t kLongestUserName = 256;
constexpr std::string_view kTrailingPunctuation = ":,;.)";
constexpr std::array<std::string_view, 24> kFileExtensions{
    "qml", "js",   "mjs", "cpp", "cc",   "cxx",  "hpp",  "hxx", "inl", "java",  "kt", "py",
    "json", "md", "txt", "log", "hlsl", "glsl", "frag", "vert", "xml", "ini", "cmake", "ts"};

enum class Skip : std::uint8_t { Nothing, Number, Word };

struct Anchor {
    std::string_view phrase;
    Skip skip = Skip::Nothing;
    std::string_view then;
    bool serialOnly = true;
};

constexpr std::array<Anchor, 11> kAnchors{{
    {"video of ", Skip::Nothing, {}, true},
    {"audio of ", Skip::Nothing, {}, true},
    {"real latency of ", Skip::Nothing, {}, true},
    {"overlay for ", Skip::Nothing, {}, true},
    {"is cut to ", Skip::Number, " for ", true},
    {"locked for ", Skip::Nothing, {}, false},
    {"released for ", Skip::Nothing, {}, false},
    {"mirror window for ", Skip::Nothing, {}, false},
    {"foreground app on ", Skip::Nothing, {}, false},
    {"filters on ", Skip::Nothing, {}, false},
    {"start panel closed for ", Skip::Word, " on ", false},
}};

struct Replacement {
    std::size_t at = 0;
    std::size_t length = 0;
    std::string with;
};

bool isDigit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

bool isAlpha(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isAlnum(char c) noexcept
{
    return isDigit(c) || isAlpha(c);
}

bool isWordChar(char c) noexcept
{
    return isAlnum(c) || c == '_';
}

bool isHex(char c) noexcept
{
    return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

bool isSpace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool isSeparator(char c) noexcept
{
    return c == '\\' || c == '/';
}

bool endsUserName(char c) noexcept
{
    constexpr std::string_view kForbidden = "\\/\":*?<>|;,=[]+'";
    return static_cast<unsigned char>(c) < 0x20 || kForbidden.find(c) != kNone;
}

char lower(char c) noexcept
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string lowered(std::string_view text)
{
    std::string result(text);
    std::ranges::transform(result, result.begin(), lower);
    return result;
}

std::string_view trimmed(std::string_view text) noexcept
{
    while (!text.empty() && isSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

std::string utf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::string applied(std::string_view text, std::vector<Replacement> replacements)
{
    if (replacements.empty()) {
        return std::string(text);
    }
    std::ranges::stable_sort(replacements, [](const Replacement& a, const Replacement& b) {
        return a.at != b.at ? a.at < b.at : a.length > b.length;
    });
    std::string result;
    result.reserve(text.size());
    std::size_t done = 0;
    for (const Replacement& each : replacements) {
        if (each.at < done) {
            continue;
        }
        result.append(text.substr(done, each.at - done));
        result.append(each.with);
        done = each.at + each.length;
    }
    result.append(text.substr(done));
    return result;
}

std::vector<std::string> distinct(const std::vector<std::string>& values, std::size_t shortest)
{
    std::vector<std::string> result;
    std::vector<std::string> seen;
    for (const std::string& value : values) {
        if (value.size() < shortest) {
            continue;
        }
        std::string key = lowered(value);
        if (std::ranges::find(seen, key) != seen.end()) {
            continue;
        }
        seen.push_back(std::move(key));
        result.push_back(value);
    }
    return result;
}

void findAll(std::string_view lowerText, std::string_view lowerNeedle, const std::string& token,
             std::vector<Replacement>& found)
{
    if (lowerNeedle.empty()) {
        return;
    }
    for (std::size_t at = lowerText.find(lowerNeedle); at != kNone;
         at = lowerText.find(lowerNeedle, at + lowerNeedle.size())) {
        found.push_back({at, lowerNeedle.size(), token});
    }
}

std::vector<std::string> clipboardPieces(const std::vector<std::string>& clipboard)
{
    std::vector<std::string> pieces;
    for (const std::string& text : clipboard) {
        pieces.emplace_back(trimmed(text));
        std::string_view rest = text;
        while (!rest.empty()) {
            const std::size_t end = rest.find('\n');
            pieces.emplace_back(trimmed(rest.substr(0, end)));
            rest = end == kNone ? std::string_view() : rest.substr(end + 1);
        }
    }
    return distinct(pieces, kShortestClipboardLine);
}

std::string_view tokenAt(std::string_view text, std::size_t at)
{
    std::size_t end = at;
    while (end < text.size() && !isSpace(text[end])) {
        ++end;
    }
    std::string_view token = text.substr(at, end - at);
    while (!token.empty() && kTrailingPunctuation.find(token.back()) != kNone) {
        token.remove_suffix(1);
    }
    return token;
}

std::optional<std::string_view> androidIdAt(std::string_view text, std::size_t at)
{
    constexpr std::size_t kShortestId = 8;
    if (at > 0 && isWordChar(text[at - 1])) {
        return std::nullopt;
    }
    std::size_t end = at + 4;
    while (end < text.size() && isHex(text[end])) {
        ++end;
    }
    if (end - at - 4 < kShortestId || (end < text.size() && isWordChar(text[end]))) {
        return std::nullopt;
    }
    return text.substr(at, end - at);
}

std::optional<std::pair<std::string_view, std::string_view>> adbServiceAt(std::string_view text, std::size_t at)
{
    if (at > 0 && (isWordChar(text[at - 1]) || text[at - 1] == '.' || text[at - 1] == '-')) {
        return std::nullopt;
    }
    const std::size_t serial = at + 4;
    std::size_t end = serial;
    while (end < text.size() && isAlnum(text[end])) {
        ++end;
    }
    if (end == serial || end >= text.size() || text[end] != '-') {
        return std::nullopt;
    }
    const std::size_t suffix = end + 1;
    std::size_t last = suffix;
    while (last < text.size() && isAlnum(text[last])) {
        ++last;
    }
    if (last == suffix) {
        return std::nullopt;
    }
    return std::pair(text.substr(at, last - at), text.substr(serial, end - serial));
}

std::optional<std::size_t> ipv4End(std::string_view text, std::size_t at) noexcept
{
    std::size_t p = at;
    for (int part = 0; part < 4; ++part) {
        if (part > 0) {
            if (p >= text.size() || text[p] != '.') {
                return std::nullopt;
            }
            ++p;
        }
        const std::size_t start = p;
        unsigned value = 0;
        while (p < text.size() && isDigit(text[p]) && p - start < 3) {
            value = value * 10 + static_cast<unsigned>(text[p] - '0');
            ++p;
        }
        if (p == start || value > 255 || (p < text.size() && isDigit(text[p]))) {
            return std::nullopt;
        }
    }
    if (p < text.size() &&
        (isWordChar(text[p]) || (text[p] == '.' && p + 1 < text.size() && isDigit(text[p + 1])))) {
        return std::nullopt;
    }
    return p;
}

std::size_t portEnd(std::string_view text, std::size_t at) noexcept
{
    if (at >= text.size() || text[at] != ':') {
        return at;
    }
    std::size_t p = at + 1;
    unsigned value = 0;
    while (p < text.size() && isDigit(text[p]) && p - at <= 5) {
        value = value * 10 + static_cast<unsigned>(text[p] - '0');
        ++p;
    }
    if (p == at + 1 || value > 65535 || (p < text.size() && isWordChar(text[p]))) {
        return at;
    }
    return p;
}

bool ipv4Exact(std::string_view text) noexcept
{
    const std::optional<std::size_t> end = ipv4End(text, 0);
    return end && *end == text.size();
}

bool validIpv6(std::string_view text) noexcept
{
    std::size_t groups = 0;
    bool compressed = false;
    std::size_t p = 0;
    if (text.starts_with("::")) {
        compressed = true;
        p = 2;
    }
    while (p < text.size()) {
        const std::size_t start = p;
        while (p < text.size() && isHex(text[p])) {
            ++p;
        }
        if (p < text.size() && text[p] == '.') {
            if (!ipv4Exact(text.substr(start))) {
                return false;
            }
            groups += 2;
            break;
        }
        if (p == start || p - start > 4) {
            return false;
        }
        ++groups;
        if (p == text.size()) {
            break;
        }
        ++p;
        if (p < text.size() && text[p] == ':') {
            if (compressed) {
                return false;
            }
            compressed = true;
            ++p;
        } else if (p == text.size()) {
            return false;
        }
    }
    return groups > 0 && (compressed ? groups <= 7 : groups == 8);
}

void addIpv4(std::string_view text, std::vector<Replacement>& found)
{
    for (std::size_t at = 0; at < text.size(); ++at) {
        if (!isDigit(text[at]) || (at > 0 && (isWordChar(text[at - 1]) || text[at - 1] == '.'))) {
            continue;
        }
        const std::optional<std::size_t> end = ipv4End(text, at);
        if (!end) {
            continue;
        }
        const std::size_t port = portEnd(text, *end);
        found.push_back({at, port - at, port > *end ? "<ip>:<port>" : "<ip>"});
        at = port - 1;
    }
}

void addIpv6(std::string_view text, std::vector<Replacement>& found)
{
    std::size_t at = 0;
    while (at < text.size()) {
        if (!isHex(text[at]) && text[at] != ':') {
            ++at;
            continue;
        }
        std::size_t end = at;
        while (end < text.size() && (isHex(text[end]) || text[end] == ':' || text[end] == '.')) {
            ++end;
        }
        std::size_t stop = end;
        while (stop > at && text[stop - 1] == '.') {
            --stop;
        }
        if (stop - at >= 2 && text[stop - 1] == ':' && text[stop - 2] != ':') {
            --stop;
        }
        const std::string_view candidate = text.substr(at, stop - at);
        const bool bounded = (at == 0 || !isWordChar(text[at - 1])) && (end >= text.size() || !isWordChar(text[end]));
        if (!bounded || std::ranges::count(candidate, ':') < 2 || !validIpv6(candidate)) {
            at = end;
            continue;
        }
        std::size_t last = stop;
        if (last < text.size() && text[last] == '%') {
            std::size_t zone = last + 1;
            while (zone < text.size() && (isWordChar(text[zone]) || text[zone] == '-')) {
                ++zone;
            }
            if (zone > last + 1) {
                last = zone;
            }
        }
        std::string with = "<ip>";
        if (at > 0 && text[at - 1] == '[' && last < text.size() && text[last] == ']') {
            const std::size_t port = portEnd(text, last + 1);
            if (port > last + 1) {
                with = "<ip>]:<port>";
                last = port;
            }
        }
        found.push_back({at, last - at, std::move(with)});
        at = last;
    }
}

void addMacs(std::string_view text, std::vector<Replacement>& found)
{
    constexpr std::size_t kLength = 17;
    for (std::size_t at = 0; at + kLength <= text.size(); ++at) {
        if (!isHex(text[at]) ||
            (at > 0 && (isWordChar(text[at - 1]) || text[at - 1] == ':' || text[at - 1] == '-'))) {
            continue;
        }
        const char separator = text[at + 2];
        if (separator != ':' && separator != '-') {
            continue;
        }
        bool mac = true;
        for (std::size_t i = 0; i < kLength && mac; ++i) {
            mac = i % 3 == 2 ? text[at + i] == separator : isHex(text[at + i]);
        }
        const std::size_t end = at + kLength;
        if (mac && (end >= text.size() || (!isWordChar(text[end]) && text[end] != separator))) {
            found.push_back({at, kLength, "<mac>"});
            at = end - 1;
        }
    }
}

bool looksLikeHost(std::string_view host)
{
    if (host.empty() || host.find('.') == kNone || host.find("..") != kNone || host.front() == '.' ||
        host.front() == '-' || host.back() == '.' || host.back() == '-') {
        return false;
    }
    const std::string_view last = host.substr(host.rfind('.') + 1);
    if (last.size() < 2 || !std::ranges::all_of(last, isAlpha)) {
        return false;
    }
    const std::string extension = lowered(last);
    return std::ranges::find(kFileExtensions, std::string_view(extension)) == kFileExtensions.end();
}

void addHostPorts(std::string_view text, std::vector<Replacement>& found)
{
    for (std::size_t colon = text.find(':'); colon != kNone; colon = text.find(':', colon + 1)) {
        const std::size_t port = portEnd(text, colon);
        if (port == colon) {
            continue;
        }
        std::size_t start = colon;
        while (start > 0 && (isAlnum(text[start - 1]) || text[start - 1] == '.' || text[start - 1] == '-')) {
            --start;
        }
        if (start > 0 && (isSeparator(text[start - 1]) || text[start - 1] == '_')) {
            continue;
        }
        if (looksLikeHost(text.substr(start, colon - start))) {
            found.push_back({start, port - start, "<host>:<port>"});
        }
    }
}

void addSerialPatterns(std::string_view text, std::vector<Replacement>& found)
{
    for (std::size_t at = text.find("aid:"); at != kNone; at = text.find("aid:", at + 1)) {
        if (const std::optional<std::string_view> id = androidIdAt(text, at)) {
            found.push_back({at, id->size(), "<serial>"});
        }
    }
    for (std::size_t at = text.find("adb-"); at != kNone; at = text.find("adb-", at + 1)) {
        if (const auto service = adbServiceAt(text, at)) {
            found.push_back({at, service->first.size(), "<serial>"});
        }
    }
}

void addQuoted(std::string_view text, std::size_t begin, std::size_t end, std::vector<Replacement>& found)
{
    constexpr std::array<std::pair<std::string_view, std::string_view>, 2> kQuotes{{
        {"\"", "\""},
        {"\u201C", "\u201D"},
    }};
    for (const auto& [open, close] : kQuotes) {
        std::size_t at = text.find(open, begin);
        while (at != kNone && at < end) {
            const std::size_t inner = at + open.size();
            const std::size_t closing = text.find(close, inner);
            if (closing == kNone || closing >= end) {
                break;
            }
            if (closing > inner) {
                found.push_back({inner, closing - inner, "<clipboard>"});
            }
            at = text.find(open, closing + close.size());
        }
    }
}

std::string scrubComputerName(std::string_view text, std::string_view computerName)
{
    if (computerName.size() < kShortestSecret) {
        return std::string(text);
    }
    std::vector<Replacement> found;
    findAll(lowered(text), lowered(computerName), "<computer>", found);
    return applied(text, std::move(found));
}

std::string utf16(std::string_view text)
{
    const QString wide = QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
    std::string bytes;
    bytes.reserve(static_cast<std::size_t>(wide.size()) * 2);
    for (const QChar c : wide) {
        bytes.push_back(static_cast<char>(c.unicode() & 0xFF));
        bytes.push_back(static_cast<char>(c.unicode() >> 8));
    }
    return bytes;
}

void maskAll(std::string& data, std::string_view lower, std::string_view needle, std::string_view fill)
{
    if (needle.empty()) {
        return;
    }
    for (std::size_t at = lower.find(needle); at != kNone; at = lower.find(needle, at + needle.size())) {
        for (std::size_t i = 0; i < needle.size(); ++i) {
            data[at + i] = fill[i % fill.size()];
        }
    }
}

void maskUserFolders(std::string& data, std::string_view lower, std::size_t width)
{
    const std::string marker = width == 1 ? std::string("\\users\\") : utf16("\\users\\");
    for (std::size_t at = lower.find(marker); at != kNone; at = lower.find(marker, at + marker.size())) {
        const std::size_t start = at + marker.size();
        for (std::size_t p = start; p + width <= data.size() && p - start < kLongestUserName * width; p += width) {
            if ((width == 1 || data[p + 1] == '\0') && endsUserName(data[p])) {
                break;
            }
            data[p] = 'x';
            if (width == 2) {
                data[p + 1] = '\0';
            }
        }
    }
}

void maskAddresses(std::string& data)
{
    for (std::size_t at = 0; at < data.size(); ++at) {
        if (!isDigit(data[at]) || (at > 0 && (isWordChar(data[at - 1]) || data[at - 1] == '.'))) {
            continue;
        }
        if (const std::optional<std::size_t> end = ipv4End(data, at)) {
            for (std::size_t i = at; i < *end; ++i) {
                if (isDigit(data[i])) {
                    data[i] = 'x';
                }
            }
            at = *end;
        }
    }
}

std::optional<std::string> readFile(const std::filesystem::path& file, bool tail)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    const auto limit = static_cast<std::streamoff>(kLargestFile);
    if (size < 0 || (size > limit && !tail)) {
        return std::nullopt;
    }
    const std::streamoff start = size > limit ? size - limit : 0;
    in.seekg(start, std::ios::beg);
    std::string data(static_cast<std::size_t>(size - start), '\0');
    in.read(data.data(), static_cast<std::streamsize>(data.size()));
    data.resize(static_cast<std::size_t>(in.gcount()));
    return data;
}

std::vector<std::filesystem::path> filesIn(const std::filesystem::path& directory, std::string_view extension)
{
    std::vector<std::filesystem::path> files;
    std::error_code error;
    for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end; it.increment(error)) {
        std::error_code kind;
        if (it->is_regular_file(kind) && (extension.empty() || utf8(it->path().extension()) == extension)) {
            files.push_back(it->path());
        }
    }
    std::ranges::sort(files);
    return files;
}

std::size_t writeTo(void* opaque, mz_uint64 offset, const void* data, std::size_t size)
{
    try {
        auto& out = *static_cast<std::string*>(opaque);
        const auto at = static_cast<std::size_t>(offset);
        if (at + size > out.size()) {
            out.resize(at + size);
        }
        std::memcpy(out.data() + at, data, size);
        return size;
    } catch (const std::bad_alloc&) {
        return 0;
    }
}

}

std::vector<std::string> serialsIn(std::string_view text)
{
    std::vector<std::string> found;
    for (const Anchor& anchor : kAnchors) {
        for (std::size_t at = text.find(anchor.phrase); at != kNone; at = text.find(anchor.phrase, at + 1)) {
            std::size_t start = at + anchor.phrase.size();
            if (anchor.skip != Skip::Nothing) {
                const std::size_t skipped = start;
                while (start < text.size() &&
                       (anchor.skip == Skip::Number ? isDigit(text[start]) : !isSpace(text[start]))) {
                    ++start;
                }
                if (start == skipped || text.substr(start, anchor.then.size()) != anchor.then) {
                    continue;
                }
                start += anchor.then.size();
            }
            const std::string_view token = tokenAt(text, start);
            if (token.size() >= kShortestSecret && token.front() != '<' &&
                (anchor.serialOnly || std::ranges::any_of(token, isDigit))) {
                found.emplace_back(token);
            }
        }
    }
    for (std::size_t at = text.find("aid:"); at != kNone; at = text.find("aid:", at + 1)) {
        if (const std::optional<std::string_view> id = androidIdAt(text, at)) {
            found.emplace_back(*id);
        }
    }
    for (std::size_t at = text.find("adb-"); at != kNone; at = text.find("adb-", at + 1)) {
        if (const auto service = adbServiceAt(text, at)) {
            found.emplace_back(service->first);
            found.emplace_back(service->second);
        }
    }
    return distinct(found, kShortestSecret);
}

std::string scrubSerials(std::string_view text, const std::vector<std::string>& serials)
{
    const std::string lower = lowered(text);
    std::vector<Replacement> found;
    const std::vector<std::string> known = distinct(serials, kShortestSecret);
    for (std::size_t i = 0; i < known.size(); ++i) {
        findAll(lower, lowered(known[i]), std::format("<serial-{}>", i + 1), found);
    }
    addSerialPatterns(text, found);
    return applied(text, std::move(found));
}

std::string scrubAddresses(std::string_view text)
{
    std::vector<Replacement> found;
    addIpv4(text, found);
    addIpv6(text, found);
    addMacs(text, found);
    addHostPorts(text, found);
    return applied(text, std::move(found));
}

std::string scrubUserPaths(std::string_view text, std::string_view userName)
{
    constexpr std::string_view kUsers = "users";
    const std::string lower = lowered(text);
    std::vector<Replacement> found;
    for (std::size_t at = lower.find(kUsers); at != kNone; at = lower.find(kUsers, at + kUsers.size())) {
        std::size_t start = at + kUsers.size();
        if (at == 0 || !isSeparator(text[at - 1]) || start >= text.size() || !isSeparator(text[start])) {
            continue;
        }
        while (start < text.size() && isSeparator(text[start])) {
            ++start;
        }
        std::size_t end = start;
        while (end < text.size() && !endsUserName(text[end])) {
            ++end;
        }
        if (end > start) {
            found.push_back({start, end - start, "<user>"});
        }
    }
    if (userName.size() >= 2) {
        const std::string name = lowered(userName);
        for (std::size_t at = lower.find(name); at != kNone; at = lower.find(name, at + 1)) {
            const std::size_t end = at + name.size();
            if (at > 0 && isSeparator(text[at - 1]) && (end == text.size() || endsUserName(text[end]))) {
                found.push_back({at, name.size(), "<user>"});
            }
        }
    }
    return applied(text, std::move(found));
}

std::string scrubWifiNames(std::string_view text)
{
    const std::string lower = lowered(text);
    std::vector<Replacement> found;
    const auto skipBlanks = [&text](std::size_t p) {
        while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) {
            ++p;
        }
        return p;
    };
    for (std::size_t at = lower.find("ssid"); at != kNone; at = lower.find("ssid", at + 4)) {
        std::size_t p = at + 4;
        if (p < text.size() && isWordChar(text[p])) {
            continue;
        }
        if (p < text.size() && text[p] == '"') {
            ++p;
        }
        p = skipBlanks(p);
        const bool separated = p < text.size() && (text[p] == ':' || text[p] == '=');
        if (separated) {
            p = skipBlanks(p + 1);
        }
        if (p >= text.size()) {
            continue;
        }
        if (text[p] == '"' || text[p] == '\'') {
            const std::size_t close = text.find(text[p], p + 1);
            if (close != kNone && close < text.find('\n', p + 1) && close > p + 1) {
                found.push_back({p + 1, close - p - 1, "<wifi>"});
            }
        } else if (separated) {
            std::size_t end = p;
            while (end < text.size() && !isSpace(text[end]) && text[end] != ',' && text[end] != ';' &&
                   text[end] != '}' && text[end] != ')') {
                ++end;
            }
            if (end > p) {
                found.push_back({p, end - p, "<wifi>"});
            }
        }
    }
    for (std::size_t at = lower.find("wifi:"); at != kNone; at = lower.find("wifi:", at + 5)) {
        const std::size_t end = std::min({text.find(";;", at), text.find('\n', at), text.size()});
        std::size_t field = at + 5;
        while (field < end) {
            std::size_t next = field;
            while (next < end && text[next] != ';') {
                next += text[next] == '\\' ? 2 : 1;
            }
            next = std::min(next, end);
            const std::string_view entry = text.substr(field, next - field);
            if (entry.size() > 2 && (entry.starts_with("S:") || entry.starts_with("P:"))) {
                found.push_back({field + 2, entry.size() - 2, entry.front() == 'S' ? "<wifi>" : "<password>"});
            }
            field = next + 1;
        }
    }
    return applied(text, std::move(found));
}

std::string scrubClipboard(std::string_view text, const std::vector<std::string>& clipboard)
{
    const std::string lower = lowered(text);
    std::vector<Replacement> found;
    const std::string token = "<clipboard>";
    for (const std::string& piece : clipboardPieces(clipboard)) {
        findAll(lower, lowered(piece), token, found);
    }
    std::size_t at = lower.find("clipboard");
    while (at != kNone) {
        const std::size_t lineStart = at == 0 ? 0 : lower.rfind('\n', at - 1) + 1;
        const std::size_t lineEnd = std::min(lower.find('\n', at), lower.size());
        addQuoted(text, lineStart, lineEnd, found);
        at = lineEnd < lower.size() ? lower.find("clipboard", lineEnd) : kNone;
    }
    return applied(text, std::move(found));
}

std::string scrub(std::string_view text, const Secrets& secrets)
{
    std::string result = scrubClipboard(text, secrets.clipboard);
    result = scrubSerials(result, secrets.serials);
    result = scrubWifiNames(result);
    result = scrubUserPaths(result, secrets.userName);
    result = scrubComputerName(result, secrets.computerName);
    return scrubAddresses(result);
}

std::string mask(std::string data, const Secrets& secrets)
{
    const std::string lower = lowered(data);
    std::vector<std::string> needles = distinct(secrets.serials, kShortestSecret);
    for (std::string& piece : clipboardPieces(secrets.clipboard)) {
        needles.push_back(std::move(piece));
    }
    for (const std::string& name : {secrets.userName, secrets.computerName}) {
        if (name.size() >= kShortestSecret) {
            needles.push_back(name);
        }
    }
    for (const std::string& needle : needles) {
        maskAll(data, lower, lowered(needle), "x");
        maskAll(data, lower, lowered(utf16(needle)), std::string_view("x\0", 2));
    }
    maskUserFolders(data, lower, 1);
    maskUserFolders(data, lower, 2);
    maskAddresses(data);
    return data;
}

std::vector<Entry> entries(const Sources& sources, Secrets secrets)
{
    std::vector<Entry> found{{"system.txt", sources.system}, {"devices.txt", sources.devices}};
    for (const std::filesystem::path& file : sources.settings) {
        if (std::optional<std::string> data = readFile(file, false)) {
            found.push_back({"settings/" + utf8(file.filename()), std::move(*data)});
        }
    }
    for (const std::filesystem::path& file : filesIn(sources.logs, ".log")) {
        if (std::optional<std::string> data = readFile(file, true)) {
            found.push_back({"logs/" + utf8(file.filename()), std::move(*data)});
        }
    }
    if (sources.crash) {
        const std::string folder = "crash/" + utf8(sources.crash->filename()) + "/";
        for (const std::filesystem::path& file : filesIn(*sources.crash, {})) {
            const bool dump = utf8(file.extension()) == ".dmp";
            if (std::optional<std::string> data = readFile(file, !dump)) {
                found.push_back({folder + utf8(file.filename()), std::move(*data), dump});
            }
        }
    }
    for (const Entry& entry : found) {
        if (!entry.binary) {
            std::vector<std::string> more = serialsIn(entry.data);
            secrets.serials.insert(secrets.serials.end(), std::make_move_iterator(more.begin()),
                                   std::make_move_iterator(more.end()));
        }
    }
    for (Entry& entry : found) {
        entry.name = scrub(entry.name, secrets);
        entry.data = entry.binary ? mask(std::move(entry.data), secrets) : scrub(entry.data, secrets);
    }
    return found;
}

std::optional<std::string> zip(const std::vector<Entry>& entries)
{
    std::string archive;
    mz_zip_archive writer{};
    writer.m_pWrite = &writeTo;
    writer.m_pIO_opaque = &archive;
    if (mz_zip_writer_init(&writer, 0) == 0) {
        return std::nullopt;
    }
    MZ_TIME_T now = std::time(nullptr);
    bool written = true;
    for (const Entry& entry : entries) {
        written = mz_zip_writer_add_mem_ex_v2(&writer, entry.name.c_str(), entry.data.data(), entry.data.size(),
                                              nullptr, 0, MZ_DEFAULT_LEVEL, 0, 0, &now, nullptr, 0, nullptr, 0) != 0;
        if (!written) {
            break;
        }
    }
    written = written && mz_zip_writer_finalize_archive(&writer) != 0;
    mz_zip_writer_end(&writer);
    if (!written) {
        return std::nullopt;
    }
    return archive;
}

std::string describeDevices(const QAbstractItemModel& devices)
{
    const QHash<int, QByteArray> roles = devices.roleNames();
    const auto role = [&roles](const char* name) { return roles.key(QByteArray(name), -1); };
    const int nameRole = role("name");
    const int factsRole = role("facts");
    const int stateRole = role("deviceState");
    const int statusRole = role("status");
    const int problemRole = role("problem");
    const int statsRole = role("stats");
    std::string text;
    for (int row = 0; row < devices.rowCount(); ++row) {
        const QModelIndex index = devices.index(row, 0);
        const auto value = [&devices, &index](int which) {
            return which < 0 ? QVariant() : devices.data(index, which);
        };
        const auto line = [&text](std::string_view label, const QString& content) {
            if (!content.isEmpty()) {
                text += std::format("  {}{}\n", label, content.toStdString());
            }
        };
        text += value(nameRole).toString().toStdString() + '\n';
        const QVariantMap facts = value(factsRole).toMap();
        line("android: ", facts.value(QStringLiteral("android")).toString());
        line("attachment: ", facts.value(QStringLiteral("attachment")).toString());
        line("link: ", facts.value(QStringLiteral("link")).toString());
        line("state: ", value(stateRole).toString());
        line("status: ", value(statusRole).toString());
        line("problem: ", value(problemRole).toString());
        const QVariantMap stats = value(statsRole).toMap();
        if (!stats.isEmpty()) {
            std::string metrics = std::format("{:.1f} fps, {:.1f} Mb/s", stats.value(QStringLiteral("fps")).toDouble(),
                                              stats.value(QStringLiteral("megabits")).toDouble());
            if (stats.contains(QStringLiteral("latencyMs"))) {
                metrics += std::format(", {:.0f} ms from input to picture",
                                       stats.value(QStringLiteral("latencyMs")).toDouble());
            }
            line("latest: ", QString::fromStdString(metrics));
        }
    }
    return text.empty() ? std::string("No device is listed.\n") : text;
}

std::vector<std::string> serialsOf(const QAbstractItemModel& devices)
{
    const QHash<int, QByteArray> roles = devices.roleNames();
    std::vector<std::string> serials;
    for (const char* name : {"serial", "deviceId"}) {
        const int which = roles.key(QByteArray(name), -1);
        if (which < 0) {
            continue;
        }
        for (int row = 0; row < devices.rowCount(); ++row) {
            const QString serial = devices.data(devices.index(row, 0), which).toString();
            if (!serial.isEmpty()) {
                serials.push_back(serial.toStdString());
            }
        }
    }
    return serials;
}

}

DiagnosticsBundle::DiagnosticsBundle(AppSettings& settings, std::filesystem::path dataDirectory, QObject* parent)
    : QObject(parent),
      m_settings(settings),
      m_data(std::move(dataDirectory))
{
    Q_ASSERT(s_instance == nullptr);
    s_instance = this;
}

DiagnosticsBundle::~DiagnosticsBundle()
{
    s_instance = nullptr;
}

DiagnosticsBundle* DiagnosticsBundle::create(QQmlEngine*, QJSEngine*)
{
    Q_ASSERT(s_instance != nullptr);
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

void DiagnosticsBundle::setDevices(const QAbstractItemModel* devices)
{
    m_devices = devices;
}

void DiagnosticsBundle::setCrashReport(const CrashReport* crashes)
{
    m_crashes = crashes;
}

void DiagnosticsBundle::setGraphics(QString description)
{
    m_graphics = std::move(description);
}

diagnostics::Sources DiagnosticsBundle::sources() const
{
    diagnostics::Sources found;
    found.system = systemText();
    found.devices = m_devices != nullptr ? diagnostics::describeDevices(*m_devices) : "No device is listed.\n";
    found.settings = {m_data / "settings.json", m_data / "devices.json"};
    found.logs = m_data / "logs";
    if (m_crashes != nullptr) {
        found.crash = m_crashes->newestFolder();
    }
    return found;
}

diagnostics::Secrets DiagnosticsBundle::secrets() const
{
    diagnostics::Secrets known;
    if (m_devices != nullptr) {
        known.serials = diagnostics::serialsOf(*m_devices);
    }
    for (const auto& device : m_settings.devices()) {
        known.serials.push_back(device.first);
    }
    for (const QVariant& connection : m_settings.recentConnections()) {
        known.serials.push_back(connection.toMap().value(QStringLiteral("address")).toString().toStdString());
    }
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr) {
        if (const QClipboard* clipboard = QGuiApplication::clipboard()) {
            if (const QString text = clipboard->text(); !text.isEmpty()) {
                known.clipboard.push_back(text.toStdString());
            }
        }
    }
    known.userName = qEnvironmentVariable("USERNAME").toStdString();
    known.computerName = qEnvironmentVariable("COMPUTERNAME").toStdString();
    return known;
}

QString DiagnosticsBundle::suggestedName() const
{
    return QStringLiteral("GameScrcpy-diagnostics-%1.zip").arg(QDate::currentDate().toString(Qt::ISODate));
}

bool DiagnosticsBundle::exportTo(const QString& fileOrUrl)
{
    const QUrl url(fileOrUrl);
    const QString local = url.isLocalFile() ? url.toLocalFile() : fileOrUrl;
    if (m_exporting || local.isEmpty() || !QDir::isAbsolutePath(local)) {
        emit exportFinished(QVariantMap{{QStringLiteral("ok"), false}});
        return false;
    }
    m_exporting = true;
    emit exportingChanged();
    m_settings.flush();
    m_worker = std::jthread([this, target = std::filesystem::path(local.toStdWString()), from = sources(),
                             hidden = secrets()]() mutable {
        const std::optional<std::string> archive = diagnostics::zip(diagnostics::entries(from, std::move(hidden)));
        const bool saved = archive.has_value() && !platform::writeFileAtomically(target, *archive);
        QMetaObject::invokeMethod(this, [this, saved, target] { finish(saved, target); }, Qt::QueuedConnection);
    });
    return true;
}

std::string DiagnosticsBundle::systemText() const
{
    const AppInfo info;
    return std::format("GameScrcpy {} ({})\nbuild: {}\nsystem: {} ({}), {}\ngraphics: {}\nlanguage: {}\n"
                       "data folder: {}\nexported: {}\n",
                       info.version().toStdString(), info.revision().toStdString(), info.build().toStdString(),
                       QSysInfo::prettyProductName().toStdString(), QSysInfo::kernelVersion().toStdString(),
                       QSysInfo::currentCpuArchitecture().toStdString(),
                       m_graphics.isEmpty() ? std::string("unknown") : m_graphics.toStdString(),
                       m_settings.interfaceLanguage().toStdString(), diagnostics::utf8(m_data),
                       QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString());
}

void DiagnosticsBundle::finish(bool saved, const std::filesystem::path& target)
{
    m_exporting = false;
    emit exportingChanged();
    const QString name = QString::fromStdWString(target.filename().wstring());
    if (saved) {
        qInfo("diagnostics saved as %s", qUtf8Printable(name));
    } else {
        qWarning("diagnostics could not be saved as %s", qUtf8Printable(name));
    }
    emit exportFinished(QVariantMap{{QStringLiteral("ok"), saved}, {QStringLiteral("name"), name}});
}

}
