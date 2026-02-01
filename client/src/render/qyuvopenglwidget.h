// Header definition
#ifndef QYUVOPENGLWIDGET_H
#define QYUVOPENGLWIDGET_H
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLWidget>
#include <QImage>
#include <QMutex>
#include <QElapsedTimer>
#include <QTimer>
#include <QShowEvent>
#include <QHideEvent>
#include <QCloseEvent>
#include <vector>
#include <array>
#include <atomic>

/**
 * @brief 渲染统计信息
 */
struct RenderStatistics
{
    quint64 totalFrames = 0;        // 总帧数
    quint64 droppedFrames = 0;      // 丢帧数
    double avgUploadTimeMs = 0;     // 平均上传时间(毫秒)
    double avgRenderTimeMs = 0;     // 平均渲染时间(毫秒)
    bool pboEnabled = false;        // PBO 是否启用
};

/**
 * YUV 格式类型
 * NV12: 用于直接渲染避免格式转换
 */
enum class YUVFormat {
    YUV420P,    // 平面格式: Y + U + V (默认)
    NV12        // 半平面格式: Y + UV (硬件解码常用)
};

/**
 * @brief YUV OpenGL 渲染控件
 *
 * 功能特性：
 * - 支持 PBO (Pixel Buffer Object) 双缓冲异步纹理上传
 * - 支持脏区域检测，减少不必要的数据传输
 * - YUV420P 到 RGB 的 GPU 加速转换 (BT.709)
 * - NV12: 支持直接 NV12 渲染避免格式转换
 * - 线程安全的帧获取接口
 */
class QYUVOpenGLWidget
    : public QOpenGLWidget
    , protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit QYUVOpenGLWidget(QWidget *parent = nullptr);
    virtual ~QYUVOpenGLWidget() override;

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

    void setFrameSize(const QSize &frameSize);
    const QSize &frameSize();

    // YUV420P 格式更新 (默认)
    void updateTextures(quint8 *dataY, quint8 *dataU, quint8 *dataV, quint32 linesizeY, quint32 linesizeU, quint32 linesizeV);

    // NV12: 直接 NV12 格式更新 (避免格式转换)
    void updateTexturesNV12(quint8 *dataY, quint8 *dataUV, quint32 linesizeY, quint32 linesizeUV);

    // NV12: 设置 YUV 格式
    void setYUVFormat(YUVFormat format);
    YUVFormat yuvFormat() const { return m_yuvFormat; }

    // 获取当前帧的 RGB 图像 (线程安全)
    QImage grabCurrentFrame();

    // 获取当前帧的灰度数据 (直接使用 Y 分量，更高效)
    std::vector<uint8_t> grabCurrentFrameGrayscale();

    // === PBO 优化相关 ===

    /**
     * @brief 启用/禁用 PBO 异步上传
     * @param enable 是否启用
     */
    void setPBOEnabled(bool enable);

    /**
     * @brief 检查 PBO 是否启用
     */
    bool isPBOEnabled() const { return m_pboEnabled && m_pboSupported; }

    /**
     * @brief 检查硬件是否支持 PBO
     */
    bool isPBOSupported() const { return m_pboSupported; }

    /**
     * @brief 获取渲染统计信息
     */
    RenderStatistics statistics() const;

    /**
     * @brief 重置统计信息
     */
    void resetStatistics();

signals:
    /**
     * @brief 渲染统计更新信号 (每秒发送一次)
     */
    void statisticsUpdated(const RenderStatistics& stats);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int width, int height) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void initShader();
    void initShaderNV12();                                  // NV12: NV12 专用着色器
    void initTextures();
    void initTexturesNV12();                                // NV12: NV12 纹理初始化
    void deInitTextures();
    void updateTexture(GLuint texture, quint32 textureType, quint8 *pixels, quint32 stride);
    void updateTextureNoContext(GLuint texture, quint32 textureType, quint8 *pixels, quint32 stride);

    // === PBO 相关方法 ===
    void initPBO();
    void deInitPBO();
    void updateTextureWithPBO(GLuint texture, quint32 textureType, quint8 *pixels, quint32 stride);
    void updateTextureWithPBONoContext(GLuint texture, quint32 textureType, quint8 *pixels, quint32 stride);
    bool checkPBOSupport();

    // === 脏区域检测 ===
    bool isRegionDirty(const quint8* newData, const quint8* oldData, size_t size, int sampleStep = 64);

private:
    QSize m_frameSize = { -1, -1 };
    bool m_needUpdate = false;
    bool m_textureInited = false;
    QOpenGLBuffer m_vbo;
    QOpenGLShaderProgram m_shaderProgram;
    QOpenGLShaderProgram m_shaderProgramNV12;               // NV12: NV12 专用着色器程序
    GLuint m_texture[3] = { 0 };
    GLuint m_textureNV12[2] = { 0 };                        // NV12: Y + UV 纹理
    YUVFormat m_yuvFormat = YUVFormat::YUV420P;             // NV12: 当前格式

    // YUV 数据缓存 (用于帧获取)
    QMutex m_yuvMutex;
    std::vector<uint8_t> m_yuvDataY;
    std::vector<uint8_t> m_yuvDataU;
    std::vector<uint8_t> m_yuvDataV;
    std::vector<uint8_t> m_yuvDataUV;                       // NV12: UV 交织数据
    quint32 m_linesizeY = 0;
    quint32 m_linesizeU = 0;
    quint32 m_linesizeV = 0;
    quint32 m_linesizeUV = 0;                               // NV12: UV stride

    // === PBO 双缓冲 ===
    static constexpr int PBO_COUNT = 2;                     // 双缓冲
    std::array<GLuint, PBO_COUNT> m_pboY = {0, 0};          // Y 分量 PBO
    std::array<GLuint, PBO_COUNT> m_pboU = {0, 0};          // U 分量 PBO
    std::array<GLuint, PBO_COUNT> m_pboV = {0, 0};          // V 分量 PBO
    int m_pboIndex = 0;                                     // 当前写入的 PBO 索引
    bool m_pboEnabled = true;                               // PBO 是否启用
    bool m_pboSupported = false;                            // 硬件是否支持 PBO
    bool m_pboInited = false;                               // PBO 是否已初始化

    // === 脏区域检测缓存 ===
    std::vector<uint8_t> m_prevFrameY;                      // 上一帧 Y 数据 (用于比较)
    bool m_dirtyCheckEnabled = false;                       // 脏区域检测开关

    // === 统计信息 ===
    std::atomic<quint64> m_totalFrames{0};
    std::atomic<quint64> m_droppedFrames{0};
    QElapsedTimer m_uploadTimer;
    QElapsedTimer m_renderTimer;
    double m_totalUploadTime = 0;
    double m_totalRenderTime = 0;
    QElapsedTimer m_statsTimer;

    // === 帧上传节流 ===
    std::atomic<bool> m_hasPendingFrame{false};

    // === 后台刷新定时器 ===
    // 当窗口最小化/不可见时，确保帧仍然被处理，避免恢复时画面卡顿
    QTimer *m_backgroundRefreshTimer = nullptr;

    // === 销毁标志 ===
    // 标记控件是否正在销毁，防止在销毁过程中继续渲染
    std::atomic<bool> m_isDestroying{false};
};

#endif // QYUVOPENGLWIDGET_H
