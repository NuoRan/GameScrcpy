#pragma once

#include "ControlChannel.h"

#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

namespace gs::app {

class ExtensionTouch : public ControlSink
{
public:
    virtual void setVideo(std::uint16_t session, int rotation) = 0;
    [[nodiscard]] virtual bool fellBack() const = 0;
};

class TouchMethodSource
{
public:
    virtual ~TouchMethodSource() = default;

    [[nodiscard]] virtual std::unique_ptr<ExtensionTouch> openTouch(std::string_view key, std::string_view device,
                                                                     ControlSink& injection,
                                                                     std::function<void()> fellBack) = 0;
    [[nodiscard]] virtual QString touchTitle(std::string_view key) const = 0;
};

}
