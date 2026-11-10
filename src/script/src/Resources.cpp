#include "gs/script/Resources.h"

#include <gs/platform/AtomicFile.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <set>
#include <utility>

namespace gs::script {

namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

constexpr std::size_t kMostResourceBytes = std::size_t{4} << 20;
constexpr std::size_t kMostFileBytes = 200;
constexpr double kSlack = 1e-9;
constexpr std::string_view kNameRule =
    "must be 1 to 64 characters without control characters, / \\ or :, and must not end in .png, .jpg, .jpeg or .bmp";

char lower(char c) noexcept
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}

std::string lowered(std::string_view text)
{
    std::string result(text);
    std::ranges::transform(result, result.begin(), lower);
    return result;
}

bool endsWithNoCase(std::string_view text, std::string_view ending) noexcept
{
    if (text.size() < ending.size()) {
        return false;
    }
    return std::ranges::equal(text.substr(text.size() - ending.size()), ending,
                              [](char a, char b) { return lower(a) == lower(b); });
}

bool looksLikePicture(std::string_view text) noexcept
{
    return endsWithNoCase(text, ".png") || endsWithNoCase(text, ".jpg") || endsWithNoCase(text, ".jpeg")
        || endsWithNoCase(text, ".bmp");
}

bool plainText(std::string_view text) noexcept
{
    return std::ranges::none_of(text, [](char c) {
        const auto byte = static_cast<unsigned char>(c);
        return byte < 0x20 || byte == 0x7F || c == '/' || c == '\\' || c == ':';
    });
}

bool sameNoCase(std::string_view a, std::string_view b) noexcept
{
    return a.size() == b.size() && std::ranges::equal(a, b, [](char x, char y) { return lower(x) == lower(y); });
}

bool reservedStem(std::string_view file) noexcept
{
    const std::string_view stem = file.substr(0, file.find('.'));
    constexpr std::array<std::string_view, 4> kDevices{"con", "prn", "aux", "nul"};
    if (std::ranges::any_of(kDevices, [stem](std::string_view device) { return sameNoCase(stem, device); })) {
        return true;
    }
    return stem.size() == 4 && (sameNoCase(stem.substr(0, 3), "com") || sameNoCase(stem.substr(0, 3), "lpt"))
        && stem[3] >= '0' && stem[3] <= '9';
}

bool ratio(double value) noexcept
{
    return value >= 0.0 && value <= 1.0;
}

std::string at(std::string_view path, std::string_view problem)
{
    return std::format("{}: {}", path, problem);
}

int hexValue(char c) noexcept
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    const char low = lower(c);
    if (low >= 'a' && low <= 'f') {
        return low - 'a' + 10;
    }
    return -1;
}

template <typename Item>
const Item* named(const std::vector<Item>& items, std::string_view name) noexcept
{
    const auto found = std::ranges::find_if(items, [name](const Item& item) { return item.name == name; });
    return found != items.end() ? &*found : nullptr;
}

std::string_view matchName(MatchMode match) noexcept
{
    return match == MatchMode::Color ? "color" : "grey";
}

class Reader
{
public:
    explicit Reader(std::vector<std::string>& problems) : m_problems(problems) {}

    bool object(const Json& value, const std::string& path, std::initializer_list<std::string_view> known)
    {
        if (!value.is_object()) {
            m_problems.push_back(at(path, "must be an object"));
            return false;
        }
        for (auto entry = value.begin(); entry != value.end(); ++entry) {
            if (std::ranges::find(known, std::string_view(entry.key())) == known.end()) {
                m_problems.push_back(at(path + "." + entry.key(), "is not a known field"));
            }
        }
        return true;
    }

    void text(const Json& value, const std::string& path, const char* key, std::string& out, bool required)
    {
        const auto found = value.find(key);
        if (found == value.end()) {
            if (required) {
                m_problems.push_back(at(path + "." + key, "is missing"));
            }
            return;
        }
        if (!found->is_string()) {
            m_problems.push_back(at(path + "." + key, "must be a string"));
            return;
        }
        out = found->get<std::string>();
    }

    void number(const Json& value, const std::string& path, const char* key, double& out, bool required)
    {
        const auto found = value.find(key);
        if (found == value.end()) {
            if (required) {
                m_problems.push_back(at(path + "." + key, "is missing"));
            }
            return;
        }
        if (!found->is_number()) {
            m_problems.push_back(at(path + "." + key, "must be a number"));
            return;
        }
        out = found->get<double>();
    }

    void match(const Json& value, const std::string& path, MatchMode& out)
    {
        std::string mode(matchName(out));
        text(value, path, "match", mode, false);
        if (mode == "color") {
            out = MatchMode::Color;
        } else if (mode == "grey") {
            out = MatchMode::Grey;
        } else {
            m_problems.push_back(at(path + ".match", "must be \"color\" or \"grey\""));
        }
    }

    void size(const Json& value, const std::string& path, PictureSize& out)
    {
        const auto found = value.find("source");
        if (found == value.end()) {
            return;
        }
        const std::string inner = path + ".source";
        if (!object(*found, inner, {"width", "height"})) {
            return;
        }
        const auto side = [&](const char* key, int& target) {
            const auto entry = found->find(key);
            if (entry == found->end() || !entry->is_number_integer() || entry->get<std::int64_t>() < 1
                || entry->get<std::int64_t>() > kMostPictureSide) {
                m_problems.push_back(at(inner + "." + key, "must be a whole number of pixels from 1 to 65535"));
                return;
            }
            target = entry->get<int>();
        };
        side("width", out.width);
        side("height", out.height);
    }

    void color(const Json& value, const std::string& path, Rgb& out)
    {
        std::string written;
        text(value, path, "color", written, true);
        if (written.empty()) {
            return;
        }
        if (const std::optional<Rgb> parsed = parseColor(written)) {
            out = *parsed;
        } else {
            m_problems.push_back(at(path + ".color", "must be written \"#RRGGBB\""));
        }
    }

    void points(const Json& value, const std::string& path, std::vector<Point>& out)
    {
        const auto found = value.find("points");
        if (found == value.end()) {
            m_problems.push_back(at(path + ".points", "is missing"));
            return;
        }
        if (!found->is_array()) {
            m_problems.push_back(at(path + ".points", "must be a list of [x, y] pairs"));
            return;
        }
        for (std::size_t index = 0; index < found->size(); ++index) {
            const Json& pair = (*found)[index];
            if (!pair.is_array() || pair.size() != 2 || !pair[0].is_number() || !pair[1].is_number()) {
                m_problems.push_back(at(std::format("{}.points[{}]", path, index), "must be an [x, y] pair of numbers"));
                continue;
            }
            out.push_back({pair[0].get<double>(), pair[1].get<double>()});
        }
    }

    template <typename Item, typename Read>
    void list(const Json& root, const char* key, std::vector<Item>& out, Read read)
    {
        const auto found = root.find(key);
        if (found == root.end()) {
            return;
        }
        const std::string path = std::string("$.") + key;
        if (!found->is_array()) {
            m_problems.push_back(at(path, "must be a list"));
            return;
        }
        if (found->size() > kMostResourcesOfAKind) {
            m_problems.push_back(at(path, "has more than 1000 entries"));
            return;
        }
        for (std::size_t index = 0; index < found->size(); ++index) {
            Item item;
            if (read((*found)[index], std::format("{}[{}]", path, index), item)) {
                out.push_back(std::move(item));
            }
        }
    }

private:
    std::vector<std::string>& m_problems;
};

template <typename Item>
void checkNames(std::vector<std::string>& problems, std::string_view list, const std::vector<Item>& items)
{
    if (items.size() > kMostResourcesOfAKind) {
        problems.push_back(at(std::format("$.{}", list), "has more than 1000 entries"));
    }
    std::set<std::string_view> seen;
    for (std::size_t index = 0; index < items.size(); ++index) {
        const std::string path = std::format("$.{}[{}].name", list, index);
        if (!resourceName(items[index].name)) {
            problems.push_back(at(path, kNameRule));
        } else if (!seen.insert(items[index].name).second) {
            problems.push_back(at(path, std::format("\"{}\" is used twice", items[index].name)));
        }
    }
}

OrderedJson pointPair(const Point& point)
{
    return OrderedJson::array({point.x, point.y});
}

}

const TemplateResource* Resources::findTemplate(std::string_view name) const noexcept
{
    return named(templates, name);
}

const RegionResource* Resources::findRegion(std::string_view name) const noexcept
{
    return named(regions, name);
}

const PointResource* Resources::findPoint(std::string_view name) const noexcept
{
    return named(points, name);
}

const PathResource* Resources::findPath(std::string_view name) const noexcept
{
    return named(paths, name);
}

const ColorResource* Resources::findColor(std::string_view name) const noexcept
{
    return named(colors, name);
}

bool Resources::empty() const noexcept
{
    return templates.empty() && regions.empty() && points.empty() && paths.empty() && colors.empty();
}

std::size_t characterCount(std::string_view text) noexcept
{
    return static_cast<std::size_t>(std::ranges::count_if(
        text, [](char c) { return (static_cast<unsigned char>(c) & 0xC0U) != 0x80U; }));
}

bool resourceName(std::string_view name) noexcept
{
    const std::size_t characters = characterCount(name);
    return characters >= 1 && characters <= kMostResourceNameChars && plainText(name) && !looksLikePicture(name);
}

bool resourceFile(std::string_view file) noexcept
{
    constexpr std::string_view kRefused = "*?\"<>|";
    return file.size() > 4 && file.size() <= kMostFileBytes && file.front() != '.' && file.front() != ' ' && plainText(file)
        && file.find_first_of(kRefused) == std::string_view::npos && endsWithNoCase(file, ".png") && !reservedStem(file);
}

std::optional<Rgb> parseColor(std::string_view text) noexcept
{
    if (text.size() != 7 || text.front() != '#') {
        return std::nullopt;
    }
    std::array<int, 6> digits{};
    for (std::size_t index = 0; index < digits.size(); ++index) {
        digits[index] = hexValue(text[index + 1]);
        if (digits[index] < 0) {
            return std::nullopt;
        }
    }
    return Rgb{static_cast<std::uint8_t>(digits[0] * 16 + digits[1]), static_cast<std::uint8_t>(digits[2] * 16 + digits[3]),
               static_cast<std::uint8_t>(digits[4] * 16 + digits[5])};
}

std::string colorText(Rgb color)
{
    return std::format("#{:02X}{:02X}{:02X}", color.red, color.green, color.blue);
}

std::string_view kindName(ResourceKind kind) noexcept
{
    switch (kind) {
    case ResourceKind::Template:
        return "template";
    case ResourceKind::Region:
        return "region";
    case ResourceKind::Point:
        return "point";
    case ResourceKind::Path:
        return "path";
    case ResourceKind::Color:
        return "color";
    }
    return "resource";
}

std::vector<std::string> checkResources(const Resources& resources)
{
    std::vector<std::string> problems;
    checkNames(problems, "templates", resources.templates);
    checkNames(problems, "regions", resources.regions);
    checkNames(problems, "points", resources.points);
    checkNames(problems, "paths", resources.paths);
    checkNames(problems, "colors", resources.colors);
    std::set<std::string> files;
    for (std::size_t index = 0; index < resources.templates.size(); ++index) {
        const TemplateResource& entry = resources.templates[index];
        const std::string path = std::format("$.templates[{}]", index);
        if (!resourceFile(entry.file)) {
            problems.push_back(at(path + ".file", "must be the name of a .png file in res/, without folders"));
        } else if (!files.insert(lowered(entry.file)).second) {
            problems.push_back(at(path + ".file", std::format("\"{}\" belongs to another template", entry.file)));
        }
        if (!ratio(entry.threshold)) {
            problems.push_back(at(path + ".threshold", "must be a number from 0 to 1"));
        }
        if (!entry.region.empty() && resources.findRegion(entry.region) == nullptr) {
            problems.push_back(at(path + ".region", std::format("there is no region named \"{}\"", entry.region)));
        }
        const bool sized = entry.source.width >= 1 && entry.source.width <= kMostPictureSide && entry.source.height >= 1
                        && entry.source.height <= kMostPictureSide;
        if (entry.source != PictureSize{} && !sized) {
            problems.push_back(at(path + ".source", "must have a width and a height from 1 to 65535 pixels"));
        }
    }
    for (std::size_t index = 0; index < resources.regions.size(); ++index) {
        const Region& area = resources.regions[index].area;
        if (!ratio(area.x) || !ratio(area.y) || !(area.width > 0.0) || !(area.height > 0.0)
            || area.x + area.width > 1.0 + kSlack || area.y + area.height > 1.0 + kSlack) {
            problems.push_back(at(std::format("$.regions[{}]", index),
                                  "must lie inside the picture: x, y, width and height are ratios from 0 to 1"));
        }
    }
    for (std::size_t index = 0; index < resources.points.size(); ++index) {
        const Point& point = resources.points[index].at;
        if (!ratio(point.x) || !ratio(point.y)) {
            problems.push_back(at(std::format("$.points[{}]", index), "x and y must be ratios from 0 to 1"));
        }
    }
    for (std::size_t index = 0; index < resources.paths.size(); ++index) {
        const PathResource& entry = resources.paths[index];
        const std::string path = std::format("$.paths[{}]", index);
        if (entry.points.size() < 2 || entry.points.size() > kMostPathPoints) {
            problems.push_back(at(path + ".points", "must hold 2 to 64 points"));
        }
        if (!std::ranges::all_of(entry.points, [](const Point& point) { return ratio(point.x) && ratio(point.y); })) {
            problems.push_back(at(path + ".points", "x and y must be ratios from 0 to 1"));
        }
        if (!(entry.durationMs > 0.0 && entry.durationMs <= kLongestPathMs)) {
            problems.push_back(at(path + ".durationMs", "must be more than 0 and at most 60000 milliseconds"));
        }
    }
    for (std::size_t index = 0; index < resources.colors.size(); ++index) {
        const ColorResource& entry = resources.colors[index];
        const std::string path = std::format("$.colors[{}]", index);
        if (!ratio(entry.tolerance)) {
            problems.push_back(at(path + ".tolerance", "must be a number from 0 to 1"));
        }
        if (!entry.region.empty() && resources.findRegion(entry.region) == nullptr) {
            problems.push_back(at(path + ".region", std::format("there is no region named \"{}\"", entry.region)));
        }
    }
    return problems;
}

LoadedResources parseResources(std::string_view text)
{
    const Json root = Json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return makeUnexpected(std::vector<std::string>{"$: the file is not a JSON object"});
    }
    const auto format = root.find("format");
    if (format == root.end() || !format->is_string() || format->get_ref<const std::string&>() != kResourcesFormat) {
        return makeUnexpected(std::vector<std::string>{at("$.format", std::format("must be \"{}\"", kResourcesFormat))});
    }
    const auto version = root.find("version");
    if (version == root.end() || !version->is_number_integer() || version->get<std::int64_t>() != kResourcesVersion) {
        return makeUnexpected(std::vector<std::string>{at("$.version", std::format("must be {}", kResourcesVersion))});
    }
    std::vector<std::string> problems;
    Reader reader(problems);
    static_cast<void>(reader.object(root, "$", {"format", "version", "templates", "regions", "points", "paths", "colors"}));
    Resources resources;
    reader.list(root, "templates", resources.templates, [&reader](const Json& value, const std::string& path, TemplateResource& item) {
        if (!reader.object(value, path, {"name", "file", "threshold", "match", "region", "source"})) {
            return false;
        }
        reader.text(value, path, "name", item.name, true);
        reader.text(value, path, "file", item.file, true);
        reader.number(value, path, "threshold", item.threshold, false);
        reader.match(value, path, item.match);
        reader.text(value, path, "region", item.region, false);
        reader.size(value, path, item.source);
        return true;
    });
    reader.list(root, "regions", resources.regions, [&reader](const Json& value, const std::string& path, RegionResource& item) {
        if (!reader.object(value, path, {"name", "x", "y", "width", "height"})) {
            return false;
        }
        reader.text(value, path, "name", item.name, true);
        reader.number(value, path, "x", item.area.x, true);
        reader.number(value, path, "y", item.area.y, true);
        reader.number(value, path, "width", item.area.width, true);
        reader.number(value, path, "height", item.area.height, true);
        return true;
    });
    reader.list(root, "points", resources.points, [&reader](const Json& value, const std::string& path, PointResource& item) {
        if (!reader.object(value, path, {"name", "x", "y"})) {
            return false;
        }
        reader.text(value, path, "name", item.name, true);
        reader.number(value, path, "x", item.at.x, true);
        reader.number(value, path, "y", item.at.y, true);
        return true;
    });
    reader.list(root, "paths", resources.paths, [&reader](const Json& value, const std::string& path, PathResource& item) {
        if (!reader.object(value, path, {"name", "points", "durationMs"})) {
            return false;
        }
        reader.text(value, path, "name", item.name, true);
        reader.points(value, path, item.points);
        reader.number(value, path, "durationMs", item.durationMs, false);
        return true;
    });
    reader.list(root, "colors", resources.colors, [&reader](const Json& value, const std::string& path, ColorResource& item) {
        if (!reader.object(value, path, {"name", "color", "tolerance", "region"})) {
            return false;
        }
        reader.text(value, path, "name", item.name, true);
        reader.color(value, path, item.color);
        reader.number(value, path, "tolerance", item.tolerance, false);
        reader.text(value, path, "region", item.region, false);
        return true;
    });
    if (problems.empty()) {
        problems = checkResources(resources);
    }
    if (!problems.empty()) {
        return makeUnexpected(std::move(problems));
    }
    return resources;
}

std::string writeResources(const Resources& resources)
{
    OrderedJson templates = OrderedJson::array();
    for (const TemplateResource& entry : resources.templates) {
        OrderedJson item = OrderedJson::object();
        item["name"] = entry.name;
        item["file"] = entry.file;
        item["threshold"] = entry.threshold;
        item["match"] = std::string(matchName(entry.match));
        if (!entry.region.empty()) {
            item["region"] = entry.region;
        }
        if (entry.source != PictureSize{}) {
            OrderedJson source = OrderedJson::object();
            source["width"] = entry.source.width;
            source["height"] = entry.source.height;
            item["source"] = std::move(source);
        }
        templates.push_back(std::move(item));
    }
    OrderedJson regions = OrderedJson::array();
    for (const RegionResource& entry : resources.regions) {
        OrderedJson item = OrderedJson::object();
        item["name"] = entry.name;
        item["x"] = entry.area.x;
        item["y"] = entry.area.y;
        item["width"] = entry.area.width;
        item["height"] = entry.area.height;
        regions.push_back(std::move(item));
    }
    OrderedJson points = OrderedJson::array();
    for (const PointResource& entry : resources.points) {
        OrderedJson item = OrderedJson::object();
        item["name"] = entry.name;
        item["x"] = entry.at.x;
        item["y"] = entry.at.y;
        points.push_back(std::move(item));
    }
    OrderedJson paths = OrderedJson::array();
    for (const PathResource& entry : resources.paths) {
        OrderedJson item = OrderedJson::object();
        OrderedJson pairs = OrderedJson::array();
        for (const Point& point : entry.points) {
            pairs.push_back(pointPair(point));
        }
        item["name"] = entry.name;
        item["points"] = std::move(pairs);
        item["durationMs"] = entry.durationMs;
        paths.push_back(std::move(item));
    }
    OrderedJson colors = OrderedJson::array();
    for (const ColorResource& entry : resources.colors) {
        OrderedJson item = OrderedJson::object();
        item["name"] = entry.name;
        item["color"] = colorText(entry.color);
        item["tolerance"] = entry.tolerance;
        if (!entry.region.empty()) {
            item["region"] = entry.region;
        }
        colors.push_back(std::move(item));
    }
    OrderedJson root = OrderedJson::object();
    root["format"] = std::string(kResourcesFormat);
    root["version"] = kResourcesVersion;
    root["templates"] = std::move(templates);
    root["regions"] = std::move(regions);
    root["points"] = std::move(points);
    root["paths"] = std::move(paths);
    root["colors"] = std::move(colors);
    return root.dump(2, ' ', false, OrderedJson::error_handler_t::replace) + "\n";
}

LoadedResources loadResources(const std::filesystem::path& file)
{
    std::string contents;
    const std::error_code error = platform::readFile(file, kMostResourceBytes, contents, platform::WhenBusy::GiveUp);
    if (platform::fileMissing(error)) {
        return Resources{};
    }
    if (error == std::errc::file_too_large) {
        return makeUnexpected(std::vector<std::string>{"$: the file is larger than 4 MiB"});
    }
    if (error) {
        return makeUnexpected(std::vector<std::string>{
            platform::fileBusy(error) ? "$: another program is using the file" : "$: the file cannot be read"});
    }
    return parseResources(contents);
}

std::error_code saveResources(const std::filesystem::path& file, const Resources& resources)
{
    if (!checkResources(resources).empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    if (error) {
        return error;
    }
    return platform::writeFileAtomically(file, writeResources(resources));
}

ResourceFile::ResourceFile(std::filesystem::path file) : m_file(std::move(file)), m_loaded(Resources{}) {}

const LoadedResources& ResourceFile::current()
{
    std::error_code error;
    const std::filesystem::file_time_type written = std::filesystem::last_write_time(m_file, error);
    const std::uintmax_t size = error ? 0 : std::filesystem::file_size(m_file, error);
    const bool present = !error;
    if (m_read && present == m_present && (!present || (written == m_written && size == m_size))) {
        return m_loaded;
    }
    m_read = true;
    m_present = present;
    m_written = present ? written : std::filesystem::file_time_type{};
    m_size = present ? size : 0;
    m_loaded = present ? loadResources(m_file) : LoadedResources(Resources{});
    return m_loaded;
}

}
