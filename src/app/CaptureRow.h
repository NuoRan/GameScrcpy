#pragma once

#include "AoaTouchSink.h"
#include "CaptureCards.h"
#include "CaptureFeed.h"
#include "ControlChannel.h"
#include "TouchMethods.h"

#include <QString>
#include <QStringList>

#include <cstdint>
#include <memory>
#include <string>

namespace gs::app {

struct CaptureRow {
    CaptureRow() = default;
    CaptureRow(const CaptureRow&) = delete;
    CaptureRow& operator=(const CaptureRow&) = delete;
    ~CaptureRow() { channel->detach(); }

    QString name;
    std::wstring link;
    std::wstring container;
    bool demo = false;
    std::unique_ptr<CaptureFeed> feed;
    std::shared_ptr<ControlChannel> channel = std::make_shared<ControlChannel>();
    DroppingSink dropping;
    std::shared_ptr<ControlSink> logging;
    std::unique_ptr<AoaPort> port;
    std::unique_ptr<AoaTouchSink> aoa;
    std::shared_ptr<ExtensionTouch> extension;
    QString touch;
    QString unavailable;
    std::uint32_t touchGeneration = 0;
    QStringList formats;
    bool cropEditing = false;
    bool goneShown = false;
    QString drivenBy;
    std::string sourceExtension;
    QString sourceExtensionName;
};

}
