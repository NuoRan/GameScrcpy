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
#include <functional>
#include <memory>

/**
 * @brief 渲染统计信息 / Render Statistics
 */
struct RenderStatistics
{
    quint64 totalFrames = 0;        // 总帧数 / Total frames
    quint64 droppedFrames = 0;      // 丢帧数 / Dropped frames
    double avgUploadTimeMs = 0;     // 平均上传时间(毫秒) / Average upload time (ms)
    double avgRenderTimeMs = 0;     // 平均渲染时间(毫秒) / Average render time (ms)
    bool pboEnabled = false;        // PBO 是否启用 / Whether PBO is enabled
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
 * - 无锁帧提交: submitFrameDirect ↔ paintGL 使用原子指针交换
 */

/**
 * @brief 直接帧数据槽 (用于无锁帧传递)
 *
 * 将帧的所有元数据打包为单个结构体，
 * 通过 atomic<DirectFrameSlot*> 的 exchange 操作实现无锁传递，
 * 消除 submitFrameDirect ↔ paintGL 之间的 QMutex 竞争。
 */
struct DirectFrameSlot {
    uint8_t* dataY = nullptr;
    uint8_t* dataU = nullptr;
    uint8_t* dataV = nullptr;
    int width = 0;
    int height = 0;
    int linesizeY = 0;
    int linesizeU = 0;
    int linesizeV = 0;
    std::function<void()> releaseCallback;
};
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

    /**
     * @brief 零拷贝帧提交 (优化跨线程渲染)
     *
     * 接收包含 YUV420P 数据的 QByteArray，利用 Qt 隐式共享避免拷贝。
     * 数据布局: Y(ly*h) + U(lu*h/2) + V(lv*h/2)
     *
     * @param frameData 帧数据（移动语义）
     * @param width 帧宽度
     * @param height 帧高度
     * @param linesizeY Y 分量行字节数
     * @param linesizeU U 分量行字节数
     * @param linesizeV V 分量行字节数
     */
    void submitFrame(QByteArray frameData, int width, int height, int linesizeY, int linesizeU, int linesizeV);

    /**
     * @brief 零拷贝帧提交 - 直接指针版本
     *
     * 直接使用 YUV 数据指针渲染，无任何内存拷贝。
     * 渲染完成后自动调用 releaseCallback 释放帧资源。
     *
     * @param dataY Y 分量指针
     * @param dataU U 分量指针
     * @param dataV V 分量指针
     * @param width 帧宽度
     * @param height 帧高度
     * @param linesizeY Y 分量行字节数
     * @param linesizeU U 分量行字节数
     * @param linesizeV V 分量行字节数
     * @param releaseCallback 渲染完成后的释放回调
     */
    void submitFrameDirect(uint8_t* dataY, uint8_t* dataU, uint8_t* dataV,
                          int width, int height,
                          int linesizeY, int linesizeU, int linesizeV,
                          std::function<void()> releaseCallback);

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
     * @brief 释放当前持有的直接指针帧
     *
     * 在窗口关闭时调用，确保帧在 session 失效前被正确释放。
     */
    void discardPendingFrame();

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
    bool m_grabDataStale = false;                           // 截图缓存是否过期（懒拷贝标志）

    // === 零拷贝帧存储 ===
    QByteArray m_zeroCopyFrame;                             // 零拷贝帧数据（利用 Qt 隐式共享）
    int m_zcFrameWidth = 0;
    int m_zcFrameHeight = 0;
    int m_zcLinesizeY = 0;
    int m_zcLinesizeU = 0;
    int m_zcLinesizeV = 0;
    bool m_useZeroCopyFrame = false;                        // 是否使用零拷贝帧

    // === 直接指针帧存储（完全零拷贝 + 无锁）===
    // 使用原子指针交换代替 QMutex
    // 解码线程: exchange 写入 m_pendingDirectFrame
    // GUI线程: exchange 读取 m_pendingDirectFrame, 渲染后存入 m_renderedFrame
    std::atomic<DirectFrameSlot*> m_pendingDirectFrame{nullptr};  // 无锁帧邮箱
    DirectFrameSlot* m_renderedFrame = nullptr;                   // 仅 GUI 线程访问

    // 以下字段保留用于旧路径兼容（submitFrame/updateTextures）
    uint8_t* m_directDataY = nullptr;
    uint8_t* m_directDataU = nullptr;
    uint8_t* m_directDataV = nullptr;
    int m_directWidth = 0;
    int m_directHeight = 0;
    int m_directLinesizeY = 0;
    int m_directLinesizeU = 0;
    int m_directLinesizeV = 0;
    bool m_useDirectFrame = false;
    std::function<void()> m_directFrameReleaseCallback;     // 帧释放回调

    // === PBO 双缓冲 ===
    static constexpr int PBO_COUNT = 2;                     // 双缓冲
    std::array<GLuint, PBO_COUNT> m_pboY = {0, 0};          // Y 分量 PBO
    std::array<GLuint, PBO_COUNT> m_pboU = {0, 0};          // U 分量 PBO
    std::array<GLuint, PBO_COUNT> m_pboV = {0, 0};          // V 分量 PBO
    int m_pboIndex = 0;                                     // 当前写入的 PBO 索引
    bool m_pboEnabled = true;                               // PBO 是否启用
    bool m_pboSupported = false;                            // 硬件是否支持 PBO
    bool m_pboInited = false;                               // PBO 是否已初始化
    std::vector<uint8_t> m_pboTempBuffer;                   // PBO stride不匹配时的复用缓冲区

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
