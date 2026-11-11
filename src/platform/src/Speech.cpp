#include "gs/platform/Speech.h"

#include "gs/platform/Thread.h"

#include <windows.h>

#include <mmsystem.h>
#include <objbase.h>
#include <sapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <deque>
#include <map>
#include <mutex>
#include <string_view>
#include <thread>
#include <utility>

namespace gs::platform {

namespace {

using Microsoft::WRL::ComPtr;

constexpr const wchar_t* kClassicVoices = L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech\\Voices";
constexpr const wchar_t* kOneCoreVoices = L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech_OneCore\\Voices";
constexpr const wchar_t* kCommandRule = L"commands";

std::wstring wide(std::string_view utf8)
{
    if (utf8.empty()) {
        return {};
    }
    const int size = static_cast<int>(utf8.size());
    const int length = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), size, nullptr, 0);
    std::wstring text(static_cast<std::size_t>(std::max(length, 0)), L'\0');
    if (length > 0) {
        ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), size, text.data(), length);
    }
    return text;
}

std::string narrow(std::wstring_view text)
{
    if (text.empty()) {
        return {};
    }
    const int size = static_cast<int>(text.size());
    const int length = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), size, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(length, 0)), '\0');
    if (length > 0) {
        ::WideCharToMultiByte(CP_UTF8, 0, text.data(), size, out.data(), length, nullptr, nullptr);
    }
    return out;
}

std::string hexOf(HRESULT result)
{
    std::array<char, 16> text{};
    std::snprintf(text.data(), text.size(), "0x%08lX", static_cast<unsigned long>(result));
    return text.data();
}

class ComScope
{
public:
    ComScope()
        : m_result(::CoInitializeEx(nullptr, COINIT_MULTITHREADED))
    {
    }
    ~ComScope()
    {
        if (SUCCEEDED(m_result)) {
            ::CoUninitialize();
        }
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    HRESULT m_result;
};

std::string attributeOf(ISpObjectToken* token, const wchar_t* name)
{
    ComPtr<ISpDataKey> attributes;
    wchar_t* value = nullptr;
    if (FAILED(token->OpenKey(L"Attributes", &attributes)) || FAILED(attributes->GetStringValue(name, &value)) ||
        value == nullptr) {
        return {};
    }
    std::string text = narrow(value);
    ::CoTaskMemFree(value);
    return text;
}

std::string languageOf(ISpObjectToken* token)
{
    const std::string codes = attributeOf(token, L"Language");
    const LCID lcid = static_cast<LCID>(std::strtoul(codes.c_str(), nullptr, 16));
    std::array<wchar_t, LOCALE_NAME_MAX_LENGTH> name{};
    if (lcid == 0 || ::LCIDToLocaleName(lcid, name.data(), static_cast<int>(name.size()), 0) == 0) {
        return {};
    }
    return narrow(name.data());
}

std::string idOf(ISpObjectToken* token)
{
    wchar_t* id = nullptr;
    if (FAILED(token->GetId(&id)) || id == nullptr) {
        return {};
    }
    std::string text = narrow(id);
    ::CoTaskMemFree(id);
    return text;
}

std::vector<ComPtr<ISpObjectToken>> tokensOf(const wchar_t* category, const wchar_t* required)
{
    std::vector<ComPtr<ISpObjectToken>> found;
    ComPtr<ISpObjectTokenCategory> tokens;
    ComPtr<IEnumSpObjectTokens> list;
    if (FAILED(::CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&tokens))) ||
        FAILED(tokens->SetId(category, FALSE)) || FAILED(tokens->EnumTokens(required, nullptr, &list)) ||
        list == nullptr) {
        return found;
    }
    ULONG count = 0;
    if (FAILED(list->GetCount(&count))) {
        return found;
    }
    for (ULONG index = 0; index < count; ++index) {
        ComPtr<ISpObjectToken> token;
        if (SUCCEEDED(list->Item(index, &token))) {
            found.push_back(std::move(token));
        }
    }
    return found;
}

ComPtr<ISpObjectToken> tokenFor(std::string_view id)
{
    ComPtr<ISpObjectToken> token;
    if (FAILED(::CoCreateInstance(CLSID_SpObjectToken, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&token))) ||
        FAILED(token->SetId(nullptr, wide(id).c_str(), FALSE))) {
        return nullptr;
    }
    return token;
}

ComPtr<ISpObjectToken> defaultMicrophone()
{
    ComPtr<ISpObjectTokenCategory> category;
    wchar_t* id = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&category))) ||
        FAILED(category->SetId(SPCAT_AUDIOIN, FALSE)) || FAILED(category->GetDefaultTokenId(&id)) || id == nullptr) {
        return nullptr;
    }
    ComPtr<ISpObjectToken> token = tokenFor(narrow(id));
    ::CoTaskMemFree(id);
    return token;
}

ComPtr<ISpObjectToken> recognizerFor(std::string_view language)
{
    const LCID lcid = ::LocaleNameToLCID(wide(language).c_str(), 0);
    if (lcid == 0) {
        return nullptr;
    }
    std::array<wchar_t, 32> required{};
    std::swprintf(required.data(), required.size(), L"Language=%X", static_cast<unsigned>(lcid));
    std::vector<ComPtr<ISpObjectToken>> found = tokensOf(SPCAT_RECOGNIZERS, required.data());
    return found.empty() ? nullptr : found.front();
}

std::vector<SpeechVoice> listVoices()
{
    std::vector<SpeechVoice> voices;
    for (const wchar_t* category : {kClassicVoices, kOneCoreVoices}) {
        for (const ComPtr<ISpObjectToken>& token : tokensOf(category, nullptr)) {
            SpeechVoice voice{.id = idOf(token.Get()),
                              .name = attributeOf(token.Get(), L"Name"),
                              .language = languageOf(token.Get())};
            if (voice.id.empty() || voice.name.empty() ||
                std::ranges::find(voices, voice.name, &SpeechVoice::name) != voices.end()) {
                continue;
            }
            voices.push_back(std::move(voice));
        }
    }
    return voices;
}

std::vector<std::string> listLanguages()
{
    std::vector<std::string> languages;
    for (const ComPtr<ISpObjectToken>& token : tokensOf(SPCAT_RECOGNIZERS, nullptr)) {
        std::string language = languageOf(token.Get());
        if (!language.empty() && std::ranges::find(languages, language) == languages.end()) {
            languages.push_back(std::move(language));
        }
    }
    return languages;
}

WAVEFORMATEX recordedFormat()
{
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = SystemSpeech::kRecordedRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 2;
    format.nAvgBytesPerSec = SystemSpeech::kRecordedRate * 2;
    return format;
}

ComPtr<ISpStream> memoryStream(ComPtr<IStream>& base)
{
    const WAVEFORMATEX format = recordedFormat();
    ComPtr<ISpStream> stream;
    if (FAILED(::CreateStreamOnHGlobal(nullptr, TRUE, &base)) ||
        FAILED(::CoCreateInstance(CLSID_SpStream, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&stream))) ||
        FAILED(stream->SetBaseStream(base.Get(), SPDFID_WaveFormatEx, &format))) {
        return nullptr;
    }
    return stream;
}

long rateOf(double rate)
{
    return std::lround(10.0 * std::log(std::clamp(rate, 0.5, 2.0)) / std::log(3.0));
}

USHORT volumeOf(double volume)
{
    return static_cast<USHORT>(std::lround(std::clamp(volume, 0.0, 1.0) * 100.0));
}

std::string prepare(ISpVoice* voice, const SpeechLine& line)
{
    if (line.voice.empty()) {
        voice->SetVoice(nullptr);
    } else {
        const ComPtr<ISpObjectToken> token = tokenFor(line.voice);
        if (token == nullptr || FAILED(voice->SetVoice(token.Get()))) {
            return "the voice " + line.voice + " cannot be used";
        }
    }
    voice->SetRate(rateOf(line.rate));
    voice->SetVolume(volumeOf(line.volume));
    return {};
}

void clearEvent(SPEVENT& event)
{
    switch (event.elParamType) {
    case SPET_LPARAM_IS_POINTER:
    case SPET_LPARAM_IS_STRING:
        ::CoTaskMemFree(reinterpret_cast<void*>(event.lParam));
        break;
    case SPET_LPARAM_IS_TOKEN:
    case SPET_LPARAM_IS_OBJECT:
        if (event.lParam != 0) {
            reinterpret_cast<IUnknown*>(event.lParam)->Release();
        }
        break;
    default:
        break;
    }
    event = SPEVENT{};
}

bool endedSpeaking(ISpVoice* voice)
{
    bool ended = false;
    SPEVENT event{};
    ULONG fetched = 0;
    while (voice->GetEvents(1, &event, &fetched) == S_OK && fetched == 1) {
        ended = ended || event.eEventId == SPEI_END_INPUT_STREAM;
        clearEvent(event);
    }
    return ended;
}

std::string squeezed(std::string_view text)
{
    std::string out;
    for (const char c : text) {
        if (c != ' ' && c != '\t') {
            out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
        }
    }
    return out;
}

std::string phraseOf(const std::vector<std::string>& phrases, const std::string& text)
{
    const std::string wanted = squeezed(text);
    const auto found = std::ranges::find_if(phrases, [&wanted](const std::string& phrase) {
        return squeezed(phrase) == wanted;
    });
    return found != phrases.end() ? *found : text;
}

}

struct SystemSpeech::Impl {
    struct Line {
        std::uint64_t id = 0;
        SpeechLine line;
        Spoken spoken;
    };

    struct Wanted {
        std::uint64_t id = 0;
        SpeechCommands commands;
        Heard heard;
        Stopped stopped;
    };

    struct Session {
        ComPtr<ISpRecognizer> recognizer;
        ComPtr<ISpRecoContext> context;
        HANDLE event = nullptr;
        std::map<std::uint64_t, ComPtr<ISpRecoGrammar>> grammars;
    };

    Impl()
        : speakWake(::CreateEventW(nullptr, FALSE, FALSE, nullptr))
        , listenWake(::CreateEventW(nullptr, FALSE, FALSE, nullptr))
    {
    }

    ~Impl()
    {
        ::CloseHandle(speakWake);
        ::CloseHandle(listenWake);
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    void speakAll();
    bool waitSpoken(ISpVoice* voice, HANDLE done);
    void listenAll();
    std::string open(std::map<std::string, Session>& sessions, const Wanted& wanted);

    SpeechOutput output = SpeechOutput::Speakers;
    HANDLE speakWake;
    HANDLE listenWake;
    std::mutex mutex;
    std::deque<Line> lines;
    std::uint64_t speaking = 0;
    bool stopSpeaking = false;
    std::vector<Wanted> adding;
    std::vector<std::uint64_t> removing;
    std::optional<std::vector<std::uint8_t>> heardInput;
    bool quit = false;
    std::uint64_t next = 0;
    std::thread speaker;
    std::thread listener;
};

void SystemSpeech::Impl::speakAll()
{
    setCurrentThreadName("speech say");
    const ComScope com;
    ComPtr<ISpVoice> voice;
    ComPtr<IStream> memory;
    const bool ready = SUCCEEDED(::CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&voice)));
    HANDLE done = nullptr;
    if (ready) {
        if (output == SpeechOutput::Nowhere) {
            if (const ComPtr<ISpStream> stream = memoryStream(memory); stream != nullptr) {
                voice->SetOutput(stream.Get(), TRUE);
            }
        }
        const ULONGLONG interest = SPFEI(SPEI_END_INPUT_STREAM);
        voice->SetInterest(interest, interest);
        voice->SetNotifyWin32Event();
        done = voice->GetNotifyEventHandle();
    }
    for (;;) {
        Line current;
        {
            std::unique_lock lock(mutex);
            while (!quit && lines.empty()) {
                lock.unlock();
                ::WaitForSingleObject(speakWake, INFINITE);
                lock.lock();
            }
            if (quit) {
                break;
            }
            current = std::move(lines.front());
            lines.pop_front();
            speaking = current.id;
            stopSpeaking = false;
        }
        bool finished = false;
        std::string problem;
        if (!ready || done == nullptr) {
            problem = "Windows speech is not available";
        } else if (problem = prepare(voice.Get(), current.line); problem.empty()) {
            static_cast<void>(endedSpeaking(voice.Get()));
            const HRESULT result = voice->Speak(wide(current.line.text).c_str(),
                                                SPF_ASYNC | SPF_IS_NOT_XML | SPF_PURGEBEFORESPEAK, nullptr);
            if (FAILED(result)) {
                problem = "speaking failed (" + hexOf(result) + ")";
            } else {
                finished = waitSpoken(voice.Get(), done);
            }
        }
        {
            const std::scoped_lock lock(mutex);
            speaking = 0;
        }
        if (memory != nullptr) {
            memory->Seek(LARGE_INTEGER{}, STREAM_SEEK_SET, nullptr);
            memory->SetSize(ULARGE_INTEGER{});
        }
        current.spoken(finished, std::move(problem));
    }
    if (ready) {
        voice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
    }
    std::deque<Line> left;
    {
        const std::scoped_lock lock(mutex);
        left.swap(lines);
    }
    for (Line& line : left) {
        line.spoken(false, {});
    }
}

bool SystemSpeech::Impl::waitSpoken(ISpVoice* voice, HANDLE done)
{
    const std::array<HANDLE, 2> handles{done, speakWake};
    for (;;) {
        const DWORD which = ::WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, INFINITE);
        if (which == WAIT_OBJECT_0) {
            if (endedSpeaking(voice)) {
                return true;
            }
            continue;
        }
        if (which != WAIT_OBJECT_0 + 1) {
            return false;
        }
        bool stop = false;
        {
            const std::scoped_lock lock(mutex);
            stop = stopSpeaking || quit;
        }
        if (stop) {
            voice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
            return false;
        }
    }
}

std::string SystemSpeech::Impl::open(std::map<std::string, Session>& sessions, const Wanted& wanted)
{
    const std::string& language = wanted.commands.language;
    auto found = sessions.find(language);
    if (found == sessions.end()) {
        const ComPtr<ISpObjectToken> engine = recognizerFor(language);
        if (engine == nullptr) {
            return "Windows has no speech recognizer for " + language;
        }
        Session session;
        if (FAILED(::CoCreateInstance(CLSID_SpInprocRecognizer, nullptr, CLSCTX_ALL,
                                      IID_PPV_ARGS(&session.recognizer))) ||
            FAILED(session.recognizer->SetRecognizer(engine.Get()))) {
            return "the speech recognizer for " + language + " cannot start";
        }
        std::optional<std::vector<std::uint8_t>> input;
        {
            const std::scoped_lock lock(mutex);
            input = std::exchange(heardInput, std::nullopt);
        }
        if (input) {
            ComPtr<IStream> base;
            const ComPtr<ISpStream> stream = memoryStream(base);
            ULONG written = 0;
            if (stream == nullptr ||
                FAILED(base->Write(input->data(), static_cast<ULONG>(input->size()), &written)) ||
                FAILED(base->Seek(LARGE_INTEGER{}, STREAM_SEEK_SET, nullptr)) ||
                FAILED(session.recognizer->SetInput(stream.Get(), TRUE))) {
                return "the recorded speech cannot be read";
            }
        } else {
            const ComPtr<ISpObjectToken> microphone = defaultMicrophone();
            if (microphone == nullptr || FAILED(session.recognizer->SetInput(microphone.Get(), TRUE))) {
                return "there is no microphone to listen with";
            }
        }
        if (FAILED(session.recognizer->CreateRecoContext(&session.context)) ||
            FAILED(session.context->SetNotifyWin32Event())) {
            return "the speech recognizer for " + language + " cannot start";
        }
        const ULONGLONG interest = SPFEI(SPEI_RECOGNITION) | SPFEI(SPEI_END_SR_STREAM);
        session.context->SetInterest(interest, interest);
        session.event = session.context->GetNotifyEventHandle();
        found = sessions.emplace(language, std::move(session)).first;
    }
    Session& session = found->second;
    ComPtr<ISpRecoGrammar> grammar;
    SPSTATEHANDLE rule = nullptr;
    std::string problem;
    if (FAILED(session.context->CreateGrammar(wanted.id, &grammar)) ||
        FAILED(grammar->GetRule(kCommandRule, 1, SPRAF_TopLevel | SPRAF_Active, TRUE, &rule))) {
        problem = "the commands cannot be listened for";
    }
    for (const std::string& phrase : wanted.commands.phrases) {
        if (problem.empty() &&
            FAILED(grammar->AddWordTransition(rule, nullptr, wide(phrase).c_str(), L" ", SPWT_LEXICAL, 1.0F, nullptr))) {
            problem = "the command \"" + phrase + "\" cannot be listened for";
        }
    }
    if (problem.empty() &&
        (FAILED(grammar->Commit(0)) || FAILED(grammar->SetRuleState(nullptr, nullptr, SPRS_ACTIVE)))) {
        problem = "the commands cannot be listened for";
    }
    if (!problem.empty()) {
        if (session.grammars.empty()) {
            sessions.erase(found);
        }
        return problem;
    }
    session.grammars.emplace(wanted.id, std::move(grammar));
    return {};
}

void SystemSpeech::Impl::listenAll()
{
    setCurrentThreadName("speech listen");
    const ComScope com;
    std::map<std::string, Session> sessions;
    std::map<std::uint64_t, Wanted> listening;
    const auto forget = [&sessions, &listening](std::uint64_t id) {
        const auto found = listening.find(id);
        if (found == listening.end()) {
            return;
        }
        const auto session = sessions.find(found->second.commands.language);
        if (session != sessions.end()) {
            session->second.grammars.erase(id);
            if (session->second.grammars.empty()) {
                sessions.erase(session);
            }
        }
        listening.erase(found);
    };
    for (;;) {
        std::vector<Wanted> added;
        std::vector<std::uint64_t> removed;
        {
            const std::scoped_lock lock(mutex);
            if (quit) {
                break;
            }
            added.swap(adding);
            removed.swap(removing);
        }
        for (const std::uint64_t id : removed) {
            forget(id);
        }
        for (Wanted& wanted : added) {
            if (std::string problem = open(sessions, wanted); !problem.empty()) {
                wanted.stopped(std::move(problem));
                continue;
            }
            const std::uint64_t id = wanted.id;
            listening.emplace(id, std::move(wanted));
        }
        std::vector<HANDLE> handles{listenWake};
        std::vector<std::string> languages;
        for (const auto& [language, session] : sessions) {
            handles.push_back(session.event);
            languages.push_back(language);
        }
        const DWORD which = ::WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, INFINITE);
        if (which == WAIT_OBJECT_0 || which >= WAIT_OBJECT_0 + handles.size()) {
            continue;
        }
        const std::string language = languages[which - WAIT_OBJECT_0 - 1];
        Session& session = sessions.at(language);
        bool ended = false;
        SPEVENT event{};
        ULONG fetched = 0;
        while (session.context->GetEvents(1, &event, &fetched) == S_OK && fetched == 1) {
            if (event.eEventId == SPEI_END_SR_STREAM) {
                ended = true;
            } else if (event.eEventId == SPEI_RECOGNITION && event.elParamType == SPET_LPARAM_IS_OBJECT &&
                       event.lParam != 0) {
                auto* result = reinterpret_cast<ISpRecoResult*>(event.lParam);
                SPPHRASE* phrase = nullptr;
                wchar_t* text = nullptr;
                if (SUCCEEDED(result->GetPhrase(&phrase)) && phrase != nullptr) {
                    const std::uint64_t id = phrase->ullGrammarID;
                    double confidence = phrase->Rule.SREngineConfidence;
                    if (!(confidence >= 0.0 && confidence <= 1.0)) {
                        confidence = phrase->Rule.Confidence == SP_HIGH_CONFIDENCE     ? 0.9
                                     : phrase->Rule.Confidence == SP_NORMAL_CONFIDENCE ? 0.6
                                                                                        : 0.3;
                    }
                    ::CoTaskMemFree(phrase);
                    std::string heard;
                    if (SUCCEEDED(result->GetText(static_cast<ULONG>(SP_GETWHOLEPHRASE),
                                                  static_cast<ULONG>(SP_GETWHOLEPHRASE), TRUE, &text, nullptr)) &&
                        text != nullptr) {
                        heard = narrow(text);
                        ::CoTaskMemFree(text);
                    }
                    if (const auto wanted = listening.find(id); wanted != listening.end()) {
                        wanted->second.heard(SpeechHeard{.phrase = phraseOf(wanted->second.commands.phrases, heard),
                                                         .text = heard,
                                                         .confidence = confidence});
                    }
                }
            }
            clearEvent(event);
        }
        if (ended) {
            std::vector<std::uint64_t> stopping;
            for (const auto& [id, wanted] : listening) {
                if (wanted.commands.language == language) {
                    stopping.push_back(id);
                }
            }
            for (const std::uint64_t id : stopping) {
                Stopped stopped = std::move(listening.at(id).stopped);
                forget(id);
                stopped({});
            }
        }
    }
    listening.clear();
    sessions.clear();
}

SystemSpeech::SystemSpeech(SpeechOutput output)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->output = output;
    m_impl->speaker = std::thread([impl = m_impl.get()] { impl->speakAll(); });
    m_impl->listener = std::thread([impl = m_impl.get()] { impl->listenAll(); });
}

SystemSpeech::~SystemSpeech()
{
    {
        const std::scoped_lock lock(m_impl->mutex);
        m_impl->quit = true;
    }
    ::SetEvent(m_impl->speakWake);
    ::SetEvent(m_impl->listenWake);
    m_impl->speaker.join();
    m_impl->listener.join();
}

std::vector<SpeechVoice> SystemSpeech::voices() const
{
    const ComScope com;
    return listVoices();
}

std::vector<std::string> SystemSpeech::languages() const
{
    const ComScope com;
    return listLanguages();
}

std::uint64_t SystemSpeech::say(SpeechLine line, Spoken spoken)
{
    std::uint64_t id = 0;
    {
        const std::scoped_lock lock(m_impl->mutex);
        id = ++m_impl->next;
        m_impl->lines.push_back({.id = id, .line = std::move(line), .spoken = std::move(spoken)});
    }
    ::SetEvent(m_impl->speakWake);
    return id;
}

void SystemSpeech::stop(std::uint64_t id)
{
    Spoken dropped;
    {
        const std::scoped_lock lock(m_impl->mutex);
        if (m_impl->speaking == id) {
            m_impl->stopSpeaking = true;
        } else if (const auto found = std::ranges::find(m_impl->lines, id, &Impl::Line::id);
                   found != m_impl->lines.end()) {
            dropped = std::move(found->spoken);
            m_impl->lines.erase(found);
        }
    }
    ::SetEvent(m_impl->speakWake);
    if (dropped) {
        dropped(false, {});
    }
}

std::uint64_t SystemSpeech::listen(SpeechCommands commands, Heard heard, Stopped stopped)
{
    std::uint64_t id = 0;
    {
        const std::scoped_lock lock(m_impl->mutex);
        id = ++m_impl->next;
        m_impl->adding.push_back(
            {.id = id, .commands = std::move(commands), .heard = std::move(heard), .stopped = std::move(stopped)});
    }
    ::SetEvent(m_impl->listenWake);
    return id;
}

void SystemSpeech::unlisten(std::uint64_t id)
{
    {
        const std::scoped_lock lock(m_impl->mutex);
        if (const auto found = std::ranges::find(m_impl->adding, id, &Impl::Wanted::id);
            found != m_impl->adding.end()) {
            m_impl->adding.erase(found);
            return;
        }
        m_impl->removing.push_back(id);
    }
    ::SetEvent(m_impl->listenWake);
}

std::optional<std::vector<std::uint8_t>> SystemSpeech::record(const SpeechLine& line)
{
    const ComScope com;
    ComPtr<ISpVoice> voice;
    ComPtr<IStream> base;
    if (FAILED(::CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&voice)))) {
        return std::nullopt;
    }
    const ComPtr<ISpStream> stream = memoryStream(base);
    if (stream == nullptr || FAILED(voice->SetOutput(stream.Get(), TRUE)) || !prepare(voice.Get(), line).empty() ||
        FAILED(voice->Speak(wide(line.text).c_str(), SPF_DEFAULT | SPF_IS_NOT_XML, nullptr))) {
        return std::nullopt;
    }
    STATSTG stat{};
    if (FAILED(base->Stat(&stat, STATFLAG_NONAME)) || FAILED(base->Seek(LARGE_INTEGER{}, STREAM_SEEK_SET, nullptr))) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> pcm(static_cast<std::size_t>(stat.cbSize.QuadPart));
    ULONG read = 0;
    if (!pcm.empty() && FAILED(base->Read(pcm.data(), static_cast<ULONG>(pcm.size()), &read))) {
        return std::nullopt;
    }
    pcm.resize(read);
    return pcm;
}

void SystemSpeech::hearFrom(std::vector<std::uint8_t> pcm)
{
    const std::scoped_lock lock(m_impl->mutex);
    m_impl->heardInput = std::move(pcm);
}

}
