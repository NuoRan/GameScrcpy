// third_party/ffmpeg is the build ADR-0046 describes: LGPL only, every decoder the client uses,
// and D3D11VA for the video ones.
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libswresample/swresample.h>
}

#include <doctest/doctest.h>

#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::string> buildInfoFlags()
{
    std::ifstream in(GS_FFMPEG_BUILDINFO);
    std::vector<std::string> flags;
    bool inFlags = false;
    bool inBlock = false;
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.starts_with("FFmpeg configure flags")) {
            inFlags = true;
        } else if (inFlags && line.starts_with("```")) {
            if (inBlock) {
                break;
            }
            inBlock = true;
        } else if (inBlock && !line.empty()) {
            flags.push_back(line);
        }
    }
    return flags;
}

bool hasD3d11va(const AVCodec& codec)
{
    for (int i = 0;; ++i) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(&codec, i);
        if (config == nullptr) {
            return false;
        }
        if (config->device_type == AV_HWDEVICE_TYPE_D3D11VA &&
            (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) != 0) {
            return true;
        }
    }
}

}

TEST_CASE("ffmpeg: LGPL only, nothing GPL or nonfree (ADR-0015)")
{
    CHECK(std::string_view(avcodec_license()).starts_with("LGPL"));
    CHECK(std::string_view(avutil_license()).starts_with("LGPL"));
    CHECK(std::string_view(swresample_license()).starts_with("LGPL"));
    const std::string_view configuration = avcodec_configuration();
    CHECK(configuration.find("--enable-gpl") == std::string_view::npos);
    CHECK(configuration.find("--enable-nonfree") == std::string_view::npos);
}

TEST_CASE("ffmpeg: the shipped libraries are the LGPL 2.1 build BUILDINFO.md and the license list describe (REL-002)")
{
    const std::vector<std::string> flags = buildInfoFlags();
    REQUIRE(flags.size() > 10);
    std::string configuration = avcodec_configuration();
    std::erase(configuration, '\'');
    for (const std::string& flag : flags) {
        CAPTURE(flag);
        CHECK(configuration.find(flag) != std::string::npos);
    }
    CHECK(configuration.find("--enable-version3") == std::string::npos);
    CHECK(std::string_view(avcodec_license()) == "LGPL version 2.1 or later");
    CHECK(std::string_view(avutil_license()) == "LGPL version 2.1 or later");
    CHECK(std::string_view(swresample_license()) == "LGPL version 2.1 or later");
}

TEST_CASE("ffmpeg: the decoders the client uses (ADR-0046)")
{
    for (const char* name : {"h264", "hevc", "av1", "libdav1d", "opus", "aac"}) {
        CAPTURE(name);
        CHECK(avcodec_find_decoder_by_name(name) != nullptr);
    }
    const AVCodec* av1 = avcodec_find_decoder(AV_CODEC_ID_AV1);
    REQUIRE(av1 != nullptr);
    CHECK(std::string_view(av1->name) == "libdav1d");
}

TEST_CASE("ffmpeg: D3D11VA for every video decoder (PLAN 4.4)")
{
    CHECK(av_hwdevice_find_type_by_name("d3d11va") == AV_HWDEVICE_TYPE_D3D11VA);
    for (const char* name : {"h264", "hevc", "av1"}) {
        CAPTURE(name);
        const AVCodec* codec = avcodec_find_decoder_by_name(name);
        REQUIRE(codec != nullptr);
        CHECK(hasD3d11va(*codec));
    }
}
