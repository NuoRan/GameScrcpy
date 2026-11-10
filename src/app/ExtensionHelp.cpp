#include "ExtensionHelp.h"

#include "AppSettings.h"
#include "ExtensionHost.h"
#include "ExtensionIcons.h"
#include "QtConversions.h"

#include <gs/ext/Archive.h>

#include <QFile>
#include <QObject>
#include <QUrl>

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace gs::app {

namespace {

namespace fs = std::filesystem;

constexpr qint64 kMostHelpBytes = 1024 * 1024;
constexpr std::array<std::string_view, 2> kLanguages{"en", "zh_CN"};
constexpr std::string_view kSvg = "svg:";

bool inside(const fs::path& root, const fs::path& file)
{
    auto filePart = file.begin();
    for (auto rootPart = root.begin(); rootPart != root.end(); ++rootPart, ++filePart) {
        if (filePart == file.end() || *filePart != *rootPart) {
            return false;
        }
    }
    return filePart != file.end();
}

std::optional<QString> helpText(const fs::path& folder, const std::string& file)
{
    if (!ext::safeArchivePath(file)) {
        return std::nullopt;
    }
    std::error_code error;
    const fs::path root = fs::weakly_canonical(folder, error);
    if (error) {
        return std::nullopt;
    }
    const fs::path path =
        fs::weakly_canonical(folder / fs::path(std::u8string(reinterpret_cast<const char8_t*>(file.data()), file.size())),
                             error);
    if (error || !inside(root, path) || !fs::is_regular_file(path, error) || error) {
        return std::nullopt;
    }
    QFile handle(QString::fromStdWString(path.wstring()));
    if (!handle.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QByteArray bytes = handle.read(kMostHelpBytes + 1);
    if (bytes.size() > kMostHelpBytes) {
        qWarning("help: %s is larger than 1 MiB", qUtf8Printable(QString::fromStdWString(path.wstring())));
        return std::nullopt;
    }
    QString text = QString::fromUtf8(bytes);
    if (text.startsWith(QChar(0xFEFF))) {
        text.remove(0, 1);
    }
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    return text;
}

}

std::vector<ExtensionHelp> extensionHelp(const ExtensionHost& host, const QString& language)
{
    std::vector<ExtensionHelp> topics;
    for (const ExtensionHost::Extension& extension : host.extensions()) {
        if (!extension.active || !extension.manifest) {
            continue;
        }
        const ext::Manifest& manifest = *extension.manifest;
        for (const ext::HelpContribution& help : manifest.contributes.help) {
            ExtensionHelp topic{
                .extension = toQString(manifest.id),
                .extensionName = extensionText(manifest, "name", manifest.name, language),
                .id = toQString(help.id),
                .base = QUrl::fromLocalFile(QString::fromStdWString(extension.directory.wstring()) + u'/')};
            for (const std::string_view code : kLanguages) {
                topic.titles.emplace(toQString(code),
                                     extensionText(manifest, "help." + help.id + ".title", help.title, toQString(code)));
            }
            if (const std::optional<QString> text = helpText(extension.directory, help.file)) {
                topic.texts.emplace(toQString(kLanguages.front()), *text);
            }
            for (const auto& [code, file] : help.locales) {
                if (const std::optional<QString> text = helpText(extension.directory, file)) {
                    topic.texts.insert_or_assign(toQString(code), *text);
                }
            }
            if (topic.texts.empty()) {
                qWarning("help: %s has no readable text for %s", manifest.id.c_str(), help.id.c_str());
                continue;
            }
            if (help.icon.starts_with(kSvg)) {
                topic.iconPath = extensionIconPath(extension.directory, std::string_view(help.icon).substr(kSvg.size()));
            } else if (builtinIconName(help.icon)) {
                topic.icon = toQString(help.icon);
            }
            topics.push_back(std::move(topic));
        }
    }
    return topics;
}

void watchExtensionHelp(HelpLibrary& help, ExtensionHost& host)
{
    const auto refresh = [&help, &host] {
        const AppSettings* settings = AppSettings::instance();
        help.setExtensionTopics(extensionHelp(host, settings != nullptr ? settings->interfaceLanguage() : QString()));
    };
    QObject::connect(&host, &ExtensionHost::extensionsChanged, &help, refresh);
    if (const AppSettings* settings = AppSettings::instance()) {
        QObject::connect(settings, &AppSettings::languageChanged, &help, refresh);
    }
    refresh();
}

}
