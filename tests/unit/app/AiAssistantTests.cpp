#include "AiAssistant.h"
#include "AppSettings.h"
#include "McpTestSupport.h"

#include <gs/platform/Credentials.h>
#include <gs/platform/Crypto.h>

#include <doctest/doctest.h>

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QVariant>

#include <array>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

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

class ModelList final
{
public:
    explicit ModelList(QByteArray reply)
        : m_reply(std::move(reply))
    {
        listening = m_server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&m_server, &QTcpServer::newConnection, &m_server, [this] {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) {
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] { read(*socket); });
            }
        });
    }

    ModelList(const ModelList&) = delete;
    ModelList& operator=(const ModelList&) = delete;

    [[nodiscard]] QString address() const { return u"http://127.0.0.1:%1/v1"_s.arg(m_server.serverPort()); }

    bool listening = false;
    std::vector<QByteArray> heads;

private:
    void read(QTcpSocket& socket)
    {
        QByteArray& buffer = m_buffers[&socket];
        buffer += socket.readAll();
        const qsizetype end = buffer.indexOf("\r\n\r\n");
        if (end < 0) {
            return;
        }
        heads.push_back(buffer.left(end));
        m_buffers.erase(&socket);
        socket.write(m_reply);
        socket.flush();
        socket.disconnectFromHost();
    }

    QTcpServer m_server;
    QByteArray m_reply;
    std::map<QTcpSocket*, QByteArray> m_buffers;
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

TEST_CASE("ADR-0099 2026-11-10: a conversation uses its own model on the same service, and the default otherwise")
{
    Setup setup;
    AiAssistant assistant(setup.settings, setup.prefix);
    CHECK_FALSE(assistant.chatConnection({}).has_value());
    REQUIRE(setup.settings.setValue(u"assistant.openai.model"_s, u"gpt-5"_s));
    REQUIRE(setup.settings.setValue(u"assistant.openai.effort"_s, u"high"_s));
    REQUIRE(setup.settings.setValue(u"assistant.openai.thinking"_s, true));
    CHECK(assistant.defaults() == gs::app::AiChoice{.model = u"gpt-5"_s,
                                                    .think = true,
                                                    .effort = u"high"_s,
                                                    .service = u"openai"_s});
    CHECK(assistant.defaultChoice().value(u"effort"_s).toString() == u"high");

    const auto usual = assistant.chatConnection({});
    REQUIRE(usual.has_value());
    CHECK(usual->model == u"gpt-5");
    CHECK(usual->effort == u"high");
    CHECK_FALSE(usual->think);

    const auto own = assistant.chatConnection(
        gs::app::AiChoice{.model = u"o3"_s, .effort = u"low"_s, .longContext = true, .service = u"openai"_s});
    REQUIRE(own.has_value());
    CHECK(own->model == u"o3");
    CHECK(own->effort == u"low");
    CHECK_FALSE(own->longContext);
    CHECK(assistant.chatConnection(gs::app::AiChoice{.model = u"o4-mini"_s})->model == u"o4-mini");

    const auto foreign = assistant.chatConnection(
        gs::app::AiChoice{.model = u"claude-sonnet-4-5"_s, .think = true, .service = u"anthropic"_s});
    REQUIRE(foreign.has_value());
    CHECK(foreign->model == u"gpt-5");
    CHECK(foreign->effort == u"high");

    assistant.makeDefault(QVariantMap{{u"model"_s, u"claude-opus-4-1"_s}, {u"service"_s, u"anthropic"_s}});
    CHECK(setup.settings.values().value(u"assistant.openai.model"_s).toString() == u"gpt-5");
    assistant.makeDefault(QVariantMap{{u"model"_s, u"o3"_s}, {u"effort"_s, u"medium"_s}, {u"think"_s, false}});
    CHECK(setup.settings.values().value(u"assistant.openai.model"_s).toString() == u"o3");
    CHECK(setup.settings.values().value(u"assistant.openai.effort"_s).toString() == u"medium");
    CHECK_FALSE(setup.settings.values().value(u"assistant.openai.thinking"_s).toBool());
    CHECK(assistant.defaults().model == u"o3");
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

TEST_CASE("ADR-0112: fetching the model list never follows a redirect, so the API key only goes to the address in the "
          "settings")
{
    Setup setup;
    const QByteArray listed = R"({"data": [{"id": "gpt-elsewhere"}]})";
    ModelList elsewhere("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                        QByteArray::number(listed.size()) + "\r\nConnection: close\r\n\r\n" + listed);
    REQUIRE(elsewhere.listening);
    ModelList configured("HTTP/1.1 302 Found\r\nLocation: " + elsewhere.address().toUtf8() +
                         "/models\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
    REQUIRE(configured.listening);
    REQUIRE(setup.settings.setValue(u"assistant.openai.address"_s, configured.address()));
    AiAssistant assistant(setup.settings, setup.prefix);
    REQUIRE(assistant.saveKey(u"sk-stays-here"_s));

    assistant.fetchModels();
    REQUIRE(gs::test::waitUntil([&assistant] { return assistant.modelsState() != u"running"; }));
    REQUIRE(configured.heads.size() == 1);
    CHECK(configured.heads.front().contains("Bearer sk-stays-here"));
    CHECK(elsewhere.heads.empty());
    CHECK(assistant.modelsState() == u"failed");
    CHECK(assistant.models().isEmpty());
}
