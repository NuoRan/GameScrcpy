#pragma once

#include "AiService.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace gs::app {

class ExtensionHost;
class NativeExtensions;
class NativeJobs;

struct AiProviderInfo {
    QString extension;
    QString name;
    QString title;
    QString extensionName;
    QString models;
    QString chat;

    [[nodiscard]] QString id() const { return extension + u'/' + name; }

    friend bool operator==(const AiProviderInfo&, const AiProviderInfo&) = default;
};

struct AiProviderModel {
    QString id;
    QString name;
    AiModelTraits traits;

    friend bool operator==(const AiProviderModel&, const AiProviderModel&) = default;
};

struct AiProviderModels {
    std::vector<AiProviderModel> models;
    QString problem;
};

[[nodiscard]] AiProviderModels aiProviderModelsOf(const QByteArray& json);

class AiProviders final : public QObject
{
    Q_OBJECT

public:
    static constexpr std::string_view kPermission = "ai.provide";
    static constexpr std::size_t kMostModels = 256;

    explicit AiProviders(std::filesystem::path requests, QObject* parent = nullptr);
    ~AiProviders() override;
    AiProviders(const AiProviders&) = delete;
    AiProviders& operator=(const AiProviders&) = delete;

    void use(NativeExtensions& native, NativeJobs& jobs);
    void follow(ExtensionHost& extensions);
    void setProviders(std::vector<AiProviderInfo> providers);

    [[nodiscard]] const std::vector<AiProviderInfo>& providers() const noexcept { return m_providers; }
    [[nodiscard]] const AiProviderInfo* find(const QString& id) const;
    [[nodiscard]] AiProviderModels models(const QString& id) const;
    [[nodiscard]] std::unique_ptr<AiProvider> provider(const QString& id, const AiConnection& connection) const;

signals:
    void providersChanged();

private:
    std::filesystem::path m_requests;
    QPointer<NativeExtensions> m_native;
    QPointer<NativeJobs> m_jobs;
    std::vector<AiProviderInfo> m_providers;
};

}
