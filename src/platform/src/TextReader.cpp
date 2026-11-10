#include "gs/platform/TextReader.h"

#include <objbase.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <thread>
#include <utility>

namespace gs::platform {

namespace {

namespace ocr = winrt::Windows::Media::Ocr;
namespace imaging = winrt::Windows::Graphics::Imaging;
using winrt::Windows::Globalization::Language;

constexpr int kSmallSide = 300;
constexpr int kSmallLongSide = 1600;
constexpr double kMostEnlargement = 4.0;

std::string lowered(std::string_view text)
{
    std::string out(text);
    std::ranges::transform(out, out.begin(),
                           [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    return out;
}

bool joinMultithreaded()
{
    thread_local bool joined = false;
    if (!joined) {
        joined = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    }
    return joined;
}

template <typename Result>
Result multithreaded(const std::function<Result()>& work, Result fallback)
{
    if (joinMultithreaded()) {
        return work();
    }
    std::optional<Result> made;
    std::thread([&made, &work] {
        if (joinMultithreaded()) {
            made.emplace(work());
        }
    }).join();
    return made ? std::move(*made) : std::move(fallback);
}

ocr::OcrEngine created(std::string_view language)
{
    if (language.empty()) {
        if (ocr::OcrEngine engine = ocr::OcrEngine::TryCreateFromUserProfileLanguages()) {
            return engine;
        }
        const auto available = ocr::OcrEngine::AvailableRecognizerLanguages();
        return available.Size() > 0 ? ocr::OcrEngine::TryCreateFromLanguage(available.GetAt(0)) : ocr::OcrEngine{nullptr};
    }
    const std::string wanted = lowered(language);
    for (const Language& each : ocr::OcrEngine::AvailableRecognizerLanguages()) {
        const std::string tag = lowered(winrt::to_string(each.LanguageTag()));
        if (tag == wanted || tag.starts_with(wanted + "-")) {
            return ocr::OcrEngine::TryCreateFromLanguage(each);
        }
    }
    try {
        const Language asked(winrt::to_hstring(language));
        if (ocr::OcrEngine::IsLanguageSupported(asked)) {
            return ocr::OcrEngine::TryCreateFromLanguage(asked);
        }
    } catch (const winrt::hresult_error&) {
        return nullptr;
    }
    return nullptr;
}

ocr::OcrEngine engineFor(std::string_view language)
{
    thread_local std::map<std::string, ocr::OcrEngine, std::less<>> engines;
    if (const auto found = engines.find(language); found != engines.end()) {
        return found->second;
    }
    ocr::OcrEngine engine = created(language);
    if (engine) {
        engines.emplace(std::string(language), engine);
    }
    return engine;
}

std::vector<std::uint8_t> bgraOf(const std::uint8_t* grey, int width, int height, std::ptrdiff_t stride, int outWidth,
                                 int outHeight)
{
    std::vector<std::uint8_t> out(static_cast<std::size_t>(outWidth) * static_cast<std::size_t>(outHeight) * 4);
    const double stepX = static_cast<double>(width) / outWidth;
    const double stepY = static_cast<double>(height) / outHeight;
    for (int y = 0; y < outHeight; ++y) {
        const double fromY = std::clamp((y + 0.5) * stepY - 0.5, 0.0, height - 1.0);
        const int y0 = static_cast<int>(fromY);
        const int y1 = std::min(y0 + 1, height - 1);
        const double partY = fromY - y0;
        const std::uint8_t* upper = grey + static_cast<std::ptrdiff_t>(y0) * stride;
        const std::uint8_t* lower = grey + static_cast<std::ptrdiff_t>(y1) * stride;
        std::uint8_t* row = out.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(outWidth) * 4;
        for (int x = 0; x < outWidth; ++x) {
            const double fromX = std::clamp((x + 0.5) * stepX - 0.5, 0.0, width - 1.0);
            const int x0 = static_cast<int>(fromX);
            const int x1 = std::min(x0 + 1, width - 1);
            const double partX = fromX - x0;
            const double top = upper[x0] + (upper[x1] - upper[x0]) * partX;
            const double bottom = lower[x0] + (lower[x1] - lower[x0]) * partX;
            const auto value = static_cast<std::uint8_t>(std::clamp(std::lround(top + (bottom - top) * partY), 0L, 255L));
            std::uint8_t* pixel = row + static_cast<std::size_t>(x) * 4;
            pixel[0] = value;
            pixel[1] = value;
            pixel[2] = value;
            pixel[3] = 255;
        }
    }
    return out;
}

char32_t firstCodePoint(std::string_view text) noexcept
{
    if (text.empty()) {
        return 0;
    }
    const auto lead = static_cast<unsigned char>(text[0]);
    const std::size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
    char32_t point = length == 1 ? lead : length == 2 ? lead & 0x1Fu : length == 3 ? lead & 0x0Fu : lead & 0x07u;
    for (std::size_t at = 1; at < length && at < text.size(); ++at) {
        point = (point << 6) | (static_cast<unsigned char>(text[at]) & 0x3Fu);
    }
    return point;
}

char32_t lastCodePoint(std::string_view text) noexcept
{
    std::size_t start = text.size();
    while (start > 0) {
        --start;
        if ((static_cast<unsigned char>(text[start]) & 0xC0u) != 0x80u) {
            break;
        }
    }
    return firstCodePoint(text.substr(start));
}

TextBox boxOf(const winrt::Windows::Foundation::Rect& rect, double backX, double backY)
{
    return {rect.X * backX, rect.Y * backY, rect.Width * backX, rect.Height * backY};
}

TextBox joined(const TextBox& one, const TextBox& other)
{
    const double left = std::min(one.x, other.x);
    const double top = std::min(one.y, other.y);
    const double right = std::max(one.x + one.width, other.x + other.width);
    const double bottom = std::max(one.y + one.height, other.y + other.height);
    return {left, top, right - left, bottom - top};
}

Expected<TextResult, TextProblem> recognized(const std::uint8_t* grey, int width, int height, std::ptrdiff_t stride,
                                             std::string_view language)
{
    const ocr::OcrEngine engine = engineFor(language);
    if (!engine) {
        return makeUnexpected(TextProblem::NoLanguage);
    }
    TextResult result;
    result.language = winrt::to_string(engine.RecognizerLanguage().LanguageTag());
    if (grey == nullptr || width <= 0 || height <= 0) {
        return result;
    }
    const double wanted = std::max(static_cast<double>(kSmallSide) / std::min(width, height),
                                   static_cast<double>(kSmallLongSide) / std::max(width, height));
    const double scale = std::min(std::clamp(wanted, 1.0, kMostEnlargement),
                                  static_cast<double>(ocr::OcrEngine::MaxImageDimension()) / std::max(width, height));
    const int outWidth = std::max(1, static_cast<int>(std::lround(width * scale)));
    const int outHeight = std::max(1, static_cast<int>(std::lround(height * scale)));
    const std::vector<std::uint8_t> pixels = bgraOf(grey, width, height, stride, outWidth, outHeight);
    const auto buffer = winrt::Windows::Security::Cryptography::CryptographicBuffer::CreateFromByteArray(pixels);
    const imaging::SoftwareBitmap bitmap = imaging::SoftwareBitmap::CreateCopyFromBuffer(
        buffer, imaging::BitmapPixelFormat::Bgra8, outWidth, outHeight, imaging::BitmapAlphaMode::Ignore);
    const ocr::OcrResult found = engine.RecognizeAsync(bitmap).get();
    const double backX = static_cast<double>(width) / outWidth;
    const double backY = static_cast<double>(height) / outHeight;
    for (const ocr::OcrLine& line : found.Lines()) {
        TextLine made;
        for (const ocr::OcrWord& word : line.Words()) {
            std::string piece = winrt::to_string(word.Text());
            if (piece.empty()) {
                continue;
            }
            const TextBox box = boxOf(word.BoundingRect(), backX, backY);
            if (made.words.empty()) {
                made.box = box;
            } else {
                if (joinsWithSpace(lastCodePoint(made.text), firstCodePoint(piece))) {
                    made.text += ' ';
                }
                made.box = joined(made.box, box);
            }
            made.text += piece;
            made.words.push_back({std::move(piece), box});
        }
        if (!made.words.empty()) {
            result.lines.push_back(std::move(made));
        }
    }
    return result;
}

}

bool joinsWithSpace(char32_t before, char32_t after) noexcept
{
    const auto wide = [](char32_t point) {
        return (point >= 0x2E80 && point <= 0x9FFF) || (point >= 0xF900 && point <= 0xFAFF)
               || (point >= 0xFE30 && point <= 0xFE4F) || (point >= 0xFF00 && point <= 0xFFEF)
               || (point >= 0x20000 && point <= 0x3FFFF);
    };
    return before != 0 && after != 0 && !wide(before) && !wide(after);
}

Expected<TextResult, TextProblem> readText(const std::uint8_t* grey, int width, int height, std::ptrdiff_t stride,
                                           std::string_view language)
{
    using Answer = Expected<TextResult, TextProblem>;
    return multithreaded<Answer>(
        [&]() -> Answer {
            try {
                return recognized(grey, width, height, stride, language);
            } catch (...) {
                return makeUnexpected(TextProblem::Failed);
            }
        },
        makeUnexpected(TextProblem::Failed));
}

std::vector<std::string> textLanguages()
{
    using Tags = std::vector<std::string>;
    return multithreaded<Tags>(
        []() -> Tags {
            Tags tags;
            try {
                for (const Language& each : ocr::OcrEngine::AvailableRecognizerLanguages()) {
                    tags.push_back(winrt::to_string(each.LanguageTag()));
                }
            } catch (...) {
                tags.clear();
            }
            return tags;
        },
        Tags{});
}

}
