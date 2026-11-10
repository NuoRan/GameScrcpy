#pragma once

#include "gs/script/Resources.h"
#include "gs/script/Services.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::script {

struct ApiFile {
    std::string path;
    std::string contents;
};

using ApiTranslate = std::function<std::string(std::string_view text)>;

inline constexpr std::string_view kApiTextContext = "ScriptApi";

[[nodiscard]] std::vector<ApiFile> apiFiles();
[[nodiscard]] std::vector<ApiFile> apiDocs(const ApiTranslate& translate);
[[nodiscard]] std::vector<std::string> apiTexts();
[[nodiscard]] std::string profileTypes(const std::vector<ControlInfo>& controls, const std::vector<StateInfo>& states,
                                       const Resources& resources = {});
[[nodiscard]] std::string extensionTypes(const std::vector<NodeInfo>& nodes);
[[nodiscard]] std::string scriptTsconfig();
[[nodiscard]] std::string profileTsconfig();

}
