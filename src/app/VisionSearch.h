#pragma once

#include <gs/media/FrameSnapshot.h>
#include <gs/script/Resources.h>
#include <gs/script/Services.h>
#include <gs/vision/Color.h>
#include <gs/vision/Matcher.h>

#include <QImage>
#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gs::app {

[[nodiscard]] QImage readPicture(const QString& path, const std::string& label, std::string& problem);
[[nodiscard]] QImage scaledPicture(const QImage& picture, script::PictureSize source, int width, int height);
[[nodiscard]] std::shared_ptr<const vision::Template> templateOf(const QImage& picture, bool color,
                                                                 const std::string& label, std::string& problem);
[[nodiscard]] vision::Rect pixelArea(const script::Region& region, int width, int height);
[[nodiscard]] std::vector<vision::Match> findTemplate(const media::LumaSnapshot& shot, const vision::Template& pattern,
                                                      const vision::Rect& area, double threshold, int most, bool all);
[[nodiscard]] std::optional<vision::ColorPatch> findColorIn(const media::LumaSnapshot& shot, vision::Rgb color,
                                                            double tolerance, const vision::Rect& area);
[[nodiscard]] std::optional<vision::Rgb> pixelAt(const media::LumaSnapshot& shot, script::Point at);
[[nodiscard]] std::string pixelJson(const media::LumaSnapshot& shot, const script::PixelQuery& query);
[[nodiscard]] std::string foundJson(const vision::Rect& rect, double score, int width, int height);
[[nodiscard]] QImage pictureOf(const media::LumaSnapshot& shot);
[[nodiscard]] std::string templateFileFor(const QString& name, const std::vector<script::TemplateResource>& templates,
                                          const std::string& keep,
                                          const std::function<bool(const std::string&)>& foreign);

}
