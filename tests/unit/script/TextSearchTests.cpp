#include "gs/script/TextSearch.h"

#include <gs/platform/TextReader.h>

#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace {

using gs::platform::TextBox;
using gs::platform::TextLine;
using gs::platform::TextResult;
using gs::platform::TextWord;
using gs::script::PictureSize;
using gs::script::TextQuery;

TextLine lineOf(std::vector<TextWord> words, std::string text)
{
    TextLine line{.text = std::move(text), .box = words.front().box, .words = {}};
    for (const TextWord& word : words) {
        const double right = std::max(line.box.x + line.box.width, word.box.x + word.box.width);
        const double bottom = std::max(line.box.y + line.box.height, word.box.y + word.box.height);
        line.box.x = std::min(line.box.x, word.box.x);
        line.box.y = std::min(line.box.y, word.box.y);
        line.box.width = right - line.box.x;
        line.box.height = bottom - line.box.y;
    }
    line.words = std::move(words);
    return line;
}

TextResult settingsPage()
{
    TextResult read;
    read.language = "en-US";
    read.lines.push_back(lineOf({{"Battery", {10, 10, 70, 20}}, {"saver", {90, 10, 50, 20}}}, "Battery saver"));
    read.lines.push_back(lineOf({{"Battery", {10, 50, 70, 20}}}, "Battery"));
    read.lines.push_back(lineOf({{"Use", {10, 90, 30, 20}}, {"USB", {50, 90, 40, 20}}, {"debugging", {100, 90, 90, 20}}},
                                "Use USB debugging"));
    read.lines.push_back(lineOf({{"电", {10, 130, 20, 20}}, {"池", {30, 130, 20, 20}}, {"设置", {60, 130, 40, 20}}},
                                "电池设置"));
    return read;
}

nlohmann::json found(std::string_view text, int left = 0, int top = 0)
{
    TextQuery query;
    query.find = true;
    query.text = std::string(text);
    const gs::script::VisionAnswer answer = gs::script::textAnswer(query, settingsPage(), left, top, PictureSize{200, 400});
    REQUIRE(answer.problem.empty());
    return nlohmann::json::parse(answer.json);
}

}

TEST_CASE("ADR-0093: findText prefers a line that says only the text, else the first line from the top that has it")
{
    const nlohmann::json exact = found("battery");
    REQUIRE(exact.is_object());
    CHECK(exact["x"].get<double>() == doctest::Approx(45.0 / 200.0));
    CHECK(exact["y"].get<double>() == doctest::Approx(60.0 / 400.0));
    CHECK(exact["width"].get<double>() == doctest::Approx(70.0 / 200.0));
    CHECK(exact["score"].get<double>() == 1.0);

    const nlohmann::json saver = found("SAVER");
    CHECK(saver["x"].get<double>() == doctest::Approx(115.0 / 200.0));
    CHECK(saver["y"].get<double>() == doctest::Approx(20.0 / 400.0));

    const nlohmann::json usb = found("usbdebug");
    CHECK(usb["x"].get<double>() == doctest::Approx(((50.0 + 190.0) / 2.0) / 200.0));
    CHECK(usb["width"].get<double>() == doctest::Approx(140.0 / 200.0));

    const nlohmann::json shifted = found("USB  debugging", 20, 100);
    CHECK(shifted["x"].get<double>() == doctest::Approx((20.0 + 120.0) / 200.0));
    CHECK(shifted["y"].get<double>() == doctest::Approx((100.0 + 100.0) / 400.0));

    const nlohmann::json chinese = found("池 设");
    CHECK(chinese["x"].get<double>() == doctest::Approx(((30.0 + 100.0) / 2.0) / 200.0));

    CHECK(found("Wi-Fi").is_null());
    CHECK(found("\xE3\x80\x80").is_null());
}

TEST_CASE("ADR-0093: readText gives every line top to bottom with its place and the language it read")
{
    TextQuery query;
    const gs::script::VisionAnswer answer = gs::script::textAnswer(query, settingsPage(), 0, 200, PictureSize{200, 400});
    REQUIRE(answer.problem.empty());
    const nlohmann::json read = nlohmann::json::parse(answer.json);
    CHECK(read["text"] == "Battery saver\nBattery\nUse USB debugging\n电池设置");
    CHECK(read["language"] == "en-US");
    REQUIRE(read["lines"].size() == 4);
    CHECK(read["lines"][2]["text"] == "Use USB debugging");
    CHECK(read["lines"][2]["x"].get<double>() == doctest::Approx(100.0 / 200.0));
    CHECK(read["lines"][2]["y"].get<double>() == doctest::Approx(300.0 / 400.0));
    CHECK(read["lines"][2]["height"].get<double>() == doctest::Approx(20.0 / 400.0));

    const gs::script::VisionAnswer none = gs::script::textAnswer(query, TextResult{.language = "zh-Hans-CN", .lines = {}},
                                                                 0, 0, PictureSize{200, 400});
    CHECK(nlohmann::json::parse(none.json) == nlohmann::json::parse(R"({"text":"","lines":[],"language":"zh-Hans-CN"})"));
}

TEST_CASE("ADR-0093: folded text drops spaces of every width and lowers English letters only")
{
    CHECK(gs::script::foldedText(" Use\tUSB \n Debugging ") == "useusbdebugging");
    CHECK(gs::script::foldedText("电池\xE3\x80\x80设置\xC2\xA0OK") == "电池设置ok");
    CHECK(gs::script::foldedText("   ").empty());
}

TEST_CASE("ADR-0093: without a language, findText reads English words with English recognition and other text "
          "with the Windows language")
{
    TextQuery query;
    CHECK(gs::script::preferredLanguage(query).empty());
    query.find = true;
    query.text = "Search settings 2";
    CHECK(gs::script::preferredLanguage(query) == "en");
    query.text = "设置";
    CHECK(gs::script::preferredLanguage(query).empty());
    query.text = "第1关";
    CHECK(gs::script::preferredLanguage(query).empty());
    query.language = "ja";
    CHECK(gs::script::preferredLanguage(query) == "ja");
    query.find = false;
    CHECK(gs::script::preferredLanguage(query) == "ja");
}
