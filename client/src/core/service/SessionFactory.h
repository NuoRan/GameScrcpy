#ifndef CORE_SESSIONFACTORY_H
#define CORE_SESSIONFACTORY_H

#include <memory>
#include "DeviceSession.h"

namespace qsc {
namespace core {

// 前向声明
class IDecoder;
class IRenderer;
class IVideoChannel;
class IControlChannel;
class IInputProcessor;

/**
 * @brief 会话工厂类 / Session Factory
 *
 * 负责创建 DeviceSession 实例，支持依赖注入用于测试。
 * Creates DeviceSession instances with dependency injection for testing.
 *
 * 使用示例：
 * @code
 * // 生产环境
 * auto session = SessionFactory::create(params);
 *
 * // 测试环境 - 注入 Mock
 * auto session = SessionFactory::createWithDeps(params,
 *     std::make_unique<MockDecoder>(),
 *     std::make_unique<MockRenderer>());
 * @endcode
 */
class SessionFactory
{
public:
    /**
     * @brief 创建会话（生产环境）/ Create session (production)
     * @param params 会话参数 / Session parameters
     * @param parent 父对象 / Parent QObject
     * @return DeviceSession 实例 / DeviceSession instance
     */
    static std::unique_ptr<DeviceSession> create(const SessionParams& params,
                                                  QObject* parent = nullptr);

    /**
     * @brief 创建会话并注入依赖（测试环境）
     * @param params 会话参数
     * @param decoder 解码器实现（可选，传 nullptr 使用默认）
     * @param renderer 渲染器实现（可选，传 nullptr 使用默认）
     * @param videoChannel 视频通道（可选，传 nullptr 使用默认）
     * @param controlChannel 控制通道（可选，传 nullptr 使用默认）
     * @param inputProcessor 输入处理器（可选，传 nullptr 使用默认）
     * @param parent 父对象
     * @return DeviceSession 实例
     */
    static std::unique_ptr<DeviceSession> createWithDeps(
        const SessionParams& params,
        std::unique_ptr<IDecoder> decoder = nullptr,
        std::unique_ptr<IRenderer> renderer = nullptr,
        std::unique_ptr<IVideoChannel> videoChannel = nullptr,
        std::unique_ptr<IControlChannel> controlChannel = nullptr,
        std::unique_ptr<IInputProcessor> inputProcessor = nullptr,
        QObject* parent = nullptr);
};

} // namespace core
} // namespace qsc

#endif // CORE_SESSIONFACTORY_H
