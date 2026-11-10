#include <gs/platform/KeyboardLayout.h>

#include <doctest/doctest.h>

using gs::platform::layoutCharacter;

TEST_CASE("the keyboard layout names the keys that type a visible character and no others (INP-013)")
{
    const std::optional<char32_t> letter = layoutCharacter(0x1E);
    REQUIRE(letter.has_value());
    CHECK(*letter > U' ');
    CHECK(layoutCharacter(0x02).has_value());
    CHECK_FALSE(layoutCharacter(0x01).has_value());
    CHECK_FALSE(layoutCharacter(0x39).has_value());
    CHECK_FALSE(layoutCharacter(0x0F).has_value());
    CHECK_FALSE(layoutCharacter(0xE048).has_value());
    CHECK_FALSE(layoutCharacter(0x3B).has_value());
}
