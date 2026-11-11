#include <gs/platform/Speech.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using gs::platform::SpeechHeard;
using gs::platform::SpeechOutput;
using gs::platform::SpeechVoice;
using gs::platform::SystemSpeech;

struct Waiter {
    std::mutex mutex;
    std::condition_variable changed;

    template <typename Ready>
    bool until(Ready ready)
    {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(30), ready);
    }

    template <typename Change>
    void note(Change change)
    {
        {
            const std::scoped_lock lock(mutex);
            change();
        }
        changed.notify_all();
    }
};

std::optional<SpeechVoice> chineseVoice(const SystemSpeech& speech)
{
    const std::vector<SpeechVoice> voices = speech.voices();
    const std::vector<std::string> languages = speech.languages();
    const auto voice = std::ranges::find(voices, std::string("zh-CN"), &SpeechVoice::language);
    if (voice == voices.end() || std::ranges::find(languages, std::string("zh-CN")) == languages.end()) {
        return std::nullopt;
    }
    return *voice;
}

}

TEST_CASE("ADR-0112: Windows speech lists its voices and recognizers by language")
{
    const SystemSpeech speech(SpeechOutput::Nowhere);
    const std::vector<SpeechVoice> voices = speech.voices();
    MESSAGE(voices.size(), " voices, recognizers for ", speech.languages().size(), " languages");
    for (const SpeechVoice& voice : voices) {
        INFO(voice.name);
        CHECK_FALSE(voice.id.empty());
        CHECK_FALSE(voice.name.empty());
        CHECK(std::ranges::count(voices, voice.name, &SpeechVoice::name) == 1);
        const std::optional<std::vector<std::uint8_t>> pcm = SystemSpeech::record({.text = "1", .voice = voice.id});
        REQUIRE(pcm);
        CHECK_FALSE(pcm->empty());
    }
    for (const std::string& language : speech.languages()) {
        CHECK(language.find('-') != std::string::npos);
    }
}

TEST_CASE("ADR-0112: Windows speech hears a command in what it read aloud")
{
    SystemSpeech speech(SpeechOutput::Nowhere);
    const std::optional<SpeechVoice> voice = chineseVoice(speech);
    if (!voice) {
        MESSAGE("no Chinese voice or recognizer on this computer");
        return;
    }
    const std::optional<std::vector<std::uint8_t>> pcm =
        SystemSpeech::record({.text = "打开地图", .voice = voice->id, .rate = 1.0, .volume = 1.0});
    REQUIRE(pcm);
    CHECK(pcm->size() > SystemSpeech::kRecordedRate / 2);

    Waiter waiter;
    std::vector<SpeechHeard> heard;
    std::optional<std::string> stopped;
    speech.hearFrom(*pcm);
    speech.listen({.phrases = {"关闭背包", "打开地图", "向前走"}, .language = "zh-CN"},
                  [&](SpeechHeard got) { waiter.note([&] { heard.push_back(std::move(got)); }); },
                  [&](std::string problem) { waiter.note([&] { stopped = std::move(problem); }); });
    REQUIRE(waiter.until([&] { return stopped.has_value(); }));
    CHECK(stopped->empty());
    REQUIRE(heard.size() == 1);
    CHECK(heard[0].phrase == "打开地图");
    CHECK(heard[0].confidence > 0.0);
    CHECK(heard[0].confidence <= 1.0);
    MESSAGE("heard \"", heard[0].text, "\" with ", heard[0].confidence);
}

TEST_CASE("ADR-0112: Windows speech reads lines in turn, drops a stopped one and says why a line fails")
{
    SystemSpeech speech(SpeechOutput::Nowhere);
    if (speech.voices().empty()) {
        MESSAGE("no voice on this computer");
        return;
    }
    Waiter waiter;
    std::vector<std::pair<int, bool>> spoken;
    std::string problem;
    const auto noting = [&](int line) {
        return [&waiter, &spoken, &problem, line](bool finished, std::string why) {
            waiter.note([&] {
                spoken.emplace_back(line, finished);
                if (!why.empty()) {
                    problem = std::move(why);
                }
            });
        };
    };
    speech.say({.text = "第一句。"}, noting(1));
    const std::uint64_t second = speech.say({.text = "第二句。"}, noting(2));
    speech.say({.text = "第三句。"}, noting(3));
    speech.stop(second);
    REQUIRE(waiter.until([&] { return spoken.size() == 3; }));
    CHECK(std::ranges::find(spoken, std::pair(2, false)) != spoken.end());
    const auto first = std::ranges::find(spoken, std::pair(1, true));
    const auto third = std::ranges::find(spoken, std::pair(3, true));
    CHECK(first != spoken.end());
    CHECK(third != spoken.end());
    CHECK(first < third);
    CHECK(problem.empty());

    speech.say({.text = "没有这个声音", .voice = "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens\\Nobody"},
               noting(4));
    REQUIRE(waiter.until([&] { return spoken.size() == 4; }));
    CHECK(spoken.back() == std::pair(4, false));
    CHECK(problem.find("cannot be used") != std::string::npos);

    std::optional<std::string> stopped;
    speech.listen({.phrases = {"hello"}, .language = "xx-XX"}, [](SpeechHeard) {},
                  [&](std::string why) { waiter.note([&] { stopped = std::move(why); }); });
    REQUIRE(waiter.until([&] { return stopped.has_value(); }));
    CHECK(stopped->find("no speech recognizer") != std::string::npos);
}
