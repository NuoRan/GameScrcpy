#pragma once

#include "AiDrafts.h"
#include "AiService.h"
#include "Settings.h"

#include <QLatin1StringView>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <optional>

class QJSEngine;
class QNetworkReply;
class QQmlEngine;

namespace gs::app {

class AppSettings;

inline constexpr QLatin1StringView kAiClient{"GameScrcpy AI"};

class AiAssistant final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString service READ service NOTIFY setupChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY setupChanged FINAL)
    Q_PROPERTY(QString missing READ missing NOTIFY setupChanged FINAL)
    Q_PROPERTY(bool hasKey READ hasKey NOTIFY setupChanged FINAL)
    Q_PROPERTY(QString keyTarget READ keyTarget NOTIFY setupChanged FINAL)
    Q_PROPERTY(QString keyProblem READ keyProblem NOTIFY setupChanged FINAL)
    Q_PROPERTY(QString defaultAddress READ defaultAddress NOTIFY setupChanged FINAL)
    Q_PROPERTY(QString host READ host NOTIFY setupChanged FINAL)
    Q_PROPERTY(QString testState READ testState NOTIFY testChanged FINAL)
    Q_PROPERTY(QString testMessage READ testMessage NOTIFY testChanged FINAL)
    Q_PROPERTY(bool explanationWanted READ explanationWanted NOTIFY explanationChanged FINAL)
    Q_PROPERTY(QStringList models READ models NOTIFY modelsChanged FINAL)
    Q_PROPERTY(QString modelsState READ modelsState NOTIFY modelsChanged FINAL)
    Q_PROPERTY(QString modelsProblem READ modelsProblem NOTIFY modelsChanged FINAL)

public:
    static constexpr QLatin1StringView kKeyPrefix{"GameScrcpy/ai/"};

    explicit AiAssistant(AppSettings& appSettings, const QString& keyPrefix = QString(kKeyPrefix),
                         QObject* parent = nullptr);
    ~AiAssistant() override;
    AiAssistant(const AiAssistant&) = delete;
    AiAssistant& operator=(const AiAssistant&) = delete;

    static AiAssistant* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);
    [[nodiscard]] static AiAssistant* instance() noexcept { return s_instance; }

    [[nodiscard]] QString service() const;
    [[nodiscard]] bool ready() const;
    [[nodiscard]] QString missing() const;
    [[nodiscard]] bool hasKey() const noexcept { return m_hasKey; }
    [[nodiscard]] QString keyTarget() const;
    [[nodiscard]] QString keyProblem() const { return m_keyProblem; }
    [[nodiscard]] QString defaultAddress() const;
    [[nodiscard]] QString host() const;
    [[nodiscard]] QString testState() const { return m_testState; }
    [[nodiscard]] QString testMessage() const { return m_testMessage; }
    [[nodiscard]] bool explanationWanted() const;
    [[nodiscard]] QStringList models() const { return m_models; }
    [[nodiscard]] QString modelsState() const { return m_modelsState; }
    [[nodiscard]] QString modelsProblem() const { return m_modelsProblem; }

    Q_INVOKABLE bool saveKey(const QString& key);
    Q_INVOKABLE bool forgetKey();
    Q_INVOKABLE void testConnection();
    Q_INVOKABLE void fetchModels();
    Q_INVOKABLE void acknowledgeExplanation(bool remember);
    Q_INVOKABLE void openSettings();

    [[nodiscard]] std::optional<AiConnection> connection() const;
    [[nodiscard]] std::unique_ptr<AiProvider> provider(bool chat = false);
    [[nodiscard]] AiToolRunner& tools() noexcept { return *m_tools; }
    [[nodiscard]] AiDraftStore& drafts() noexcept { return *m_drafts; }

signals:
    void setupChanged();
    void testChanged();
    void explanationChanged();
    void modelsChanged();
    void settingsWanted();

private:
    void settingsChanged();
    void readKey();
    void setTest(const QString& state, const QString& message);
    [[nodiscard]] settings::Assistant setup() const;
    [[nodiscard]] AiKind kind() const;

    static inline AiAssistant* s_instance = nullptr;

    AppSettings& m_settings;
    const QString m_keyPrefix;
    QNetworkAccessManager m_network;
    std::unique_ptr<AiToolRunner> m_tools;
    std::unique_ptr<AiDraftStore> m_drafts;
    std::unique_ptr<AiProvider> m_test;
    QString m_keyProblem;
    QString m_testState;
    QString m_testMessage;
    QString m_keyService;
    QString m_endpoint;
    QString m_modelsKey;
    QStringList m_models;
    QString m_modelsState;
    QString m_modelsProblem;
    QPointer<QNetworkReply> m_modelsReply;
    bool m_hasKey = false;
    bool m_explain = true;
    bool m_explained = false;
};

}
