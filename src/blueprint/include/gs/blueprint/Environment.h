#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gs::blueprint {

class Api;

[[nodiscard]] std::shared_ptr<const Api> loadApi(std::string_view report, std::string_view declarations);

[[nodiscard]] std::vector<std::string> variableTypeNames();

class Labels
{
public:
    Labels() = default;
    Labels(const Labels&) = default;
    Labels& operator=(const Labels&) = default;
    virtual ~Labels() = default;

    [[nodiscard]] virtual std::string text(std::string_view key) const = 0;
    [[nodiscard]] virtual std::string english(std::string_view key) const = 0;
    [[nodiscard]] virtual std::string keyName(std::string_view key) const = 0;
};

struct ControlEntry {
    std::string id;
    std::string name;
    std::string type;
    std::string key;
};

struct VariableEntry {
    std::string id;
    std::string name;
    std::string kind;
};

struct ImageEntry {
    std::string name;
    std::string preview;
};

struct ExtensionEntry {
    std::string owner;
    std::string name;
};

struct LibraryParam {
    std::string name;
    bool optional = false;
};

struct LibraryFunction {
    std::string name;
    std::vector<LibraryParam> params;
    bool async = false;
};

inline constexpr std::string_view kLibraryFile = "library.ts";

[[nodiscard]] std::vector<LibraryFunction> libraryFunctions(std::string_view typescript);
[[nodiscard]] std::string libraryImportFor(std::string_view file);

struct Environment {
    std::shared_ptr<const Api> api;
    const Labels* labels = nullptr;
    std::vector<ControlEntry> controls;
    std::vector<ControlEntry> gone;
    std::vector<VariableEntry> vars;
    std::vector<ImageEntry> images;
    std::vector<std::string> regions;
    std::vector<std::string> points;
    std::vector<ExtensionEntry> extensions;
    std::vector<std::string> files;
    std::vector<LibraryFunction> library;
    std::string libraryImport;
    std::string fileName;
    std::string ownControl;
    bool readOnly = false;
};

}
