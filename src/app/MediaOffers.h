#pragma once

#include <gs/exthost/Module.h>

#include <QString>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gs::app {

struct MediaSourceDevice {
    std::string extension;
    QString extensionName;
    exthost::MediaKind kind = exthost::MediaKind::Video;
    std::string source;
    QString sourceTitle;
    std::string id;
    QString name;
    std::vector<std::string> kinds;

    friend bool operator==(const MediaSourceDevice&, const MediaSourceDevice&) = default;
};

struct MediaSinkOffer {
    std::string extension;
    QString extensionName;
    std::string name;
    QString title;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t fps = 0;
    bool audio = false;

    friend bool operator==(const MediaSinkOffer&, const MediaSinkOffer&) = default;
};

struct ExporterOffer {
    std::string extension;
    QString extensionName;
    std::string name;
    QString title;
    std::string fileExtension;

    friend bool operator==(const ExporterOffer&, const ExporterOffer&) = default;
};

class SourceLease final
{
public:
    SourceLease(std::weak_ptr<exthost::Module> module, std::shared_ptr<exthost::MediaStream> stream) noexcept
        : m_module(std::move(module))
        , m_stream(std::move(stream))
    {
    }

    ~SourceLease()
    {
        if (const std::shared_ptr<exthost::Module> alive = m_module.lock()) {
            alive->closeSource(m_stream->id());
        }
    }

    SourceLease(const SourceLease&) = delete;
    SourceLease& operator=(const SourceLease&) = delete;

    [[nodiscard]] exthost::MediaStream& stream() const noexcept { return *m_stream; }

private:
    std::weak_ptr<exthost::Module> m_module;
    std::shared_ptr<exthost::MediaStream> m_stream;
};

class SinkLease final
{
public:
    SinkLease(std::weak_ptr<exthost::Module> module, std::shared_ptr<exthost::SinkPort> port) noexcept
        : m_module(std::move(module))
        , m_port(std::move(port))
    {
    }

    ~SinkLease()
    {
        if (const std::shared_ptr<exthost::Module> alive = m_module.lock()) {
            alive->closeSink(m_port->id());
        }
    }

    SinkLease(const SinkLease&) = delete;
    SinkLease& operator=(const SinkLease&) = delete;

    [[nodiscard]] exthost::SinkPort& port() const noexcept { return *m_port; }
    [[nodiscard]] std::shared_ptr<exthost::SinkPort> sharedPort() const noexcept { return m_port; }

private:
    std::weak_ptr<exthost::Module> m_module;
    std::shared_ptr<exthost::SinkPort> m_port;
};

}
