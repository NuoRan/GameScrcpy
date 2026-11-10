#include "ModulePath.h"

#include "gs/script/LogicHost.h"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <vector>

namespace gs::script::detail {

namespace {

constexpr std::string_view kForbidden = "\\:*?\"<>|";

std::vector<std::string_view> segmentsOf(std::string_view path)
{
    std::vector<std::string_view> segments;
    for (;;) {
        const std::size_t slash = path.find('/');
        segments.push_back(path.substr(0, slash));
        if (slash == std::string_view::npos) {
            return segments;
        }
        path.remove_prefix(slash + 1);
    }
}

std::optional<std::string> segmentProblem(std::string_view segment)
{
    if (segment.empty()) {
        return "contains an empty path segment";
    }
    const bool forbidden = std::ranges::any_of(segment, [](char c) {
        return static_cast<unsigned char>(c) < 0x20 || kForbidden.find(c) != std::string_view::npos;
    });
    if (forbidden) {
        return "contains a character that file names cannot have";
    }
    if (segment.back() == '.' || segment.back() == ' ') {
        return "has a path segment that ends with a dot or a space";
    }
    return std::nullopt;
}

std::string extensionOf(std::string_view name)
{
    const std::size_t dot = name.rfind('.');
    if (dot == std::string_view::npos || name.find('/', dot) != std::string_view::npos) {
        return {};
    }
    std::string extension(name.substr(dot));
    std::ranges::transform(extension, extension.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return extension;
}

bool hasModuleExtension(std::string_view name)
{
    const std::string extension = extensionOf(name);
    return extension == ".js" || extension == ".mjs" || extension == ".ts" || extension == ".mts";
}

}

bool isTypeScriptModule(std::string_view name)
{
    const std::string extension = extensionOf(name);
    return extension == ".ts" || extension == ".mts";
}

std::filesystem::path pathOf(std::string_view utf8)
{
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::optional<std::string> moduleNameProblem(std::string_view name)
{
    if (name.empty()) {
        return "is empty";
    }
    for (const std::string_view segment : segmentsOf(name)) {
        if (segment == "." || segment == "..") {
            return "must name a file inside the script folder without '.' or '..'";
        }
        if (auto problem = segmentProblem(segment)) {
            return problem;
        }
    }
    if (!hasModuleExtension(name)) {
        return "is not a .js, .mjs, .ts or .mts file";
    }
    return std::nullopt;
}

Expected<std::string, std::string> resolveImport(std::string_view importer,
                                                 std::string_view specifier)
{
    if (!specifier.starts_with("./") && !specifier.starts_with("../")) {
        return makeUnexpected(std::string(
            "only relative imports such as './util.js' and the built-in module \"gamescrcpy\" are supported"));
    }
    std::vector<std::string_view> parts = segmentsOf(importer);
    parts.pop_back();
    for (const std::string_view segment : segmentsOf(specifier)) {
        if (segment == ".") {
            continue;
        }
        if (segment == "..") {
            if (parts.empty()) {
                return makeUnexpected(std::string("the path leaves the script folder"));
            }
            parts.pop_back();
            continue;
        }
        if (auto problem = segmentProblem(segment)) {
            return makeUnexpected("the path " + *problem);
        }
        parts.push_back(segment);
    }
    std::string resolved;
    for (const std::string_view part : parts) {
        if (!resolved.empty()) {
            resolved += '/';
        }
        resolved += part;
    }
    if (!hasModuleExtension(resolved)) {
        return makeUnexpected(std::string("the path does not name a .js, .mjs, .ts or .mts file"));
    }
    return resolved;
}

Expected<std::filesystem::path, std::string> moduleFile(const std::filesystem::path& folder,
                                                        std::string_view name)
{
    std::error_code error;
    const std::filesystem::path root = std::filesystem::canonical(folder, error);
    if (error) {
        return makeUnexpected(
            std::string("cannot be found because the script folder cannot be opened"));
    }
    const std::filesystem::path file = std::filesystem::canonical(root / pathOf(name), error);
    if (error || !std::filesystem::is_regular_file(file, error)) {
        return makeUnexpected(std::string("cannot be found"));
    }
    const std::filesystem::path relative = file.lexically_relative(root);
    if (relative.empty() || *relative.begin() == "..") {
        return makeUnexpected(std::string("resolves to a file outside the script folder"));
    }
    return file;
}

}

namespace gs::script {

std::vector<std::string> controlModules(const std::filesystem::path& folder)
{
    std::vector<std::string> modules;
    std::error_code error;
    for (std::filesystem::directory_iterator it(folder / "controls", error), end; !error && it != end;
         it.increment(error)) {
        std::error_code kind;
        if (!it->is_regular_file(kind)) {
            continue;
        }
        const std::u8string file = it->path().filename().u8string();
        const std::string name = "controls/" + std::string(file.begin(), file.end());
        if (!name.ends_with(".d.ts") && !detail::moduleNameProblem(name)) {
            modules.push_back(name);
        }
    }
    std::ranges::sort(modules);
    return modules;
}

}
