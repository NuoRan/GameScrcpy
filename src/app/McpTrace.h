#pragma once

#include <gs/script/Replay.h>

#include <QImage>
#include <QJsonObject>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gs::app {

struct McpTraceFrame {
    double atMs = 0;
    QImage image;
};

class McpTrace
{
public:
    static constexpr std::size_t kMostSteps = 1500;
    static constexpr std::size_t kMostFrames = 12;
    static constexpr int kFrameSide = 480;
    static constexpr double kFrameGapMs = 250.0;

    void addHits(const std::string& json, std::int64_t epochMs);
    [[nodiscard]] QJsonObject steps(const std::vector<script::ReplayLine>& lines) const;
    [[nodiscard]] static std::vector<double> frameTimes(const std::vector<script::ReplayLine>& lines, double endedMs);

private:
    struct Hit {
        double atMs = 0;
        QString file;
        QString position;
        QJsonObject values;
    };

    std::vector<Hit> m_hits;
    std::size_t m_dropped = 0;
};

[[nodiscard]] std::vector<McpTraceFrame> mcpTraceFrames(const std::filesystem::path& video, std::int64_t startPtsUs,
                                                        const std::vector<double>& timesMs, int longSide);

}
