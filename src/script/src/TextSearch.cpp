#include "gs/script/TextSearch.h"

#include <gs/platform/TextReader.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gs::script {

namespace {

using Json = nlohmann::ordered_json;

struct Match {
    std::size_t line = 0;
    std::size_t first = 0;
    std::size_t last = 0;
};

std::optional<Match> matchIn(const platform::TextResult& read, std::string_view wanted)
{
    std::optional<Match> partial;
    if (wanted.empty()) {
        return partial;
    }
    for (std::size_t index = 0; index < read.lines.size(); ++index) {
        const platform::TextLine& line = read.lines[index];
        std::string folded;
        std::vector<std::size_t> owners;
        for (std::size_t word = 0; word < line.words.size(); ++word) {
            const std::string piece = foldedText(line.words[word].text);
            folded += piece;
            owners.insert(owners.end(), piece.size(), word);
        }
        if (folded.empty()) {
            continue;
        }
        if (folded == wanted) {
            return Match{.line = index, .first = owners.front(), .last = owners.back()};
        }
        if (const std::size_t at = folded.find(wanted); !partial && at != std::string::npos) {
            partial = Match{.line = index, .first = owners[at], .last = owners[at + wanted.size() - 1]};
        }
    }
    return partial;
}

platform::TextBox wordsBox(const platform::TextLine& line, std::size_t first, std::size_t last)
{
    platform::TextBox box = line.words[first].box;
    for (std::size_t word = first + 1; word <= last; ++word) {
        const platform::TextBox& next = line.words[word].box;
        const double right = std::max(box.x + box.width, next.x + next.width);
        const double bottom = std::max(box.y + box.height, next.y + next.height);
        box.x = std::min(box.x, next.x);
        box.y = std::min(box.y, next.y);
        box.width = right - box.x;
        box.height = bottom - box.y;
    }
    return box;
}

std::string problemOf(platform::TextProblem problem, std::string_view language)
{
    if (problem == platform::TextProblem::Failed) {
        return "GS-API-039 Windows could not read the text in the picture";
    }
    const std::vector<std::string> installed = platform::textLanguages();
    if (installed.empty()) {
        return "GS-API-039 this computer has no text recognition: add a language with optical character recognition "
               "in Windows Settings, Time & language, Language & region";
    }
    std::string have;
    for (const std::string& tag : installed) {
        have += (have.empty() ? "" : ", ") + tag;
    }
    return std::format("GS-API-039 this computer has no text recognition for \"{}\" (it has {}): add the language in "
                       "Windows Settings, Time & language, Language & region",
                       language, have);
}

}

VisionAnswer textAnswer(const TextQuery& query, const platform::TextResult& read, int left, int top,
                        PictureSize picture)
{
    const double width = picture.width > 0 ? picture.width : 1.0;
    const double height = picture.height > 0 ? picture.height : 1.0;
    const auto place = [&](const platform::TextBox& box, Json& into) {
        into["x"] = (left + box.x + box.width / 2.0) / width;
        into["y"] = (top + box.y + box.height / 2.0) / height;
        into["width"] = box.width / width;
        into["height"] = box.height / height;
    };
    if (query.find) {
        const std::optional<Match> match = matchIn(read, foldedText(query.text));
        if (!match) {
            return {.json = "null"};
        }
        Json found = Json::object();
        place(wordsBox(read.lines[match->line], match->first, match->last), found);
        found["score"] = 1;
        return {.json = found.dump()};
    }
    Json lines = Json::array();
    std::string all;
    bool first = true;
    for (const platform::TextLine& line : read.lines) {
        if (!std::exchange(first, false)) {
            all += '\n';
        }
        all += line.text;
        Json entry = Json::object();
        entry["text"] = line.text;
        place(line.box, entry);
        lines.push_back(std::move(entry));
    }
    Json made = Json::object();
    made["text"] = all;
    made["lines"] = std::move(lines);
    made["language"] = read.language;
    return {.json = made.dump(-1, ' ', false, Json::error_handler_t::replace)};
}

std::string_view preferredLanguage(const TextQuery& query) noexcept
{
    if (!query.language.empty() || !query.find) {
        return query.language;
    }
    const bool english = std::ranges::all_of(query.text, [](char c) { return static_cast<unsigned char>(c) < 0x80; });
    return english ? std::string_view("en") : std::string_view();
}

VisionAnswer answerText(const TextQuery& query, const GreyArea& area, PictureSize picture)
{
    const bool empty = area.plane == nullptr || area.width <= 0 || area.height <= 0;
    const std::uint8_t* start =
        empty ? nullptr : area.plane + static_cast<std::ptrdiff_t>(area.top) * area.stride + area.left;
    const int width = empty ? 0 : area.width;
    const int height = empty ? 0 : area.height;
    const std::string_view preferred = preferredLanguage(query);
    auto read = platform::readText(start, width, height, area.stride, preferred);
    if (!read && read.error() == platform::TextProblem::NoLanguage && preferred != query.language) {
        read = platform::readText(start, width, height, area.stride, query.language);
    }
    if (!read) {
        return {.problem = problemOf(read.error(), query.language)};
    }
    return textAnswer(query, *read, area.left, area.top, picture);
}

}
