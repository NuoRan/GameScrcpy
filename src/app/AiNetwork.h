#pragma once

#include "AiService.h"

#include <chrono>
#include <memory>

class QNetworkAccessManager;

namespace gs::app {

inline constexpr std::chrono::milliseconds kAiIdleLimit{120'000};

void aiUseSystemProxy(QNetworkAccessManager& network);
[[nodiscard]] std::unique_ptr<AiProvider> makeAiProvider(QNetworkAccessManager& network, AiConnection connection);

}
