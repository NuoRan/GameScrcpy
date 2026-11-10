#include "gs/ext/Minisign.h"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

using namespace gs::ext;

namespace {

constexpr std::string_view kAuthorKey = "RWQYB7al9OPSwU7aritYZ3nmUlPQk5fh7N4Izs3H4UUtma8A/RO4rFBZ";
constexpr std::string_view kOtherKey = "RWSId2ZVRDMiEa7DCLu1nxR+gMiGsuYNvpyJdwItkg7PqsjnQoi009w+";
constexpr std::string_view kMessage = "GameScrcpy extension test\n";
constexpr std::string_view kHashedSignature =
    "untrusted comment: signature from minisign secret key\n"
    "RUQYB7al9OPSwUANBFt6q/9U+rhpays4qECxKyUdhBvqCvlI0OMKpOUEn5/fB7LPLEjENiZ31O078KFRUsh/xEgQuM9JsihudAs=\n"
    "trusted comment: timestamp:1762430400\tfile:message.txt\thashed\n"
    "IKNtgn1Om3svjkSXOHvdBKJRN5ZWuMVcVsLam6LjVWHgqB3iYEBYdyc9elHL9CrBndnKwIY5QVoXC+AijkoGDA==\n";
constexpr std::string_view kLegacySignature =
    "untrusted comment: signature from minisign secret key\n"
    "RWQYB7al9OPSwR9KRC/2EIySs7a4rR2DrTprvGVmaO+EulMZ1U/Ok7OC3cZEEGILS19/0lBi6NT2a0cz40cDLpES+bFo/1KY+g8=\n"
    "trusted comment: timestamp:1762430400\tfile:message.txt\n"
    "LQGmnsmP5ArXfllDgjJK8nSguV3AokFiJGf25DuDXWc1/f7Dx1npL+ld8ZwRE/8NRpHZLHr9pOaXExdHbbHhDA==\n";
constexpr std::string_view kOtherSignature =
    "untrusted comment: signature from minisign secret key\n"
    "RUSId2ZVRDMiER0LSSkUUrUVA7EVGMr7+O6oN+mWbNtZeauGCU5lHWzScwNosWlcXXdHVouf+FUspgx2O0+G/cfmNG6bBqDTXgc=\n"
    "trusted comment: timestamp:1762430400\tfile:message.txt\thashed\n"
    "iVhpuHitsdYECMtk8pLu4Xg1YamHTXTwc5uRSAramLncAev6SCQZ2SAY7vtBsmWwmjntIVHcJXFdKwo8oDDFBA==\n";

struct RfcVector {
    std::string_view publicKey;
    std::string_view message;
    std::string_view signature;
};

constexpr std::array<RfcVector, 3> kRfcVectors{{
    {"d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
     "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe2"
     "4655141438e7a100b"},
    {"3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
     "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302ae"
     "eb00d291612bb0c00"},
    {"fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
     "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28"
     "dc027beceea1ec40a"},
}};

int nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    return c - 'a' + 10;
}

std::string unhex(std::string_view text)
{
    std::string bytes;
    for (std::size_t i = 0; i + 1 < text.size(); i += 2) {
        bytes.push_back(static_cast<char>(nibble(text[i]) * 16 + nibble(text[i + 1])));
    }
    return bytes;
}

template <std::size_t N>
std::array<std::uint8_t, N> arrayOf(std::string_view text)
{
    const std::string bytes = unhex(text);
    std::array<std::uint8_t, N> out{};
    for (std::size_t i = 0; i < N && i < bytes.size(); ++i) {
        out[i] = static_cast<std::uint8_t>(bytes[i]);
    }
    return out;
}

std::string hexOf(const Digest512& digest)
{
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::string text;
    for (const std::uint8_t byte : digest) {
        text.push_back(kDigits[byte >> 4]);
        text.push_back(kDigits[byte & 0x0f]);
    }
    return text;
}

std::string replaced(std::string_view text, std::string_view from, std::string_view to)
{
    std::string out(text);
    const std::size_t at = out.find(from);
    REQUIRE(at != std::string::npos);
    out.replace(at, from.size(), to);
    return out;
}

}

TEST_CASE("SHA-512 and BLAKE2b-512 give the published digests, also across block boundaries")
{
    CHECK(hexOf(sha512("abc")) ==
          "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d44236"
          "43ce80e2a9ac94fa54ca49f");
    CHECK(hexOf(sha512("")) ==
          "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd4"
          "7417a81a538327af927da3e");
    CHECK(hexOf(sha512("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnop"
                       "qrsmnopqrstnopqrstu")) ==
          "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018501d289e4900f7e4331b99dec4b5433ac7d329eeb"
          "6dd26545e96e55b874be909");
    CHECK(hexOf(sha512(std::string(1000, 'a'))) ==
          "67ba5535a46e3f86dbfbed8cbbaf0125c76ed549ff8b0b9e03e0c88cf90fa634fa7b12b47d77b694de488ace8d9a65967dc96df59"
          "9727d3292a8d9d447709c97");
    CHECK(hexOf(blake2b512("abc")) ==
          "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38aa8d"
          "bf1925ab92386edd4009923");
    CHECK(hexOf(blake2b512("")) ==
          "786a02f742015903c6c6fd852552d272912f4740e15847618a86e217f71f5419d25e1031afee585313896444934eb04b903a685b1"
          "448b755d56f701afe9be2ce");
    CHECK(hexOf(blake2b512(std::string(128, 'a'))) ==
          "fc6c71f688f43ea7d60817478808f3cac753e61571865c95adbc2d9122c943a76b92c2cb1047ef3fe7bf6e436ec1d0a99a9e5b216"
          "780bf7fed9d7ca91d3a8f3b");
    CHECK(hexOf(blake2b512(std::string(300, 'a'))) ==
          "a2ff3040eda405b929c2fc2fd93e8add6ac3bb5369b679bae170ac6956863ca006285f132a868000fc3fae5bc696e5d17fe3fddfb"
          "4a342876c40451184742986");
}

TEST_CASE("Ed25519 accepts the RFC 8032 test signatures and rejects changed or non-canonical ones")
{
    for (const RfcVector& vector : kRfcVectors) {
        CAPTURE(vector.message);
        const Ed25519Key key = arrayOf<32>(vector.publicKey);
        const Ed25519Signature signature = arrayOf<64>(vector.signature);
        const std::string message = unhex(vector.message);
        CHECK(verifyEd25519(message, signature, key));
        CHECK_FALSE(verifyEd25519(message + "x", signature, key));
        Ed25519Signature flipped = signature;
        flipped[5] = static_cast<std::uint8_t>(flipped[5] ^ 0x01);
        CHECK_FALSE(verifyEd25519(message, flipped, key));
        Ed25519Signature late = signature;
        late[40] = static_cast<std::uint8_t>(late[40] ^ 0x20);
        CHECK_FALSE(verifyEd25519(message, late, key));
    }
    const Ed25519Signature stretched = arrayOf<64>(
        "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901554c8c7872aa064e049dbb3013fbf29380d25bf5f0595bb"
        "e24655141438e7a101b");
    CHECK_FALSE(verifyEd25519("", stretched, arrayOf<32>(kRfcVectors[0].publicKey)));
    CHECK_FALSE(verifyEd25519("", arrayOf<64>(kRfcVectors[0].signature), arrayOf<32>(kRfcVectors[1].publicKey)));
}

TEST_CASE("minisign signatures made with the author key are valid, prehashed or not")
{
    const std::optional<MinisignKey> key = parseMinisignKey(kAuthorKey);
    REQUIRE(key.has_value());
    CHECK(minisignKeyId(*key) == "C1D2E3F4A5B60718");
    CHECK(verifyMinisign(kMessage, kHashedSignature, *key) == MinisignResult::Valid);
    CHECK(verifyMinisign(kMessage, kLegacySignature, *key) == MinisignResult::Valid);
    std::string crlf;
    for (const char c : kHashedSignature) {
        if (c == '\n') {
            crlf += '\r';
        }
        crlf += c;
    }
    CHECK(verifyMinisign(kMessage, crlf, *key) == MinisignResult::Valid);
    const std::optional<MinisignKey> other = parseMinisignKey(kOtherKey);
    REQUIRE(other.has_value());
    CHECK(minisignKeyId(*other) == "1122334455667788");
    CHECK(verifyMinisign(kMessage, kOtherSignature, *other) == MinisignResult::Valid);
}

TEST_CASE("minisign tells another key, a changed message, a changed trusted comment and garbage apart")
{
    const MinisignKey key = *parseMinisignKey(kAuthorKey);
    CHECK(verifyMinisign(kMessage, kOtherSignature, key) == MinisignResult::OtherKey);
    CHECK(verifyMinisign("GameScrcpy extension test!\n", kHashedSignature, key) == MinisignResult::BadSignature);
    CHECK(verifyMinisign("GameScrcpy extension test!\n", kLegacySignature, key) == MinisignResult::BadSignature);
    CHECK(verifyMinisign(kMessage, replaced(kHashedSignature, "file:message.txt", "file:other.txt"), key) ==
          MinisignResult::BadComment);
    CHECK(verifyMinisign(kMessage, replaced(kHashedSignature, "untrusted comment:", "comment:"), key) ==
          MinisignResult::Malformed);
    CHECK(verifyMinisign(kMessage, "not a signature", key) == MinisignResult::Malformed);
    CHECK(verifyMinisign(kMessage, replaced(kHashedSignature, "RUQYB7al", "RUQYB7a="), key) ==
          MinisignResult::Malformed);
    CHECK_FALSE(parseMinisignKey("").has_value());
    CHECK_FALSE(parseMinisignKey("RWQYB7al9OPSwU7aritYZ3nmUlPQk5fh7N4Izs3H4UUtma8A/RO4rFB").has_value());
    CHECK_FALSE(parseMinisignKey("RUQYB7al9OPSwU7aritYZ3nmUlPQk5fh7N4Izs3H4UUtma8A/RO4rFBZ").has_value());
    CHECK_FALSE(parseMinisignKey(std::string(kAuthorKey) + "\n").has_value());
}
