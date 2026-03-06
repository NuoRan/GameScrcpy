#include "imagematcher.h"

#include "StringUtils.h"
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#endif

#define LOG_TAG "ImageMatcher"
#include "Logger.h"

#ifdef ENABLE_IMAGE_MATCHING

// OpenCV headers
#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc.hpp>

// opencv_matching headers
#include "matcher.h"

// 注意: 已移除全局 s_opencvMutex
// 原因: thread_local 匹配器已保证线程隔离，
// OpenCV 的 cvtColor/matchTemplate 等操作在不同数据上是线程安全的。
// 全局锁会导致多个沙箱的找图完全串行，严重影响性能。

// ---------------------------------------------------------
// 线程局部存储的匹配器管理
// 每个线程拥有自己的 matcher，避免跨线程同步问题
// ---------------------------------------------------------
struct ThreadLocalMatcher {
    template_matching::Matcher* matcher = nullptr;
    double threshold = 0.0;
    double maxAngle = 0.0;

    ~ThreadLocalMatcher() {
        if (matcher) {
            delete matcher;
            matcher = nullptr;
        }
    }

    template_matching::Matcher* get(double newThreshold, double newMaxAngle) {
        // 参数变化时重建
        if (matcher && (threshold != newThreshold || maxAngle != newMaxAngle)) {
            delete matcher;
            matcher = nullptr;
        }

        if (!matcher) {
            template_matching::MatcherParam param;
            param.matcherType = template_matching::MatcherType::PATTERN;
            param.maxCount = 1;
            param.scoreThreshold = newThreshold;
            param.iouThreshold = 0.0;
            param.angle = newMaxAngle;
            param.minArea = 256;

            matcher = template_matching::GetMatcher(param);
            threshold = newThreshold;
            maxAngle = newMaxAngle;
        }
        return matcher;
    }
};

// 线程局部存储
static thread_local ThreadLocalMatcher t_matcher;

// ---------------------------------------------------------
// ImageMatcher::Impl - 内部实现类
// ---------------------------------------------------------
class ImageMatcher::Impl
{
public:
    Impl() = default;
    ~Impl() = default;

    // 获取当前线程的匹配器（线程局部，无需加锁）
    static template_matching::Matcher* getMatcher(double threshold, double maxAngle) {
        return t_matcher.get(threshold, maxAngle);
    }

    // cv::Mat 转灰度 cv::Mat（如果已经是灰度则直接返回）
    static cv::Mat toGrayMat(const cv::Mat& image) {
        if (image.empty()) return cv::Mat();
        if (image.channels() == 1) return image;
        cv::Mat gray;
        if (image.channels() == 3) {
            cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
        } else if (image.channels() == 4) {
            cv::cvtColor(image, gray, cv::COLOR_BGRA2GRAY);
        } else {
            return image;
        }
        return gray;
    }
};

#else // !ENABLE_IMAGE_MATCHING

// 无 OpenCV 时的空实现
class ImageMatcher::Impl
{
public:
    Impl() = default;
    ~Impl() = default;
};

#endif // ENABLE_IMAGE_MATCHING

// ---------------------------------------------------------
// ImageMatcher 实现
// ---------------------------------------------------------

ImageMatcher::ImageMatcher() : m_impl(std::make_unique<Impl>())
{
}

ImageMatcher::~ImageMatcher() = default;

#ifdef ENABLE_IMAGE_MATCHING

ImageMatchResult ImageMatcher::findTemplate(
    const cv::Mat& mainImage,
    const cv::Mat& templateImage,
    double threshold,
    const RectF& searchRegion,
    double maxAngle)
{
    // 不再使用全局互斥锁 - thread_local 匹配器已保证线程隔离

    ImageMatchResult result;
    result.found = false;

    if (mainImage.empty() || templateImage.empty()) {
        LOGW() << "ImageMatcher: Invalid input images (main=" << mainImage.empty() << " tpl=" << templateImage.empty() << ")";
        return result;
    }

    try {
        // 转换为灰度图
        cv::Mat mainMat = Impl::toGrayMat(mainImage);
        cv::Mat tplMat = Impl::toGrayMat(templateImage);

        LOG_I_ONCE("ImageMatcher::findTemplate: main=%dx%d, tpl=%dx%d, threshold=%.2f",
                   mainMat.cols, mainMat.rows, tplMat.cols, tplMat.rows, threshold);

        // 处理搜索区域
        cv::Mat searchMat;
        int offsetX = 0, offsetY = 0;

        if (searchRegion.isValid() && !searchRegion.isNull()) {
            int x1 = static_cast<int>(searchRegion.left() * mainMat.cols);
            int y1 = static_cast<int>(searchRegion.top() * mainMat.rows);
            int x2 = static_cast<int>(searchRegion.right() * mainMat.cols);
            int y2 = static_cast<int>(searchRegion.bottom() * mainMat.rows);

            // 边界检查
            x1 = std::max(0, std::min(x1, mainMat.cols - 1));
            y1 = std::max(0, std::min(y1, mainMat.rows - 1));
            x2 = std::max(x1 + 1, std::min(x2, mainMat.cols));
            y2 = std::max(y1 + 1, std::min(y2, mainMat.rows));

            // 确保搜索区域大于模板
            if ((x2 - x1) >= tplMat.cols && (y2 - y1) >= tplMat.rows) {
                searchMat = mainMat(cv::Rect(x1, y1, x2 - x1, y2 - y1)).clone();
                offsetX = x1;
                offsetY = y1;
            } else {
                searchMat = mainMat;
            }
        } else {
            searchMat = mainMat;
        }

        // 检查模板是否小于搜索图像
        if (tplMat.cols > searchMat.cols || tplMat.rows > searchMat.rows) {
            LOGW() << "ImageMatcher: Template larger than search area";
            return result;
        }

        // 获取或复用匹配器（单例）
        template_matching::Matcher* matcher = Impl::getMatcher(threshold, maxAngle);
        if (!matcher) {
            LOGW() << "ImageMatcher: Failed to create matcher";
            return result;
        }

        // 设置模板
        matcher->setTemplate(tplMat);

        // 执行匹配
        std::vector<template_matching::MatchResult> matchResults;
        matcher->match(searchMat, matchResults);

        // 不再 delete matcher，由 Impl 管理生命周期

        // 处理结果
        if (!matchResults.empty()) {
            const auto& best = matchResults[0];

            // 计算全局像素坐标 (中心点)
            int globalPixelX = static_cast<int>(best.Center.x) + offsetX;
            int globalPixelY = static_cast<int>(best.Center.y) + offsetY;

            // 转换为归一化坐标
            result.found = true;
            result.x = static_cast<double>(globalPixelX) / mainMat.cols;
            result.y = static_cast<double>(globalPixelY) / mainMat.rows;
            result.confidence = best.Score;
            result.angle = best.Angle;
            result.pixelX = globalPixelX;
            result.pixelY = globalPixelY;
        }

    } catch (const std::exception& e) {
        LOGW() << "ImageMatcher: Exception:" << e.what();
    } catch (...) {
        LOGW() << "ImageMatcher: Unknown exception";
    }

    return result;
}

ImageMatchResult ImageMatcher::findTemplateFromGray(
    const GrayFrame& grayFrame,
    const cv::Mat& templateMat,
    double threshold,
    const RectF& searchRegion,
    double maxAngle)
{
    ImageMatchResult result;
    result.found = false;

    if (!grayFrame.isValid() || templateMat.empty()) {
        LOGW() << "ImageMatcher: Invalid input (grayFrame valid=" << grayFrame.isValid() << " tpl empty=" << templateMat.empty() << ")";
        return result;
    }

    try {
        // 零拷贝：直接用 GrayFrame 数据构建 cv::Mat（不拥有数据，不深拷贝）
        cv::Mat mainMat(grayFrame.height, grayFrame.width, CV_8UC1,
                        const_cast<uint8_t*>(grayFrame.data.data()));

        // 模板转灰度（如果尚未是灰度）
        cv::Mat tplMat = Impl::toGrayMat(templateMat);

        LOG_I_ONCE("ImageMatcher::findTemplateFromGray: main=%dx%d, tpl=%dx%d, threshold=%.2f",
                   mainMat.cols, mainMat.rows, tplMat.cols, tplMat.rows, threshold);

        // 处理搜索区域
        cv::Mat searchMat;
        int offsetX = 0, offsetY = 0;

        if (searchRegion.isValid() && !searchRegion.isNull()) {
            int x1 = static_cast<int>(searchRegion.left() * mainMat.cols);
            int y1 = static_cast<int>(searchRegion.top() * mainMat.rows);
            int x2 = static_cast<int>(searchRegion.right() * mainMat.cols);
            int y2 = static_cast<int>(searchRegion.bottom() * mainMat.rows);

            x1 = std::max(0, std::min(x1, mainMat.cols - 1));
            y1 = std::max(0, std::min(y1, mainMat.rows - 1));
            x2 = std::max(x1 + 1, std::min(x2, mainMat.cols));
            y2 = std::max(y1 + 1, std::min(y2, mainMat.rows));

            if ((x2 - x1) >= tplMat.cols && (y2 - y1) >= tplMat.rows) {
                searchMat = mainMat(cv::Rect(x1, y1, x2 - x1, y2 - y1)).clone();
                offsetX = x1;
                offsetY = y1;
            } else {
                searchMat = mainMat;
            }
        } else {
            searchMat = mainMat;
        }

        if (tplMat.cols > searchMat.cols || tplMat.rows > searchMat.rows) {
            LOGW() << "ImageMatcher: Template larger than search area";
            return result;
        }

        template_matching::Matcher* matcher = Impl::getMatcher(threshold, maxAngle);
        if (!matcher) {
            LOGW() << "ImageMatcher: Failed to create matcher";
            return result;
        }

        matcher->setTemplate(tplMat);

        std::vector<template_matching::MatchResult> matchResults;
        matcher->match(searchMat, matchResults);

        if (!matchResults.empty()) {
            const auto& best = matchResults[0];

            int globalPixelX = static_cast<int>(best.Center.x) + offsetX;
            int globalPixelY = static_cast<int>(best.Center.y) + offsetY;

            result.found = true;
            result.x = static_cast<double>(globalPixelX) / mainMat.cols;
            result.y = static_cast<double>(globalPixelY) / mainMat.rows;
            result.confidence = best.Score;
            result.angle = best.Angle;
            result.pixelX = globalPixelX;
            result.pixelY = globalPixelY;
        }

    } catch (const std::exception& e) {
        LOGW() << "ImageMatcher: Exception:" << e.what();
    } catch (...) {
        LOGW() << "ImageMatcher: Unknown exception";
    }

    return result;
}

#else // !ENABLE_IMAGE_MATCHING

ImageMatchResult ImageMatcher::findTemplate(
    const cv::Mat& mainImage,
    const cv::Mat& templateImage,
    double threshold,
    const RectF& searchRegion,
    double maxAngle)
{
    (void)mainImage;
    (void)templateImage;
    (void)threshold;
    (void)searchRegion;
    (void)maxAngle;

    LOGW() << "ImageMatcher: Image matching is disabled (OpenCV not available)";

    ImageMatchResult result;
    result.found = false;
    return result;
}

ImageMatchResult ImageMatcher::findTemplateFromGray(
    const GrayFrame& grayFrame,
    const cv::Mat& templateMat,
    double threshold,
    const RectF& searchRegion,
    double maxAngle)
{
    (void)grayFrame;
    (void)templateMat;
    (void)threshold;
    (void)searchRegion;
    (void)maxAngle;

    LOGW() << "ImageMatcher: Image matching is disabled (OpenCV not available)";

    ImageMatchResult result;
    result.found = false;
    return result;
}

#endif // ENABLE_IMAGE_MATCHING

std::string ImageMatcher::getImagesPath()
{
    std::string path = strutil::appDirPath() + "/keymap/images";
    namespace fs = std::filesystem;
    fs::path dirPath(strutil::toWide(path));
    if (!fs::exists(dirPath)) {
        fs::create_directories(dirPath);
    }
    return path;
}

cv::Mat ImageMatcher::loadTemplateImage(const std::string& imageName)
{
    std::string fullPath = getImagesPath() + "/" + imageName + ".png";

#ifdef ENABLE_IMAGE_MATCHING
    // Windows: cv::imread 不支持 UTF-8 路径，用二进制读取 + cv::imdecode 绕过
#ifdef _WIN32
    std::wstring widePath = strutil::toWide(fullPath);
    FILE* f = nullptr;
    _wfopen_s(&f, widePath.c_str(), L"rb");
    if (!f) {
        LOGW() << "ImageMatcher: Failed to load template:" << fullPath.c_str();
        return cv::Mat();
    }
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uchar> buf(fileSize);
    fread(buf.data(), 1, fileSize, f);
    fclose(f);
    cv::Mat image = cv::imdecode(buf, cv::IMREAD_GRAYSCALE);
#else
    cv::Mat image = cv::imread(fullPath, cv::IMREAD_GRAYSCALE);
#endif
    if (image.empty()) {
        LOGW() << "ImageMatcher: Failed to load template:" << fullPath.c_str();
    }
    return image;
#else
    (void)fullPath;
    LOGW() << "ImageMatcher: Image matching is disabled (OpenCV not available)";
    return cv::Mat();
#endif
}

bool ImageMatcher::saveTemplateImage(const cv::Mat& image, const std::string& imageName)
{
    if (image.empty()) {
        return false;
    }

    std::string fullPath = getImagesPath() + "/" + imageName;

#ifdef ENABLE_IMAGE_MATCHING
    // Windows: cv::imwrite 不支持 UTF-8 路径，用 cv::imencode + 二进制写入绕过
#ifdef _WIN32
    std::vector<uchar> buf;
    // 根据扩展名确定编码格式
    std::string ext = ".png";
    size_t dotPos = fullPath.find_last_of('.');
    if (dotPos != std::string::npos) {
        ext = fullPath.substr(dotPos);
    }
    bool encodeOk = cv::imencode(ext, image, buf);
    if (!encodeOk || buf.empty()) {
        LOGW() << "ImageMatcher: Failed to encode template:" << fullPath.c_str();
        return false;
    }
    std::wstring widePath = strutil::toWide(fullPath);
    FILE* f = nullptr;
    _wfopen_s(&f, widePath.c_str(), L"wb");
    if (!f) {
        LOGW() << "ImageMatcher: Failed to save template:" << fullPath.c_str();
        return false;
    }
    size_t written = fwrite(buf.data(), 1, buf.size(), f);
    fclose(f);
    bool success = (written == buf.size());
#else
    bool success = cv::imwrite(fullPath, image);
#endif
    if (!success) {
        LOGW() << "ImageMatcher: Failed to save template:" << fullPath.c_str();
    }
    return success;
#else
    (void)fullPath;
    LOGW() << "ImageMatcher: Image matching is disabled (OpenCV not available)";
    return false;
#endif
}

bool ImageMatcher::templateExists(const std::string& imageName)
{
    std::string fullPath = getImagesPath() + "/" + imageName;
    return std::filesystem::exists(strutil::toWide(fullPath));
}
