#include "gs/script/TextSearch.h"

namespace gs::script {

namespace {

std::size_t spaceAt(std::string_view text, std::size_t at) noexcept
{
    const auto byte = [&text](std::size_t index) { return static_cast<unsigned char>(text[index]); };
    const unsigned char first = byte(at);
    if (first == ' ' || (first >= '\t' && first <= '\r')) {
        return 1;
    }
    if (first == 0xC2 && at + 1 < text.size() && byte(at + 1) == 0xA0) {
        return 2;
    }
    if (first == 0xE3 && at + 2 < text.size() && byte(at + 1) == 0x80 && byte(at + 2) == 0x80) {
        return 3;
    }
    return 0;
}

}

std::string foldedText(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t at = 0; at < text.size();) {
        if (const std::size_t skip = spaceAt(text, at); skip != 0) {
            at += skip;
            continue;
        }
        const char c = text[at++];
        out += c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return out;
}

}
