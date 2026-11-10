#pragma once

#include <QImage>

#include <map>
#include <string>
#include <vector>

namespace gs::render {

struct VideoFilter {
    std::string id;
    std::string fileName;
    std::string source;
    std::map<std::string, double, std::less<>> values;
    std::map<std::string, QImage, std::less<>> textures;

    friend bool operator==(const VideoFilter&, const VideoFilter&) = default;
};

struct FilterStatus {
    std::string id;
    bool ready = false;
    std::string error;
    std::vector<std::string> passes;
    std::vector<double> passMs;

    friend bool operator==(const FilterStatus&, const FilterStatus&) = default;
};

}
