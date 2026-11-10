#pragma once

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QString>
#include <QTimer>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

namespace gs::test {

class McpApplication
{
public:
    McpApplication()
    {
        if (QCoreApplication::instance() == nullptr) {
            m_app = std::make_unique<QCoreApplication>(m_argc, m_argv.data());
        }
    }

private:
    int m_argc = 1;
    char m_name[5] = "test";
    std::array<char*, 2> m_argv{m_name, nullptr};
    std::unique_ptr<QCoreApplication> m_app;
};

class McpFolder
{
public:
    McpFolder()
        : m_path(std::filesystem::temp_directory_path() /
                 ("gs-mcp-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                  std::to_string(s_next++)))
    {
        std::filesystem::create_directories(m_path);
    }
    ~McpFolder()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_path, ignored);
    }
    McpFolder(const McpFolder&) = delete;
    McpFolder& operator=(const McpFolder&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return m_path; }

private:
    static inline std::atomic<int> s_next = 0;
    std::filesystem::path m_path;
};

template <typename Done>
bool waitUntil(Done done, std::chrono::milliseconds limit = std::chrono::seconds(20))
{
    QElapsedTimer timer;
    timer.start();
    while (!done()) {
        if (timer.elapsed() > limit.count()) {
            return false;
        }
        QEventLoop loop;
        QTimer::singleShot(5, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return true;
}

inline QString uniquePipe()
{
    static std::atomic<int> next = 0;
    return QStringLiteral("GameScrcpy-MCP-test-%1-%2").arg(QCoreApplication::applicationPid()).arg(next++);
}

inline QJsonObject rpc(int id, const QString& method, const QJsonObject& params = {})
{
    return {{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), id},
            {QStringLiteral("method"), method},
            {QStringLiteral("params"), params}};
}

inline QJsonObject hello(int id, const QString& client)
{
    return rpc(id, QStringLiteral("initialize"),
               {{QStringLiteral("protocolVersion"), QStringLiteral("2025-06-18")},
                {QStringLiteral("capabilities"), QJsonObject{}},
                {QStringLiteral("clientInfo"),
                 QJsonObject{{QStringLiteral("name"), client}, {QStringLiteral("version"), QStringLiteral("1.0")}}}});
}

class McpClient
{
public:
    explicit McpClient(const QString& pipe) { m_socket.connectToServer(pipe); }

    [[nodiscard]] bool connected()
    {
        static_cast<void>(waitUntil([this] { return m_socket.state() != QLocalSocket::ConnectingState; },
                                    std::chrono::seconds(5)));
        return m_socket.state() == QLocalSocket::ConnectedState;
    }

    [[nodiscard]] bool dropped()
    {
        return waitUntil([this] { return m_socket.state() == QLocalSocket::UnconnectedState; });
    }

    void send(const QJsonObject& message) { sendRaw(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n'); }

    void sendRaw(const QByteArray& bytes)
    {
        m_socket.write(bytes);
        m_socket.flush();
    }

    [[nodiscard]] QJsonObject receive()
    {
        const bool arrived = waitUntil([this] {
            m_buffer += m_socket.readAll();
            return m_buffer.contains('\n');
        });
        if (!arrived) {
            return {};
        }
        const qsizetype newline = m_buffer.indexOf('\n');
        const QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        return QJsonDocument::fromJson(line).object();
    }

    [[nodiscard]] QJsonObject ask(const QJsonObject& message)
    {
        send(message);
        return receive();
    }

private:
    QLocalSocket m_socket;
    QByteArray m_buffer;
};

}
