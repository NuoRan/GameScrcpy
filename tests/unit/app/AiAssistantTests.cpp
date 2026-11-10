#include "AiAssistant.h"
#include "AppSettings.h"
#include "McpTestSupport.h"

#include <gs/platform/Credentials.h>
#include <gs/platform/Crypto.h>

#include <doctest/doctest.h>

#include <QObject>
#include <QString>
#include <QVariant>

#include <array>
#include <cstdint>
#include <utility>

using gs::app::AiAssistant;
using gs::app::AppSettings;
using namespace Qt::StringLiterals;

namespace {

QString testPrefix()
{
    std::array<std::uint8_t, 6> salt{};
    gs::platform::randomBytes(salt);
    return u"GameScrcpy/test-%1/ai/"_s.arg(QString::fromStdString(gs::platform::toHex(salt)));
}

class ForgetKeys
{
public:
    explicit ForgetKeys(QString prefix)
        : m_prefix(std::move(prefix))
    {
    }
    ~ForgetKeys()
    {
        for (const char* service : {"openai", "anthropic"}) {
            static_cast<void>(gs::platform::deleteCredential((m_prefix + QString::fromLatin1(service)).toStdString()));
        }
    }
    ForgetKeys(const ForgetKeys&) = delete;
    ForgetKeys& operator=(const ForgetKeys&) = delete;

private:
    QString m_prefix;
};

struct Setup {
    gs::test::McpApplication application;
    gs::test::McpFolder folder;
    AppSettings settings{folder.path()};
    QString prefix = testPrefix();
    ForgetKeys forget{prefix};
};

}

TEST_CASE("the explanation shows before the first message once a run, and never again once remembered")
{
    Setup setup;
    {
        AiAssistant assistant(setup.settings, setup.prefix);
        CHECK(AiAssistant::instance() == &assistant);
        CHECK(assistant.explanationWanted());
        assistant.acknowledgeExplanation(false);
        CHECK_FALSE(assistant.explanationWanted());
        CHECK(setup.settings.values().value(u"assistant.explain"_s).toBool());
    }
    CHECK(AiAssistant::instance() == nullptr);
    {
        AiAssistant again(setup.settings, setup.prefix);
        CHECK(again.explanationWanted());
        again.acknowledgeExplanation(true);
        CHECK_FALSE(again.explanationWanted());
        CHECK_FALSE(setup.settings.values().value(u"assistant.explain"_s).toBool());
    }
    AiAssistant later(setup.settings, setup.prefix);
    CHECK_FALSE(later.explanationWanted());
    REQUIRE(setup.settings.setValue(u"assistant.explain"_s, true));
    CHECK(later.explanationWanted());
}

TEST_CASE("the assistant is ready with a model, a web address and, for Anthropic, a key")
{
    Setup setup;
    AiAssistant assistant(setup.settings, setup.prefix);
    CHECK(assistant.service() == u"openai");
    CHECK(assistant.keyTarget() == setup.prefix + u"openai"_s);
    CHECK(assistant.defaultAddress() == u"https://api.openai.com/v1");
    CHECK_FALSE(assistant.ready());
    CHECK(assistant.missing().contains(u"model"));
    CHECK_FALSE(assistant.connection().has_value());
    CHECK(assistant.provider() == nullptr);

    REQUIRE(setup.settings.setValue(u"assistant.openai.model"_s, u"gpt-test"_s));
    CHECK(assistant.ready());
    const auto openAi = assistant.connection();
    REQUIRE(openAi.has_value());
    CHECK(openAi->kind == gs::app::AiKind::OpenAi);
    CHECK(openAi->url.toString() == u"https://api.openai.com/v1/chat/completions");
    CHECK(openAi->model == u"gpt-test");
    CHECK(openAi->key.isEmpty());
    CHECK(assistant.host() == u"api.openai.com");

    REQUIRE(setup.settings.setValue(u"assistant.openai.address"_s, u"ftp://example.com"_s));
    CHECK_FALSE(assistant.ready());
    CHECK(assistant.missing().contains(u"address"));
    REQUIRE(setup.settings.setValue(u"assistant.openai.address"_s, u"http://localhost:11434/v1"_s));
    REQUIRE(assistant.ready());
    CHECK(assistant.connection()->url.toString() == u"http://localhost:11434/v1/chat/completions");

    REQUIRE(setup.settings.setValue(u"assistant.service"_s, u"anthropic"_s));
    CHECK(assistant.service() == u"anthropic");
    CHECK(assistant.keyTarget() == setup.prefix + u"anthropic"_s);
    CHECK(assistant.defaultAddress() == u"https://api.anthropic.com");
    CHECK_FALSE(assistant.ready());
    REQUIRE(setup.settings.setValue(u"assistant.anthropic.model"_s, u"claude-test"_s));
    CHECK_FALSE(assistant.ready());
    CHECK(assistant.missing().contains(u"API key"));

    REQUIRE(assistant.saveKey(u"  sk-ant-test\n"_s));
    CHECK(assistant.hasKey());
    CHECK(assistant.ready());
    const auto anthropic = assistant.connection();
    REQUIRE(anthropic.has_value());
    CHECK(anthropic->kind == gs::app::AiKind::Anthropic);
    CHECK(anthropic->key == u"sk-ant-test");
    CHECK(anthropic->url.toString() == u"https://api.anthropic.com/v1/messages");
    CHECK(assistant.provider() != nullptr);

    REQUIRE(assistant.forgetKey());
    CHECK_FALSE(assistant.hasKey());
    CHECK_FALSE(assistant.ready());
}

TEST_CASE("each service keeps its own API key in Windows Credential Manager, under its own target name")
{
    Setup setup;
    AiAssistant assistant(setup.settings, setup.prefix);
    CHECK_FALSE(assistant.hasKey());
    CHECK_FALSE(assistant.saveKey(u"   "_s));
    REQUIRE(assistant.saveKey(u"sk-open"_s));
    CHECK(assistant.hasKey());
    CHECK(assistant.keyProblem().isEmpty());
    const auto stored = gs::platform::readCredential((setup.prefix + u"openai"_s).toStdString());
    REQUIRE(stored.has_value());
    CHECK(*stored == "sk-open");

    REQUIRE(setup.settings.setValue(u"assistant.service"_s, u"anthropic"_s));
    CHECK_FALSE(assistant.hasKey());
    REQUIRE(setup.settings.setValue(u"assistant.service"_s, u"openai"_s));
    CHECK(assistant.hasKey());

    REQUIRE(assistant.forgetKey());
    CHECK_FALSE(assistant.hasKey());
    const auto gone = gs::platform::readCredential((setup.prefix + u"openai"_s).toStdString());
    REQUIRE_FALSE(gone.has_value());
    CHECK(gs::platform::credentialMissing(gone.error()));
    REQUIRE(assistant.forgetKey());
}

TEST_CASE("testing the connection without a model says what is missing, and the settings can be asked for")
{
    Setup setup;
    AiAssistant assistant(setup.settings, setup.prefix);
    assistant.testConnection();
    CHECK(assistant.testState() == u"failed");
    CHECK(assistant.testMessage() == assistant.missing());

    int asked = 0;
    QObject::connect(&assistant, &AiAssistant::settingsWanted, [&asked] { ++asked; });
    assistant.openSettings();
    CHECK(asked == 1);
}
