#include "KeyFrameGate.h"

#include <doctest/doctest.h>

using namespace gs;
using gs::session::detail::KeyFrameGate;

namespace {

protocol::MediaHeader frame(bool key)
{
    protocol::MediaHeader header;
    header.kind = protocol::MediaKind::Frame;
    header.keyFrame = key;
    header.session = 1;
    return header;
}

protocol::MediaHeader description()
{
    protocol::MediaHeader header;
    header.kind = protocol::MediaKind::Session;
    header.session = 2;
    return header;
}

}

TEST_CASE("after a lost packet the frames wait for a key frame, which is asked for (SPEC 10.5, WIF-003)")
{
    KeyFrameGate gate(500'000);
    auto decision = gate.admit(frame(false), 0, 0);
    CHECK((decision.deliver && !decision.askKeyFrame));

    decision = gate.admit(frame(false), 2, 1'000);
    CHECK_FALSE(decision.deliver);
    CHECK(decision.askKeyFrame);
    CHECK(gate.waiting());
    CHECK_FALSE(gate.admit(frame(false), 0, 2'000).deliver);
    CHECK(gate.admit(description(), 0, 3'000).deliver);

    decision = gate.admit(frame(true), 0, 4'000);
    CHECK(decision.deliver);
    CHECK_FALSE(decision.askKeyFrame);
    CHECK_FALSE(gate.waiting());
    CHECK(gate.admit(frame(false), 0, 5'000).deliver);
}

TEST_CASE("key frame requests are spaced, and a lost key frame keeps the frames waiting")
{
    KeyFrameGate gate(500'000);
    CHECK(gate.admit(frame(false), 1, 1'000'000).askKeyFrame);
    CHECK_FALSE(gate.admit(frame(false), 1, 1'200'000).askKeyFrame);
    CHECK(gate.admit(frame(false), 1, 1'500'000).askKeyFrame);
    CHECK(gate.waiting());

    const auto decision = gate.admit(frame(true), 3, 1'600'000);
    CHECK(decision.deliver);
    CHECK_FALSE(gate.waiting());
}
