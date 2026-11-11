#include "KeyTools.h"

#include "ScriptProject.h"

#include <gs/ext/Minisign.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace gs::cli {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kKeygenUsage = "gs ext keygen <secret key file> [--force] [--json]";

int keygen(const fs::path& given, bool force, bool json)
{
    std::error_code error;
    const fs::path secret = fs::absolute(given, error);
    if (error || !secret.has_filename()) {
        std::cerr << "gs ext keygen: " << utf8(given) << " is not a file name\n";
        return kUsage;
    }
    fs::path shared = secret;
    shared.replace_extension(".pub");
    if (shared == secret) {
        std::cerr << "gs ext keygen: name the secret key file something other than .pub\n";
        return kUsage;
    }
    for (const fs::path& file : {secret, shared}) {
        std::error_code missing;
        if (!force && fs::exists(file, missing)) {
            std::cerr << "gs ext keygen: " << utf8(file) << " exists; pass --force to replace it\n";
            return kProblems;
        }
    }
    const ext::MinisignSecretKey key = ext::generateMinisignKey();
    const ext::MinisignKey publicKey = ext::minisignPublicKey(key);
    if (!writeText(secret, ext::minisignSecretKeyFile(key))) {
        std::cerr << "gs ext keygen: cannot write " << utf8(secret) << '\n';
        return kProblems;
    }
    if (!writeText(shared, ext::minisignPublicKeyFile(publicKey))) {
        std::cerr << "gs ext keygen: cannot write " << utf8(shared) << '\n';
        return kProblems;
    }
    const std::string authorKey = ext::minisignKeyText(publicKey);
    if (json) {
        const nlohmann::ordered_json report = {{"ok", true},
                                               {"secretKey", utf8(secret)},
                                               {"publicKey", utf8(shared)},
                                               {"keyId", ext::minisignKeyId(publicKey)},
                                               {"authorKey", authorKey}};
        std::cout << report.dump(2) << '\n';
        return kSuccess;
    }
    std::cout << "wrote the secret key to " << utf8(secret)
              << "; keep it private: whoever has it can sign extensions as you\n"
              << "wrote the public key to " << utf8(shared) << '\n'
              << "key id " << ext::minisignKeyId(publicKey) << '\n'
              << "put this line in manifest.json: \"authorKey\": \"" << authorKey << "\"\n"
              << "then pack with: gs pack <folder> --sign " << utf8(secret) << '\n';
    return kSuccess;
}

}

std::optional<int> runKeyCommand(int argc, wchar_t** argv)
{
    if (argc < 3 || std::wstring_view(argv[1]) != L"ext" || std::wstring_view(argv[2]) != L"keygen") {
        return std::nullopt;
    }
    std::vector<fs::path> files;
    bool force = false;
    bool json = false;
    bool fine = true;
    for (int index = 3; index < argc; ++index) {
        const std::wstring_view argument(argv[index]);
        if (argument == L"--force" && !force) {
            force = true;
        } else if (argument == L"--json" && !json) {
            json = true;
        } else if (argument.starts_with(L'-')) {
            fine = false;
        } else {
            files.emplace_back(argument);
        }
    }
    if (!fine || files.size() != 1) {
        std::cerr << "usage: " << kKeygenUsage << '\n';
        return kUsage;
    }
    return keygen(files.front(), force, json);
}

}
