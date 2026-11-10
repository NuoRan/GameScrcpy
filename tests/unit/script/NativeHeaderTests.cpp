#include "gs/script/ApiFiles.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::string generatedHeader()
{
    const std::vector<gs::script::ApiFile> files = gs::script::apiFiles();
    const auto found = std::ranges::find(files, std::string("include/gamescrcpy_ext.h"), &gs::script::ApiFile::path);
    REQUIRE(found != files.end());
    return found->contents;
}

}

TEST_CASE("ADR-0097: the C header for native extensions comes from the tables and matches the one in sdk/include")
{
    const std::string header = generatedHeader();
    std::ifstream in(std::filesystem::path(GS_SDK_DIR) / "include" / "gamescrcpy_ext.h", std::ios::binary);
    REQUIRE(in.is_open());
    std::string checkedIn{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    std::erase(checkedIn, '\r');
    CHECK(checkedIn == header);
}

TEST_CASE("ADR-0097: the C header names every manifest type in order and takes their meaning from the binding table")
{
    const std::string header = generatedHeader();
    CHECK(header.starts_with("/* GameScrcpy native extension interface"));
    CHECK(header.find("#define GS_EXT_ABI_VERSION 1\n") != std::string::npos);
    CHECK(header.find("extern \"C\" {") != std::string::npos);
    CHECK(header.find("GS_EXT_EXPORT int32_t gs_ext_entry(const gs_ext_host* host, gs_ext_module* module);\n") !=
          std::string::npos);
    constexpr std::array<std::string_view, 8> kTypes{"boolean", "number", "string",   "point",
                                                     "region",  "color",  "duration", "json"};
    for (std::size_t index = 0; index < kTypes.size(); ++index) {
        std::string name(kTypes[index]);
        CAPTURE(name);
        CHECK(header.find("\"" + name + "\" in the manifest") != std::string::npos);
        std::ranges::transform(name, name.begin(), [](char c) { return static_cast<char>(c - 'a' + 'A'); });
        CHECK(header.find("    GS_EXT_TYPE_" + name + " = " + std::to_string(index + 1) + ",\n") != std::string::npos);
    }
    CHECK(header.find("A position on the phone picture as ratios: (0, 0) top left, (1, 1) bottom right.") !=
          std::string::npos);
    CHECK(header.find("A color written \"#RRGGBB\", such as \"#F5C542\".") != std::string::npos);
    for (const std::string_view structure : {"gs_ext_value", "gs_ext_result", "gs_ext_contact", "gs_ext_touch_frame",
                                             "gs_ext_touch_method", "gs_ext_node", "gs_ext_host", "gs_ext_module"}) {
        CAPTURE(structure);
        const std::string name(structure);
        const std::size_t declared = header.find("typedef struct " + name + " " + name + ";\n");
        const std::size_t defined = header.find("struct " + name + " {\n");
        CHECK(declared != std::string::npos);
        CHECK(defined != std::string::npos);
        CHECK(declared < defined);
    }
    CHECK(header.ends_with("#ifdef __cplusplus\n}\n#endif\n\n#endif\n"));
}

TEST_CASE("ADR-0105: attachments and behaviors are appended to the module, so DLLs built before them still load")
{
    const std::string header = generatedHeader();
    CHECK(header.find("#define GS_EXT_MOST_FINGERS 10\n") != std::string::npos);
    for (const std::string_view structure :
         {"gs_ext_input_event", "gs_ext_touch_command", "gs_ext_output", "gs_ext_attachment", "gs_ext_behavior"}) {
        CAPTURE(structure);
        const std::string name(structure);
        const std::size_t declared = header.find("typedef struct " + name + " " + name + ";\n");
        const std::size_t defined = header.find("struct " + name + " {\n");
        CHECK(declared != std::string::npos);
        CHECK(defined != std::string::npos);
        CHECK(declared < defined);
    }
    const std::size_t module = header.find("struct gs_ext_module {\n");
    REQUIRE(module != std::string::npos);
    const std::size_t unload = header.find("    void (*unload)(void* context);\n", module);
    const std::size_t attachments = header.find("    const gs_ext_attachment* attachments;\n", module);
    const std::size_t behaviors = header.find("    const gs_ext_behavior* behaviors;\n};\n", module);
    CHECK(unload != std::string::npos);
    CHECK(unload < attachments);
    CHECK(attachments < behaviors);
    CHECK(behaviors != std::string::npos);
    CHECK(header.find("    GS_EXT_COMMAND_UP = 2,\n") != std::string::npos);
    CHECK(header.find("    GS_EXT_SLOT_AXIS1 = 2,\n") != std::string::npos);
    CHECK(header.find("    GS_EXT_INPUT_WHEEL = 5,\n") != std::string::npos);
}
