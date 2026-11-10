#pragma once

#include "HelpLibrary.h"

#include <QString>

#include <vector>

namespace gs::app {

class ExtensionHost;

[[nodiscard]] std::vector<ExtensionHelp> extensionHelp(const ExtensionHost& host, const QString& language);
void watchExtensionHelp(HelpLibrary& help, ExtensionHost& host);

}
