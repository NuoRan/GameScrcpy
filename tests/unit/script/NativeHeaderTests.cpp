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
    CHECK(header.find("#define GS_EXT_ABI_VERSION 2\n") != std::string::npos);
    CHECK(header.find("extern \"C\" {") != std::string::npos);
    CHECK(header.find("GS_EXT_EXPORT int32_t gs_ext_main(const gs_ext_host* host, const gs_ext_plugin** plugin);\n") !=
          std::string::npos);
    CHECK(header.find("gs_ext_entry(") == std::string::npos);
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
                                             "gs_ext_touch_method", "gs_ext_node", "gs_ext_host", "gs_ext_contribution",
                                             "gs_ext_plugin"}) {
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

TEST_CASE("ADR-0112: every struct a DLL fills in or that holds arrays starts with its size, and the contribution "
          "kinds are named")
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
    for (const std::string_view structure :
         {"gs_ext_touch_frame", "gs_ext_touch_open", "gs_ext_touch_method", "gs_ext_node", "gs_ext_output",
          "gs_ext_attachment", "gs_ext_behavior", "gs_ext_host", "gs_ext_contribution", "gs_ext_plugin"}) {
        CAPTURE(structure);
        const std::size_t defined = header.find("struct " + std::string(structure) + " {\n");
        REQUIRE(defined != std::string::npos);
        const std::size_t firstField = header.find(";\n", defined);
        const std::size_t size = header.find("    uint32_t size;\n", defined);
        CHECK(size != std::string::npos);
        CHECK(size + std::string_view("    uint32_t size").size() == firstField);
    }
    for (const std::string_view frozen : {"gs_ext_value", "gs_ext_contact", "gs_ext_key", "gs_ext_input_event",
                                          "gs_ext_touch_command"}) {
        CAPTURE(frozen);
        const std::size_t defined = header.find("struct " + std::string(frozen) + " {\n");
        REQUIRE(defined != std::string::npos);
        const std::size_t doc = header.rfind("/*", defined);
        CHECK(header.substr(doc, defined - doc).find("never changes in later versions") != std::string::npos);
    }
    CHECK(header.find("#define GS_EXT_KIND_TOUCH_METHOD \"touch-method\"\n") != std::string::npos);
    CHECK(header.find("#define GS_EXT_KIND_BEHAVIOR \"behavior\"\n") != std::string::npos);
    CHECK(header.find("struct gs_ext_module {\n") == std::string::npos);
    CHECK(header.find("    GS_EXT_COMMAND_UP = 2,\n") != std::string::npos);
    CHECK(header.find("    GS_EXT_SLOT_AXIS1 = 2,\n") != std::string::npos);
    CHECK(header.find("    GS_EXT_INPUT_WHEEL = 5,\n") != std::string::npos);
}

TEST_CASE("ADR-0112: the C header offers input sources: their contribution kind, a struct that starts with its size "
          "and input_event at the end of the host table")
{
    const std::string header = generatedHeader();
    CHECK(header.find("#define GS_EXT_KIND_INPUT_SOURCE \"input-source\"\n") != std::string::npos);
    const std::size_t declared = header.find("typedef struct gs_ext_input_source gs_ext_input_source;\n");
    const std::size_t defined = header.find("struct gs_ext_input_source {\n");
    REQUIRE(declared != std::string::npos);
    REQUIRE(defined != std::string::npos);
    CHECK(declared < defined);
    const std::size_t firstField = header.find(";\n", defined);
    const std::size_t size = header.find("    uint32_t size;\n", defined);
    CHECK(size + std::string_view("    uint32_t size").size() == firstField);
    CHECK(header.find("    const char* name;\n", defined) < header.find("};\n", defined));

    const std::size_t host = header.find("struct gs_ext_host {\n");
    REQUIRE(host != std::string::npos);
    const std::size_t hostEnd = header.find("};\n", host);
    const std::size_t release = header.find("    int32_t (*release)(void* context, const char* resource, size_t size);\n",
                                            host);
    const std::size_t inputEvent = header.find(
        "    int32_t (*input_event)(void* context, const char* source, size_t source_size, const char* slot, "
        "size_t slot_size, double x, double y, uint64_t time_us);\n",
        host);
    REQUIRE(release != std::string::npos);
    REQUIRE(inputEvent != std::string::npos);
    CHECK(release < inputEvent);
    CHECK(inputEvent < hostEnd);
    CHECK(header.find("input.provide", host) < hostEnd);
}

TEST_CASE("ADR-0112: the media contributions, their pictures and sound and the host functions that carry them are in "
          "the C header, which stays plain ASCII")
{
    const std::string header = generatedHeader();
    for (const std::string_view structure :
         {"gs_ext_video_frame", "gs_ext_audio", "gs_ext_video_source", "gs_ext_audio_source", "gs_ext_sink_info",
          "gs_ext_media_sink", "gs_ext_recording_exporter"}) {
        CAPTURE(structure);
        const std::size_t defined = header.find("struct " + std::string(structure) + " {\n");
        REQUIRE(defined != std::string::npos);
        const std::size_t firstField = header.find(";\n", defined);
        const std::size_t size = header.find("    uint32_t size;\n", defined);
        CHECK(size + std::string_view("    uint32_t size").size() == firstField);
    }
    for (const std::string_view kind : {"#define GS_EXT_KIND_VIDEO_SOURCE \"video-source\"\n",
                                        "#define GS_EXT_KIND_AUDIO_SOURCE \"audio-source\"\n",
                                        "#define GS_EXT_KIND_MEDIA_SINK \"media-sink\"\n",
                                        "#define GS_EXT_KIND_RECORDING_EXPORTER \"recording-exporter\"\n",
                                        "#define GS_EXT_SOUND_RATE 48000\n", "    GS_EXT_PIXEL_BGRA = 1,\n",
                                        "    GS_EXT_PIXEL_NV12 = 2,\n", "    GS_EXT_SAMPLE_S16 = 1,\n",
                                        "    GS_EXT_SAMPLE_F32 = 2,\n"}) {
        CAPTURE(kind);
        CHECK(header.find(kind) != std::string::npos);
    }
    const std::size_t host = header.find("struct gs_ext_host {\n");
    REQUIRE(host != std::string::npos);
    const std::size_t end = header.find("};\n", host);
    std::size_t last = header.find("(*release)", host);
    for (const std::string_view function :
         {"(*sources_changed)", "(*video_frame)", "(*audio_samples)", "(*stream_ended)"}) {
        CAPTURE(function);
        const std::size_t at = header.find(function, host);
        CHECK(at < end);
        CHECK(at > last);
        last = at;
    }
    CHECK(std::ranges::all_of(header, [](char c) { return static_cast<unsigned char>(c) < 0x80; }));
}
