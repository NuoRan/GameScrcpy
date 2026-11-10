#pragma once

#include "McpTools.h"

namespace gs::app {

class DeviceManager;

[[nodiscard]] McpDevices mcpDevicesOf(DeviceManager& devices);

}
