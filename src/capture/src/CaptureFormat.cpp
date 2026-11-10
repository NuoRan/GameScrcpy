#include "gs/capture/CaptureFormat.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <system_error>

namespace gs::capture {

namespace {

constexpr double kSmoothRate = 59.9;
constexpr double kRateSlack = 0.05;
constexpr int kHdLong = 1920;
constexpr int kHdShort = 1080;
constexpr double kRateScale = 1000.0;

struct Named {
    Subtype subtype;
    std::string_view name;
};

constexpr std::array<Named, 4> kNames{{
    {Subtype::Nv12, "NV12"},
    {Subtype::Yuy2, "YUY2"},
    {Subtype::Mjpg, "MJPG"},
    {Subtype::H264, "H264"},
}};

int subtypeRank(Subtype subtype) noexcept
{
    switch (subtype) {
    case Subtype::Nv12:
        return 4;
    case Subtype::Yuy2:
        return 3;
    case Subtype::Mjpg:
        return 2;
    case Subtype::H264:
        return 1;
    case Subtype::Other:
        break;
    }
    return 0;
}

bool fitsHd(const CaptureFormat& format) noexcept
{
    return std::max(format.width, format.height) <= kHdLong && std::min(format.width, format.height) <= kHdShort;
}

long long areaOf(const CaptureFormat& format) noexcept
{
    return static_cast<long long>(format.width) * static_cast<long long>(format.height);
}

bool better(const CaptureFormat& a, const CaptureFormat& b) noexcept
{
    const double rateA = a.framesPerSecond();
    const double rateB = b.framesPerSecond();
    const bool smoothA = rateA >= kSmoothRate;
    const bool smoothB = rateB >= kSmoothRate;
    if (smoothA != smoothB) {
        return smoothA;
    }
    if (!smoothA && std::abs(rateA - rateB) > kRateSlack) {
        return rateA > rateB;
    }
    const bool fitsA = fitsHd(a);
    const bool fitsB = fitsHd(b);
    if (fitsA != fitsB) {
        return fitsA;
    }
    if (areaOf(a) != areaOf(b)) {
        return fitsA ? areaOf(a) > areaOf(b) : areaOf(a) < areaOf(b);
    }
    if (subtypeRank(a.subtype) != subtypeRank(b.subtype)) {
        return subtypeRank(a.subtype) > subtypeRank(b.subtype);
    }
    if (smoothA && std::abs(rateA - rateB) > kRateSlack) {
        return rateA < rateB;
    }
    return false;
}

std::string rateText(double rate)
{
    std::string text = std::format("{:.2f}", std::round(rate * 100.0) / 100.0);
    while (!text.empty() && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text;
}

template <typename T>
bool parseNumber(std::string_view text, T& value)
{
    const char* const end = text.data() + text.size();
    const auto [stop, error] = std::from_chars(text.data(), end, value);
    return error == std::errc() && stop == end;
}

}

double CaptureFormat::framesPerSecond() const noexcept
{
    return rateDenominator == 0 ? 0.0 : static_cast<double>(rateNumerator) / static_cast<double>(rateDenominator);
}

std::string_view subtypeName(Subtype subtype) noexcept
{
    for (const Named& named : kNames) {
        if (named.subtype == subtype) {
            return named.name;
        }
    }
    return "OTHER";
}

std::optional<Subtype> subtypeOfName(std::string_view name) noexcept
{
    for (const Named& named : kNames) {
        if (named.name.size() == name.size() &&
            std::equal(named.name.begin(), named.name.end(), name.begin(), [](char a, char b) {
                return a == (b >= 'a' && b <= 'z' ? static_cast<char>(b - 'a' + 'A') : b);
            })) {
            return named.subtype;
        }
    }
    return std::nullopt;
}

bool decodable(Subtype subtype) noexcept
{
    return subtype != Subtype::Other;
}

std::string formatName(const CaptureFormat& format)
{
    return std::format("{}x{}@{}/{}", format.width, format.height, rateText(format.framesPerSecond()),
                       subtypeName(format.subtype));
}

std::optional<CaptureFormat> parseFormatName(std::string_view name)
{
    const std::size_t by = name.find('x');
    const std::size_t at = name.find('@');
    const std::size_t slash = name.find('/');
    if (by == std::string_view::npos || at == std::string_view::npos || slash == std::string_view::npos ||
        !(by < at && at < slash)) {
        return std::nullopt;
    }
    int width = 0;
    int height = 0;
    double rate = 0.0;
    if (!parseNumber(name.substr(0, by), width) || !parseNumber(name.substr(by + 1, at - by - 1), height) ||
        !parseNumber(name.substr(at + 1, slash - at - 1), rate)) {
        return std::nullopt;
    }
    const std::optional<Subtype> subtype = subtypeOfName(name.substr(slash + 1));
    if (!subtype || width <= 0 || height <= 0 || !(rate > 0.0) || rate > 1000.0) {
        return std::nullopt;
    }
    return CaptureFormat{.width = width,
                         .height = height,
                         .rateNumerator = static_cast<std::uint32_t>(std::llround(rate * kRateScale)),
                         .rateDenominator = static_cast<std::uint32_t>(kRateScale),
                         .subtype = *subtype};
}

bool sameMode(const CaptureFormat& a, const CaptureFormat& b) noexcept
{
    return a.width == b.width && a.height == b.height && a.subtype == b.subtype &&
           std::abs(a.framesPerSecond() - b.framesPerSecond()) <= kRateSlack;
}

std::optional<CaptureFormat> chooseFormat(std::span<const CaptureFormat> formats, std::string_view manual)
{
    if (!manual.empty()) {
        if (const std::optional<CaptureFormat> wanted = parseFormatName(manual)) {
            const auto found = std::ranges::find_if(formats, [&](const CaptureFormat& format) {
                return sameMode(format, *wanted);
            });
            if (found != formats.end()) {
                return *found;
            }
        }
    }
    std::optional<CaptureFormat> best;
    for (const CaptureFormat& format : formats) {
        if (!decodable(format.subtype) || format.width <= 0 || format.height <= 0 ||
            !(format.framesPerSecond() > 0.0)) {
            continue;
        }
        if (!best || better(format, *best)) {
            best = format;
        }
    }
    return best;
}

}
