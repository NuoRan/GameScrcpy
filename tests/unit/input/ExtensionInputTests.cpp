#include "gs/input/MappingEngine.h"
#include "gs/input/Profile.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

using namespace gs::input;
using gs::Microseconds;
using gs::protocol::TouchAction;
using gs::protocol::TouchFrame;

namespace {

class Touches final : public TouchFrameSink, public EngineListener
{
public:
    std::vector<TouchFrame> frames;
    std::vector<std::pair<std::string, bool>> presses;

    bool trySend(const TouchFrame& frame) override
    {
        frames.push_back(frame);
        return true;
    }
    void pointerState(PointerState) override {}
    void notice(EngineNotice) override {}
    void liveChanged(const std::vector<bool>&) override {}
    void controlPressed(std::string_view id, bool down) override { presses.emplace_back(id, down); }
};

LoadResult loadControls(std::string_view controls)
{
    return loadProfile(R"({"format":"gamescrcpy.profile","version":3,"controls":)" + std::string(controls) + "}");
}

InputSource source(std::string_view name)
{
    const auto parsed = parseSource(name);
    REQUIRE(parsed.has_value());
    return *parsed;
}

}

TEST_CASE("SCR-026: Ext input source names parse, print and round-trip")
{
    for (const char* name : {"Ext.mine.deck.k1", "Ext.mine.pedal-board.left", "Ext.com.example.deck.deck.Key_7",
                             "Ext.community.midi-in.keys.note-60", "Ext.a.b.c.d"}) {
        CAPTURE(name);
        const auto parsed = parseSource(name);
        REQUIRE(parsed.has_value());
        CHECK(parsed->device == Device::Extension);
        CHECK(sourceName(*parsed) == name);
        CHECK(parseSource(name) == parsed);
        CHECK(knownSource(name) == parsed);
        CHECK(kindOf(*parsed) == SourceKind::Button);
        CHECK(denseIndex(*parsed) >= kKeyboardSourceCount + kMouseSourceCount + kGamepadSourceCount);
        CHECK(denseIndex(*parsed) < kSourceCount);
    }
    CHECK(parseSource("Ext.mine.deck.k1") != parseSource("Ext.mine.deck.k2"));
    CHECK(parseSource("Ext.mine.deck.k1") != parseSource("Ext.com.example.deck.k1"));

    const auto split = splitExtensionInput("Ext.com.example.deck.deck.Key_7");
    REQUIRE(split.has_value());
    CHECK(split->extension == "com.example.deck");
    CHECK(split->source == "deck");
    CHECK(split->key == "Key_7");
    CHECK(extensionInputName("mine", "pedal", "left") == "Ext.mine.pedal.left");
    CHECK(extensionInputName(split->extension, split->source, split->key) == "Ext.com.example.deck.deck.Key_7");

    for (const char* bad : {"Ext.", "Ext.mine", "Ext.mine.deck", "Ext.mine..k1", "Ext.mine.deck.", "Ext.yours.deck.k1",
                            "Ext.Com.example.deck.k1", "Ext.com..example.deck.k1", "Ext.-com.example.deck.k1",
                            "Ext.mine.Deck.k1", "Ext.mine.deck_1.k1", "Ext.mine.deck.k 1", "Ext.mine.deck.k.1x.",
                            "ext.mine.deck.k1", "Ext.mine.deck.abcdefghijklmnopqrstuvwxyz0123456",
                            "Ext.mine.abcdefghijklmnopqrstuvwxyz0123456.k1"}) {
        CAPTURE(bad);
        CHECK_FALSE(parseSource(bad).has_value());
        CHECK_FALSE(splitExtensionInput(bad).has_value());
    }
    CHECK(validInputSourceName("deck-2"));
    CHECK_FALSE(validInputSourceName(""));
    CHECK_FALSE(validInputSourceName("Deck"));
    CHECK(validInputKeyName("PadA_1-x"));
    CHECK_FALSE(validInputKeyName("a.b"));
}

TEST_CASE("SCR-026: an Ext name is only remembered once a profile or a chord uses it")
{
    CHECK_FALSE(knownSource("Ext.mine.unseen.never").has_value());
    const auto chord = parseChord("Ext.mine.unseen.never");
    REQUIRE(chord.has_value());
    CHECK(chord->modifiers == 0);
    CHECK(chordName(*chord) == "Ext.mine.unseen.never");
    CHECK(knownSource("Ext.mine.unseen.never") == chord->source);
    CHECK(chordName(*parseChord("Ctrl+Ext.mine.unseen.never")) == "Ctrl+Ext.mine.unseen.never");
}

TEST_CASE("SCR-026: Ext inputs bind to buttons and sticks, save as written and clash like other inputs")
{
    const LoadResult loaded = loadControls(R"([
        {"id": "jump", "type": "tap", "inputs": {"press": "Ext.mine.deck.k1"}},
        {"id": "crouch", "type": "hold", "inputs": {"press": {"input": "Ext.mine.deck.k1"}}},
        {"id": "fire", "type": "hold", "inputs": {"press": "Ext.com.example.deck.deck.k1"}},
        {"id": "walk", "type": "padStick", "inputs": {"stick": "Ext.mine.knob.xy"}}
    ])");
    REQUIRE(loaded.ok());
    const std::vector<InputConflict> conflicts = inputConflicts(loaded.profile);
    REQUIRE(conflicts.size() == 1);
    CHECK(chordName(conflicts[0].input) == "Ext.mine.deck.k1");
    CHECK(conflicts[0].bindings.size() == 2);
    CHECK(std::ranges::count(loaded.diagnostics, DiagnosticCode::Conflict, &Diagnostic::code) == 1);

    const std::string saved = saveProfile(loaded.profile);
    CHECK(saved.find("\"Ext.mine.deck.k1\"") != std::string::npos);
    CHECK(saved.find("\"Ext.mine.knob.xy\"") != std::string::npos);
    const LoadResult again = loadProfile(saved);
    REQUIRE(again.ok());
    CHECK(again.profile == loaded.profile);

    const LoadResult claimed = loadControls(R"([
        {"id": "jump", "type": "tap", "inputs": {"press": {"input": "Ext.mine.deck.k1", "claim": "both"}}}
    ])");
    CHECK_FALSE(claimed.ok());
    CHECK(std::ranges::count(claimed.diagnostics, DiagnosticCode::InputKindMismatch, &Diagnostic::code) == 1);
}

TEST_CASE("SCR-026: the mapping engine turns Ext presses and axes into touches like keys and sticks")
{
    const LoadResult loaded = loadControls(R"([
        {"id": "jump", "type": "hold", "offset": [0.3, 0.2], "inputs": {"press": "Ext.mine.deck.k1"}},
        {"id": "walk", "type": "padStick", "anchor": "bottomLeft", "offset": [0.3, -0.3],
         "inputs": {"stick": "Ext.mine.knob.xy"}}
    ])");
    REQUIRE(loaded.ok());
    Touches touches;
    MappingEngine engine(loaded.profile,
                         EngineConfig{.screen = {.width = 2400, .height = 1080}, .refreshHz = 60.0,
                                      .pacingTick = Microseconds{0}},
                         touches, touches);
    engine.buttonDown(source("Ext.mine.deck.k1"), Microseconds{0});
    REQUIRE_FALSE(touches.frames.empty());
    CHECK(touches.frames.back().points.back().action == TouchAction::Down);
    CHECK(engine.claims(source("Ext.mine.deck.k1")));
    CHECK_FALSE(engine.claims(source("Ext.mine.deck.k2")));
    engine.buttonUp(source("Ext.mine.deck.k1"), Microseconds{1000});
    CHECK(touches.frames.back().points.back().action == TouchAction::Up);
    CHECK(touches.presses == std::vector<std::pair<std::string, bool>>{{"jump", true}, {"jump", false}});

    const std::size_t before = touches.frames.size();
    engine.axis(source("Ext.mine.knob.xy"), Vec2{1.0, 0.0}, Microseconds{2000});
    CHECK(touches.frames.size() > before);
    CHECK(engine.activeTouches() == 1);
    engine.axis(source("Ext.mine.knob.xy"), Vec2{0.0, 0.0}, Microseconds{3000});
    engine.tick(Microseconds{200'000});
    CHECK(engine.activeTouches() == 0);
}
