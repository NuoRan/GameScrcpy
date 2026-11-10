#include <gs/capture/AudioPairing.h>
#include <gs/capture/CaptureDevices.h>

#include <doctest/doctest.h>

#include <optional>
#include <string>
#include <vector>

using gs::capture::AudioEndpoint;

namespace {

AudioEndpoint endpoint(const wchar_t* id, const char* name, const wchar_t* container)
{
    return {.id = id, .name = name, .container = container};
}

const std::vector<AudioEndpoint> kEndpoints{
    endpoint(L"{0.0.1.00000000}.{built-in}", "Microphone Array", L"{00000000-0000-0000-FFFF-FFFFFFFFFFFF}"),
    endpoint(L"{0.0.1.00000000}.{headset}", "Headset Microphone", L"{5D2A1C33-8F51-4C1E-9A11-0B7C3E19D402}"),
    endpoint(L"{0.0.1.00000000}.{card}", "Digital Audio Interface (USB3 Video)", L"{A1B2C3D4-E5F6-4711-8899-AABBCCDDEEFF}"),
    endpoint(L"{0.0.1.00000000}.{unknown}", "Line In", L""),
};

}

TEST_CASE("a capture card's sound device is the one in the same device container, however the ID is written (MED-014)")
{
    CHECK(gs::capture::pairedEndpoint(L"{A1B2C3D4-E5F6-4711-8899-AABBCCDDEEFF}", kEndpoints) == std::optional<std::size_t>(2));
    CHECK(gs::capture::pairedEndpoint(L"{a1b2c3d4-e5f6-4711-8899-aabbccddeeff}", kEndpoints) == std::optional<std::size_t>(2));
    CHECK(gs::capture::pairedEndpoint(L"a1b2c3d4-e5f6-4711-8899-aabbccddeeff", kEndpoints) == std::optional<std::size_t>(2));
    CHECK(gs::capture::pairedEndpoint(L"{5D2A1C33-8F51-4C1E-9A11-0B7C3E19D402}", kEndpoints) == std::optional<std::size_t>(1));
}

TEST_CASE("a card without a container of its own, or with the container all built-in devices share, has no sound")
{
    CHECK_FALSE(gs::capture::pairedEndpoint(L"", kEndpoints));
    CHECK_FALSE(gs::capture::pairedEndpoint(L"{}", kEndpoints));
    CHECK_FALSE(gs::capture::pairedEndpoint(L"{00000000-0000-0000-FFFF-FFFFFFFFFFFF}", kEndpoints));
    CHECK_FALSE(gs::capture::pairedEndpoint(L"{00000000-0000-0000-0000-000000000000}",
                                            std::vector{endpoint(L"x", "Zero", L"{00000000-0000-0000-0000-000000000000}")}));
    CHECK_FALSE(gs::capture::pairedEndpoint(L"{0F0F0F0F-0000-1111-2222-333344445555}", kEndpoints));
    CHECK_FALSE(gs::capture::pairedEndpoint(L"{A1B2C3D4-E5F6-4711-8899-AABBCCDDEEFF}", std::vector<AudioEndpoint>{}));
}

TEST_CASE("container IDs are the same with or without braces and in any case, and never when empty")
{
    CHECK(gs::capture::sameContainer(L"{A1B2C3D4-E5F6-4711-8899-AABBCCDDEEFF}", L"a1b2c3d4-e5f6-4711-8899-aabbccddeeff"));
    CHECK_FALSE(gs::capture::sameContainer(L"{A1B2C3D4-E5F6-4711-8899-AABBCCDDEEFF}", L"{A1B2C3D4-E5F6-4711-8899-AABBCCDDEEF0}"));
    CHECK_FALSE(gs::capture::sameContainer(L"{A1B2}", L"{A1B2C3}"));
    CHECK_FALSE(gs::capture::sameContainer(L"", L""));
    CHECK_FALSE(gs::capture::sameContainer(L"{}", L"{}"));
}

TEST_CASE("a camera built into the screen bezel is no capture card, and the short device hash ignores case")
{
    CHECK(gs::capture::builtInCamera({.link = L"\\\\?\\usb#vid_0bda&pid_5634", .name = "Integrated Camera",
                                      .panelLocated = true, .removable = false}));
    CHECK_FALSE(gs::capture::builtInCamera({.link = L"\\\\?\\usb#vid_534d&pid_2109", .name = "USB3 Video",
                                            .panelLocated = false, .removable = true}));
    CHECK_FALSE(gs::capture::builtInCamera({.link = L"\\\\?\\usb#vid_046d&pid_085c", .name = "C922 Pro Stream Webcam",
                                            .panelLocated = true, .removable = true}));
    const std::string hash = gs::capture::shortHash(L"\\\\?\\USB#VID_534D&PID_2109#5&1A2B3C4D&0&2");
    CHECK(hash.size() == 8);
    CHECK(hash.find_first_not_of("0123456789abcdef") == std::string::npos);
    CHECK(hash == gs::capture::shortHash(L"\\\\?\\usb#vid_534d&pid_2109#5&1a2b3c4d&0&2"));
    CHECK(hash != gs::capture::shortHash(L"\\\\?\\usb#vid_534d&pid_2109#5&1a2b3c4d&0&3"));
}
