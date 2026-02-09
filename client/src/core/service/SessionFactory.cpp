#include "SessionFactory.h"
#include "../interfaces/IDecoder.h"
#include "../interfaces/IRenderer.h"
#include "../interfaces/IVideoChannel.h"
#include "../interfaces/IControlChannel.h"
#include "../interfaces/IInputProcessor.h"

namespace qsc {
namespace core {

std::unique_ptr<DeviceSession> SessionFactory::create(const SessionParams& params,
                                                       QObject* parent)
{
    return std::make_unique<DeviceSession>(params, parent);
}

std::unique_ptr<DeviceSession> SessionFactory::createWithDeps(
    const SessionParams& params,
    std::unique_ptr<IDecoder> decoder,
    std::unique_ptr<IRenderer> renderer,
    std::unique_ptr<IVideoChannel> videoChannel,
    std::unique_ptr<IControlChannel> controlChannel,
    std::unique_ptr<IInputProcessor> inputProcessor,
    QObject* parent)
{
    // 创建会话
    auto session = std::make_unique<DeviceSession>(params, parent);

    // TODO: 如果需要完整的依赖注入，需要修改 DeviceSession 接受这些组件
    // 目前的实现是：如果传入了自定义组件，会话会使用它们；否则使用默认实现
    // 这需要在 DeviceSession 中添加 setDecoder(), setRenderer() 等方法

    // 当前实现：忽略注入的依赖，返回默认会话
    // 注入的对象会在 unique_ptr 析构时自动释放
    Q_UNUSED(decoder);
    Q_UNUSED(renderer);
    Q_UNUSED(videoChannel);
    Q_UNUSED(controlChannel);
    Q_UNUSED(inputProcessor);

    return session;
}

} // namespace core
} // namespace qsc
