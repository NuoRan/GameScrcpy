#ifndef IMAGEMATCHER_H
#define IMAGEMATCHER_H

#include <QString>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <vector>
#include <cstdint>
#include <memory>

// 前向声明 opencv_matching 的类
namespace template_matching {
    class Matcher;
    struct MatcherParam;
    struct MatchResult;
}

// ---------------------------------------------------------
// 图像模板匹配结果 / Image Template Match Result
// ---------------------------------------------------------
struct ImageMatchResult
{
    bool found = false;       // 是否找到匹配 / Whether a match was found
    double x = 0.0;           // 匹配中心点 x (0.0~1.0) / Match center x (0.0~1.0)
    double y = 0.0;           // 匹配中心点 y (0.0~1.0) / Match center y (0.0~1.0)
    double confidence = 0.0;  // 匹配置信度 / Match confidence
    double angle = 0.0;       // 匹配角度 / Match angle

    // 像素坐标 (内部使用) / Pixel coordinates (internal use)
    int pixelX = 0;
    int pixelY = 0;
};

// ---------------------------------------------------------
// 图像模板匹配器 / Image Template Matcher
// 基于 opencv_matching 的快速金字塔模板匹配 / Fast pyramid template matching based on opencv_matching
// ---------------------------------------------------------
class ImageMatcher
{
public:
    ImageMatcher();
    ~ImageMatcher();

    // =========================================================
    // 主要接口
    // =========================================================

    /**
     * @brief 在图像中搜索模板
     * @param mainImage 主图像
     * @param templateImage 模板图像
     * @param threshold 相似度阈值 (0.0~1.0，默认 0.7)
     * @param searchRegion 搜索区域 (归一化坐标 x1,y1,x2,y2)，为空则全图搜索
     * @param maxAngle 最大旋转角度 (0 表示不旋转匹配)
     * @return 匹配结果
     */
    ImageMatchResult findTemplate(
        const QImage& mainImage,
        const QImage& templateImage,
        double threshold = 0.7,
        const QRectF& searchRegion = QRectF(),
        double maxAngle = 0.0
    );

    // =========================================================
    // 辅助接口
    // =========================================================

    /**
     * @brief 获取图片目录路径
     */
    static QString getImagesPath();

    /**
     * @brief 加载模板图片
     * @param imageName 图片名称 (不含路径，如 "button.png")
     * @return 加载的图像，失败返回空图像
     */
    static QImage loadTemplateImage(const QString& imageName);

    /**
     * @brief 保存模板图片
     * @param image 要保存的图像
     * @param imageName 图片名称
     * @return 是否保存成功
     */
    static bool saveTemplateImage(const QImage& image, const QString& imageName);

    /**
     * @brief 检查模板图片是否存在
     */
    static bool templateExists(const QString& imageName);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

#endif // IMAGEMATCHER_H
