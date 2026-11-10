#include <gs/platform/TextReader.h>

#include <doctest/doctest.h>

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using gs::platform::readText;
using gs::platform::TextLine;
using gs::platform::TextProblem;
using gs::platform::TextResult;

struct Picture {
    std::vector<std::uint8_t> grey;
    int width = 0;
    int height = 0;
    std::ptrdiff_t stride = 0;
};

Picture drawn(std::wstring_view text, int width, int height, int left, int top, int size, const wchar_t* face,
              int padding = 0)
{
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC dc = CreateCompatibleDC(nullptr);
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ previousBitmap = SelectObject(dc, bitmap);
    RECT whole{0, 0, width, height};
    FillRect(dc, &whole, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    HFONT font = CreateFontW(-size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, face);
    HGDIOBJ previousFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(0, 0, 0));
    RECT place{left, top, width, height};
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &place, DT_LEFT | DT_TOP | DT_NOPREFIX);
    GdiFlush();
    Picture picture{.grey = {}, .width = width, .height = height, .stride = width + padding};
    picture.grey.assign(static_cast<std::size_t>(picture.stride) * static_cast<std::size_t>(height), 0);
    const auto* pixels = static_cast<const std::uint8_t*>(bits);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* pixel = pixels + (static_cast<std::size_t>(y) * width + x) * 4;
            picture.grey[static_cast<std::size_t>(y) * picture.stride + x] =
                static_cast<std::uint8_t>((pixel[0] + pixel[1] + pixel[2]) / 3);
        }
    }
    SelectObject(dc, previousFont);
    DeleteObject(font);
    SelectObject(dc, previousBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return picture;
}

std::optional<std::string> installed(std::string_view prefix)
{
    for (const std::string& tag : gs::platform::textLanguages()) {
        if (tag.starts_with(prefix)) {
            return tag;
        }
    }
    return std::nullopt;
}

std::string squeezed(std::string_view text)
{
    std::string out;
    for (const char c : text) {
        if (c != ' ') {
            out += c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        }
    }
    return out;
}

gs::Expected<TextResult, TextProblem> seen(const Picture& picture, std::string_view language)
{
    return readText(picture.grey.data(), picture.width, picture.height, picture.stride, language);
}

}

TEST_CASE("ADR-0093: Windows reads an English line with its place, and each word with its own")
{
    const std::optional<std::string> english = installed("en");
    if (!english) {
        MESSAGE("no English text recognition on this computer");
        return;
    }
    const Picture picture = drawn(L"GameScrcpy 123", 640, 160, 40, 50, 36, L"Segoe UI", 7);
    const auto read = seen(picture, "en");
    REQUIRE(read.has_value());
    CHECK(read->language == *english);
    REQUIRE(read->lines.size() == 1);
    const TextLine& line = read->lines.front();
    CHECK(squeezed(line.text) == "gamescrcpy123");
    CHECK(line.text.find(' ') != std::string::npos);
    CHECK(line.box.x == doctest::Approx(40.0).epsilon(0.25));
    CHECK(line.box.y > 45.0);
    CHECK(line.box.y + line.box.height < 105.0);
    CHECK(line.box.width > 150.0);
    REQUIRE(line.words.size() == 2);
    CHECK(line.words[1].box.x > line.words[0].box.x + line.words[0].box.width);
}

TEST_CASE("ADR-0093: lines come top to bottom, small text is enlarged first, and an empty picture has none")
{
    if (!installed("en")) {
        MESSAGE("no English text recognition on this computer");
        return;
    }
    const auto two = seen(drawn(L"Battery\nSearch settings", 480, 200, 20, 20, 30, L"Segoe UI"), "en");
    REQUIRE(two.has_value());
    REQUIRE(two->lines.size() == 2);
    CHECK(squeezed(two->lines[0].text) == "battery");
    CHECK(squeezed(two->lines[1].text) == "searchsettings");
    CHECK(two->lines[0].box.y < two->lines[1].box.y);

    const Picture small = drawn(L"Level 42", 96, 24, 4, 4, 14, L"Segoe UI");
    const auto tiny = seen(small, "en");
    REQUIRE(tiny.has_value());
    REQUIRE(tiny->lines.size() == 1);
    CHECK(squeezed(tiny->lines[0].text) == "level42");
    CHECK(tiny->lines[0].box.x + tiny->lines[0].box.width <= 96.5);
    CHECK(tiny->lines[0].box.y + tiny->lines[0].box.height <= 24.5);

    const auto blank = seen(drawn(L"", 200, 100, 0, 0, 20, L"Segoe UI"), "en");
    REQUIRE(blank.has_value());
    CHECK(blank->lines.empty());
}

TEST_CASE("ADR-0093: Chinese words are joined without spaces")
{
    if (!installed("zh-Hans")) {
        MESSAGE("no Chinese text recognition on this computer");
        return;
    }
    const auto read = seen(drawn(L"电池 设置", 480, 140, 30, 40, 40, L"Microsoft YaHei"), "zh-Hans");
    REQUIRE(read.has_value());
    CHECK(read->language.starts_with("zh-Hans"));
    REQUIRE(read->lines.size() == 1);
    CHECK(read->lines[0].text == "电池设置");
}

TEST_CASE("ADR-0093: a language without text recognition is named as the problem, and the default picks one")
{
    const std::vector<std::string> languages = gs::platform::textLanguages();
    const Picture picture = drawn(L"Ready", 200, 80, 10, 10, 28, L"Segoe UI");
    const auto missing = seen(picture, "xx");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error() == TextProblem::NoLanguage);
    const auto chosen = seen(picture, "");
    if (languages.empty()) {
        REQUIRE_FALSE(chosen.has_value());
        CHECK(chosen.error() == TextProblem::NoLanguage);
        return;
    }
    REQUIRE(chosen.has_value());
    CHECK(std::ranges::find(languages, chosen->language) != languages.end());
}

TEST_CASE("ADR-0093: words are spaced only between two letters that are not Chinese, Japanese or full width")
{
    using gs::platform::joinsWithSpace;
    CHECK(joinsWithSpace(U'a', U'1'));
    CHECK(joinsWithSpace(U'%', U'B'));
    CHECK(joinsWithSpace(U'한', U'국'));
    CHECK_FALSE(joinsWithSpace(U'电', U'池'));
    CHECK_FALSE(joinsWithSpace(U'a', U'池'));
    CHECK_FALSE(joinsWithSpace(U'设', U'B'));
    CHECK_FALSE(joinsWithSpace(U'ア', U'イ'));
    CHECK_FALSE(joinsWithSpace(U'Ａ', U'b'));
    CHECK_FALSE(joinsWithSpace(0, U'b'));
}
