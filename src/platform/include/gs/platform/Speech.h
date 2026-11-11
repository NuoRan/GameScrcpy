#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gs::platform {

struct SpeechVoice {
    std::string id;
    std::string name;
    std::string language;
};

struct SpeechLine {
    std::string text;
    std::string voice;
    double rate = 1.0;
    double volume = 1.0;
};

struct SpeechCommands {
    std::vector<std::string> phrases;
    std::string language;
};

struct SpeechHeard {
    std::string phrase;
    std::string text;
    double confidence = 0;
};

enum class SpeechOutput : std::uint8_t { Speakers, Nowhere };

class SystemSpeech
{
public:
    using Spoken = std::function<void(bool finished, std::string problem)>;
    using Heard = std::function<void(SpeechHeard heard)>;
    using Stopped = std::function<void(std::string problem)>;

    explicit SystemSpeech(SpeechOutput output = SpeechOutput::Speakers);
    ~SystemSpeech();
    SystemSpeech(const SystemSpeech&) = delete;
    SystemSpeech& operator=(const SystemSpeech&) = delete;

    [[nodiscard]] std::vector<SpeechVoice> voices() const;
    [[nodiscard]] std::vector<std::string> languages() const;

    std::uint64_t say(SpeechLine line, Spoken spoken);
    void stop(std::uint64_t id);

    std::uint64_t listen(SpeechCommands commands, Heard heard, Stopped stopped);
    void unlisten(std::uint64_t id);

    [[nodiscard]] static std::optional<std::vector<std::uint8_t>> record(const SpeechLine& line);
    void hearFrom(std::vector<std::uint8_t> pcm);

    static constexpr int kRecordedRate = 16000;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
