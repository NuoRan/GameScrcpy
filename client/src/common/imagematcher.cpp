#include "imagematcher.h"

#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QMutex>

#ifdef ENABLE_IMAGE_MATCHING

// OpenCV headers
#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc.hpp>

// opencv_matching headers
#include "matcher.h"

// 【修复】全局互斥锁保护 OpenCV 操作，防止多线程内存问题
static QMutex s_opencvMutex;

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

    // QImage 转 cv::Mat
    static cv::Mat qImageToMat(const QImage& image) {
        QImage converted = image.convertToFormat(QImage::Format_RGB888);
        cv::Mat mat(converted.height(), converted.width(), CV_8UC3,
                    const_cast<uchar*>(converted.bits()), converted.bytesPerLine());
        cv::Mat result;
        cv::cvtColor(mat, result, cv::COLOR_RGB2BGR);
        return result.clone();  // 深拷贝，避免 QImage 释放后数据失效
    }

    // QImage 转灰度 cv::Mat
    static cv::Mat qImageToGrayMat(const QImage& image) {
        cv::Mat bgr = qImageToMat(image);
        cv::Mat gray;
        cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
        return gray;
    }

    // QImage 转增强灰度图 (使用 CLAHE 自适应直方图均衡)
    static cv::Mat qImageToEnhancedGrayMat(const QImage& image) {
        cv::Mat gray = qImageToGrayMat(image);

        cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE();
        clahe->setClipLimit(2.0);
        clahe->setTilesGridSize(cv::Size(8, 8));

        cv::Mat enhanced;
        clahe->apply(gray, enhanced);

        return enhanced;
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
    const QImage& mainImage,
    const QImage& templateImage,
    double threshold,
    const QRectF& searchRegion,
    double maxAngle)
{
    // 【修复】使用全局互斥锁保护 OpenCV 操作
    // OpenCV 在多线程环境下可能存在内存管理问题
    QMutexLocker locker(&s_opencvMutex);

    ImageMatchResult result;
    result.found = false;

    if (mainImage.isNull() || templateImage.isNull()) {
        qWarning() << "ImageMatcher: Invalid input images";
        return result;
    }

    try {
        // 转换为灰度图 (和 demo.cpp 一致的处理方式)
        cv::Mat mainMat = Impl::qImageToGrayMat(mainImage);
        cv::Mat tplMat = Impl::qImageToGrayMat(templateImage);

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
            qWarning() << "ImageMatcher: Template larger than search area";
            return result;
        }

        // 获取或复用匹配器（单例）
        template_matching::Matcher* matcher = Impl::getMatcher(threshold, maxAngle);
        if (!matcher) {
            qWarning() << "ImageMatcher: Failed to create matcher";
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
        qWarning() << "ImageMatcher: Exception:" << e.what();
    } catch (...) {
        qWarning() << "ImageMatcher: Unknown exception";
    }

    return result;
}

#else // !ENABLE_IMAGE_MATCHING

ImageMatchResult ImageMatcher::findTemplate(
    const QImage& mainImage,
    const QImage& templateImage,
    double threshold,
    const QRectF& searchRegion,
    double maxAngle)
{
    Q_UNUSED(mainImage);
    Q_UNUSED(templateImage);
    Q_UNUSED(threshold);
    Q_UNUSED(searchRegion);
    Q_UNUSED(maxAngle);

    qWarning() << "ImageMatcher: Image matching is disabled (OpenCV not available)";

    ImageMatchResult result;
    result.found = false;
    return result;
}

#endif // ENABLE_IMAGE_MATCHING

QString ImageMatcher::getImagesPath()
{
    QString path = QCoreApplication::applicationDirPath() + "/keymap/images";
    QDir dir(path);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    return path;
}

QImage ImageMatcher::loadTemplateImage(const QString& imageName)
{
    QString fullPath = getImagesPath() + "/" + imageName + ".png";
    QImage image(fullPath);

    if (image.isNull()) {
        qWarning() << "ImageMatcher: Failed to load template:" << fullPath;
    }

    return image;
}

bool ImageMatcher::saveTemplateImage(const QImage& image, const QString& imageName)
{
    if (image.isNull()) {
        return false;
    }

    QString fullPath = getImagesPath() + "/" + imageName;
    bool success = image.save(fullPath, "PNG");

    if (!success) {
        qWarning() << "ImageMatcher: Failed to save template:" << fullPath;
    }

    return success;
}

bool ImageMatcher::templateExists(const QString& imageName)
{
    QString fullPath = getImagesPath() + "/" + imageName;
    return QFile::exists(fullPath);
}
