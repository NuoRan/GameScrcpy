#include "gs/platform/Crypto.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace gs::platform;

namespace {

std::span<const std::uint8_t> bytes(std::string_view text)
{
    return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

}

TEST_CASE("sha256 matches the FIPS 180-4 examples")
{
    CHECK(toHex(sha256({})) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(toHex(sha256(bytes("abc"))) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("hmacSha256 matches RFC 4231")
{
    const std::vector<std::uint8_t> key1(20, 0x0B);
    CHECK(toHex(hmacSha256(key1, {bytes("Hi There")})) ==
          "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");

    CHECK(toHex(hmacSha256(bytes("Jefe"), {bytes("what do ya want for nothing?")})) ==
          "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");

    const std::vector<std::uint8_t> key6(131, 0xAA);
    CHECK(toHex(hmacSha256(key6, {bytes("Test Using Larger Than Block-Size Key - Hash Key First")})) ==
          "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
}

TEST_CASE("hmacSha256 authenticates the concatenation of its parts")
{
    CHECK(hmacSha256(bytes("Jefe"), {bytes("what do ya "), bytes(""), bytes("want for nothing?")}) ==
          hmacSha256(bytes("Jefe"), {bytes("what do ya want for nothing?")}));
}

TEST_CASE("randomBytes fills the whole buffer with fresh values")
{
    std::vector<std::uint8_t> first(64, 0);
    std::vector<std::uint8_t> second(64, 0);
    randomBytes(first);
    randomBytes(second);
    CHECK(first != second);
    CHECK(std::ranges::any_of(first, [](std::uint8_t b) { return b != 0; }));
    randomBytes({});
}

TEST_CASE("toHex writes two lowercase digits per byte")
{
    const std::uint8_t data[] = {0x00, 0x0F, 0xAB, 0xFF};
    CHECK(toHex(data) == "000fabff");
    CHECK(toHex({}).empty());
}

namespace {

std::vector<std::uint8_t> fromHex(std::string_view hex)
{
    const auto nibble = [](char c) {
        return static_cast<std::uint8_t>(c <= '9' ? c - '0' : c - 'a' + 10);
    };
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<std::uint8_t>((nibble(hex[i]) << 4U) | nibble(hex[i + 1])));
    }
    return out;
}

template <std::size_t N>
std::array<std::uint8_t, N> fixed(std::string_view hex)
{
    const std::vector<std::uint8_t> bytes = fromHex(hex);
    std::array<std::uint8_t, N> out{};
    std::copy_n(bytes.begin(), std::min(bytes.size(), N), out.begin());
    return out;
}

struct GcmAnswer {
    std::string_view key;
    std::string_view nonce;
    std::string_view aad;
    std::string_view plaintext;
    std::string_view sealed;
};

constexpr std::string_view kSpecKey = "feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308";
constexpr std::string_view kSpecNonce = "cafebabefacedbaddecaf888";
constexpr std::string_view kSpecPlaintext = "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
                                            "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255";
constexpr std::string_view kSpecCiphertext = "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa"
                                             "8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662898015ad";

}

TEST_CASE("AES-256-GCM matches the known answers of the GCM specification (test cases 13 to 16)")
{
    const std::string spec16Plaintext(kSpecPlaintext.substr(0, 120));
    const std::string spec16Sealed = std::string(kSpecCiphertext.substr(0, 120)) + "76fc6ece0f4e1768cddf8853bb2d551b";
    const std::string spec15Sealed = std::string(kSpecCiphertext) + "b094dac5d93471bdec1a502270e3cc6c";
    const std::string zeros32(64, '0');
    const std::string zeros12(24, '0');
    const GcmAnswer answers[] = {
        {zeros32, zeros12, "", "", "530f8afbc74536b9a963b4f1c4cb738b"},
        {zeros32, zeros12, "", std::string_view("00000000000000000000000000000000"),
         "cea7403d4d606b6e074ec5d3baf39d18d0d1c8a799996bf0265b98b5d48ab919"},
        {kSpecKey, kSpecNonce, "", kSpecPlaintext, spec15Sealed},
        {kSpecKey, kSpecNonce, "feedfacedeadbeeffeedfacedeadbeefabaddad2", spec16Plaintext, spec16Sealed},
    };
    for (const GcmAnswer& answer : answers) {
        CAPTURE(answer.sealed);
        const auto key = fixed<kAesGcmKeySize>(answer.key);
        const auto nonce = fixed<kAesGcmNonceSize>(answer.nonce);
        const std::vector<std::uint8_t> aad = fromHex(answer.aad);
        const std::vector<std::uint8_t> plaintext = fromHex(answer.plaintext);
        const std::vector<std::uint8_t> sealed = aesGcmSeal(key, nonce, plaintext, aad);
        CHECK(toHex(sealed) == answer.sealed);
        const auto opened = aesGcmOpen(key, nonce, fromHex(answer.sealed), aad);
        REQUIRE(opened.has_value());
        CHECK(*opened == plaintext);
    }
}

TEST_CASE("AES-256-GCM rejects a changed tag, ciphertext, nonce, key or associated data")
{
    const auto key = fixed<kAesGcmKeySize>(kSpecKey);
    const auto nonce = fixed<kAesGcmNonceSize>(kSpecNonce);
    const std::vector<std::uint8_t> aad = fromHex("feedfacedeadbeef");
    const std::vector<std::uint8_t> plaintext = fromHex(kSpecPlaintext);
    const std::vector<std::uint8_t> sealed = aesGcmSeal(key, nonce, plaintext, aad);
    REQUIRE(sealed.size() == plaintext.size() + kAesGcmTagSize);
    CHECK(aesGcmOpen(key, nonce, sealed, aad).has_value());

    for (const std::size_t at : {std::size_t{0}, plaintext.size() / 2, sealed.size() - 1}) {
        std::vector<std::uint8_t> changed = sealed;
        changed[at] ^= 0x01U;
        CHECK_FALSE(aesGcmOpen(key, nonce, changed, aad).has_value());
    }
    auto otherNonce = nonce;
    otherNonce[11] ^= 0x01U;
    CHECK_FALSE(aesGcmOpen(key, otherNonce, sealed, aad).has_value());
    auto otherKey = key;
    otherKey[0] ^= 0x80U;
    CHECK_FALSE(aesGcmOpen(otherKey, nonce, sealed, aad).has_value());
    CHECK_FALSE(aesGcmOpen(key, nonce, sealed, fromHex("feedfacedeadbeee")).has_value());
    CHECK_FALSE(aesGcmOpen(key, nonce, sealed, {}).has_value());
    CHECK_FALSE(aesGcmOpen(key, nonce, std::span(sealed).first(kAesGcmTagSize - 1), aad).has_value());
    CHECK_FALSE(aesGcmOpen(key, nonce, std::span(sealed).first(sealed.size() - 1), aad).has_value());
}

TEST_CASE("AES-256-GCM round-trips random messages and an empty one")
{
    std::array<std::uint8_t, kAesGcmKeySize> key{};
    std::array<std::uint8_t, kAesGcmNonceSize> nonce{};
    randomBytes(key);
    randomBytes(nonce);
    for (const std::size_t size : {std::size_t{0}, std::size_t{1}, std::size_t{15}, std::size_t{16},
                                   std::size_t{17}, std::size_t{4096}, std::size_t{16406}}) {
        std::vector<std::uint8_t> message(size);
        randomBytes(message);
        const std::array<std::uint8_t, 4> aad{0x00, 0x00, 0x40, 0x16};
        const std::vector<std::uint8_t> sealed = aesGcmSeal(key, nonce, message, aad);
        CHECK(sealed.size() == size + kAesGcmTagSize);
        const auto opened = aesGcmOpen(key, nonce, sealed, aad);
        REQUIRE(opened.has_value());
        CHECK(*opened == message);
    }
}

TEST_CASE("data protected for the current user opens again only with the same entropy and unchanged (DPAPI)")
{
    std::array<std::uint8_t, 32> secret{};
    randomBytes(secret);
    const std::span<const std::uint8_t> entropy = bytes("GameScrcpy test entropy");
    const auto sealed = protectForUser(secret, entropy);
    REQUIRE(sealed.has_value());
    CHECK(sealed->size() > secret.size());
    CHECK(std::search(sealed->begin(), sealed->end(), secret.begin(), secret.end()) == sealed->end());
    const auto opened = unprotectForUser(*sealed, entropy);
    REQUIRE(opened.has_value());
    CHECK(std::ranges::equal(*opened, secret));

    CHECK_FALSE(unprotectForUser(*sealed, bytes("other entropy")).has_value());
    CHECK_FALSE(unprotectForUser(*sealed, {}).has_value());
    std::vector<std::uint8_t> changed = *sealed;
    changed.back() ^= 0x01U;
    CHECK_FALSE(unprotectForUser(changed, entropy).has_value());
    CHECK_FALSE(unprotectForUser({}, entropy).has_value());

    const auto again = protectForUser(secret, entropy);
    REQUIRE(again.has_value());
    const auto reopened = unprotectForUser(*again, entropy);
    REQUIRE(reopened.has_value());
    CHECK(std::ranges::equal(*reopened, secret));

    const auto bare = protectForUser(secret, {});
    REQUIRE(bare.has_value());
    const auto bareOpened = unprotectForUser(*bare, {});
    REQUIRE(bareOpened.has_value());
    CHECK(std::ranges::equal(*bareOpened, secret));
}
