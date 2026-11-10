#pragma once

#include <gs/adb/AdbClient.h>

#include <QImage>
#include <QObject>
#include <QString>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace gs::app {

[[nodiscard]] double pictureSimilarity(const QImage& capture, const QImage& phone);

struct PhoneCandidate {
    QString deviceId;
    QImage picture;
};

struct PhoneMatch {
    QString deviceId;
    double score = 0;
};

[[nodiscard]] std::optional<PhoneMatch> bestPhoneMatch(const QImage& capture, const std::vector<PhoneCandidate>& phones,
                                                       double threshold = 0.8, double margin = 0.1);

[[nodiscard]] QImage decodeScreencap(std::string_view png);

struct PhoneSource {
    QString deviceId;
    QString serial;
    QImage current;
};

class CaptureMatcher final : public QObject
{
    Q_OBJECT

public:
    explicit CaptureMatcher(std::uint16_t adbPort = adb::AdbClient::kDefaultPort, QObject* parent = nullptr);

    int suggest(const QString& captureSerial, const QImage& capture, std::vector<PhoneSource> phones);

signals:
    void suggested(int ticket, const QString& captureSerial, const QString& deviceId, double score);

private:
    std::uint16_t m_port;
    int m_tickets = 0;
};

}
