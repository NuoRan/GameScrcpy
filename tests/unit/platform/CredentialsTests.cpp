#include "gs/platform/Credentials.h"
#include "gs/platform/Crypto.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

using namespace gs::platform;

namespace {

std::string testTarget()
{
    std::array<std::uint8_t, 8> salt{};
    randomBytes(salt);
    return "GameScrcpy/test/credentials-" + toHex(salt);
}

class Forget
{
public:
    explicit Forget(std::string target)
        : m_target(std::move(target))
    {
    }
    ~Forget() { static_cast<void>(deleteCredential(m_target)); }
    Forget(const Forget&) = delete;
    Forget& operator=(const Forget&) = delete;

private:
    std::string m_target;
};

}

TEST_CASE("a credential is written, read back, replaced and deleted under a target name of its own")
{
    const std::string target = testTarget();
    const Forget forget(target);

    const auto before = readCredential(target);
    REQUIRE_FALSE(before.has_value());
    CHECK(credentialMissing(before.error()));

    REQUIRE_FALSE(writeCredential(target, "sk-test-123"));
    const auto first = readCredential(target);
    REQUIRE(first.has_value());
    CHECK(*first == "sk-test-123");

    const std::string unicode = "key-\xE5\xAF\x86\xE9\x92\xA5-\xF0\x9F\x94\x91";
    REQUIRE_FALSE(writeCredential(target, unicode));
    CHECK(readCredential(target).value() == unicode);

    const std::string longest(kMostSecretBytes, 'k');
    REQUIRE_FALSE(writeCredential(target, longest));
    CHECK(readCredential(target).value() == longest);
    CHECK(writeCredential(target, std::string(kMostSecretBytes + 1, 'k')) == std::errc::value_too_large);
    CHECK(readCredential(target).value() == longest);

    REQUIRE_FALSE(deleteCredential(target));
    const auto after = readCredential(target);
    REQUIRE_FALSE(after.has_value());
    CHECK(credentialMissing(after.error()));
    CHECK_FALSE(deleteCredential(target));
}

TEST_CASE("a credential target must be valid UTF-8 text")
{
    CHECK(writeCredential("", "secret") == std::errc::invalid_argument);
    CHECK(writeCredential(std::string_view("\xFF\xFE", 2), "secret") == std::errc::invalid_argument);
    CHECK(writeCredential(std::string_view("a\0b", 3), "secret") == std::errc::invalid_argument);
    CHECK(readCredential("").error() == std::errc::invalid_argument);
    CHECK(deleteCredential("") == std::errc::invalid_argument);
    CHECK_FALSE(credentialMissing(std::make_error_code(std::errc::invalid_argument)));
}
