#include "imagematcher.h"

#include <QCoreApplication>
#include <QDir>
#include <QDebug>

#ifdef ENABLE_IMAGE_MATCHING

// OpenCV headers
#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc.hpp>

// opencv_matching headers
#include "matcher.h"

// ---------------------------------------------------------
// ImageMatcher::Impl - 内部实现类
// ---------------------------------------------------------
class ImageMatcher::Impl
{
public:
    Impl() = default;
    ~Impl() = default;

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
    // 这种方式能增强对比度，对半透明UI元素更友好
    static cv::Mat qImageToEnhancedGrayMat(const QImage& image) {
        cv::Mat gray = qImageToGrayMat(image);

        // 使用 CLAHE (Contrast Limited Adaptive Histogram Equalization)
        // 自适应直方图均衡能增强局部对比度，对半透明元素更有效
        cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE();
        clahe->setClipLimit(2.0);  // 对比度限制
        clahe->setTilesGridSize(cv::Size(8, 8));  // 分块大小

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

        // 配置匹配参数
        template_matching::MatcherParam param;
        param.matcherType = template_matching::MatcherType::PATTERN;
        param.maxCount = 1;  // 只找最佳匹配
        param.scoreThreshold = threshold;
        param.iouThreshold = 0.0;
        param.angle = maxAngle;
        param.minArea = 256;

        // 创建匹配器
        template_matching::Matcher* matcher = template_matching::GetMatcher(param);
        if (!matcher) {
            qWarning() << "ImageMatcher: Failed to create matcher";
            return result;
        }

        // 设置模板
        matcher->setTemplate(tplMat);

        // 执行匹配
        std::vector<template_matching::MatchResult> matchResults;
        matcher->match(searchMat, matchResults);

        // 清理匹配器
        delete matcher;

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
    QString fullPath = getImagesPath() + "/" + imageName;
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
