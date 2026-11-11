#pragma once

#include "gs/ext/Diagnostics.h"
#include "gs/ext/Minisign.h"
#include "gs/ext/NativeCheck.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace gs::ext {

inline constexpr std::string_view kPublishersFolder = ".publishers";

[[nodiscard]] std::string checksumsOf(const PackageContents& contents, std::vector<Diagnostic>& problems);
[[nodiscard]] std::string signingComment(const Manifest& manifest);
[[nodiscard]] std::vector<Diagnostic> packSigned(const std::filesystem::path& directory,
                                                 const std::filesystem::path& archive, const MinisignSecretKey& key,
                                                 std::string_view trustedComment);

[[nodiscard]] std::optional<MinisignKey> rememberedPublisher(const std::filesystem::path& root, std::string_view id);
[[nodiscard]] std::error_code rememberPublisher(const std::filesystem::path& root, std::string_view id,
                                                const MinisignKey& key);
[[nodiscard]] std::error_code forgetPublisher(const std::filesystem::path& root, std::string_view id);
[[nodiscard]] std::optional<MinisignKey> publisherOf(const std::filesystem::path& root, std::string_view id);
[[nodiscard]] std::optional<Diagnostic> publisherProblem(const std::optional<MinisignKey>& remembered,
                                                         const SignatureCheck& incoming);

}
