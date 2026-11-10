#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
class QQuickWindow;

namespace gs::render {

class GraphicsDevice
{
public:
    [[nodiscard]] static std::unique_ptr<GraphicsDevice> create(bool debugLayer = false, bool forceWarp = false);
    ~GraphicsDevice();
    GraphicsDevice(const GraphicsDevice&) = delete;
    GraphicsDevice& operator=(const GraphicsDevice&) = delete;

    [[nodiscard]] ID3D11Device* device() const noexcept { return m_device; }
    [[nodiscard]] ID3D11DeviceContext* context() const noexcept { return m_context; }
    [[nodiscard]] bool warp() const noexcept { return m_warp; }
    [[nodiscard]] bool debugLayer() const noexcept { return m_debugLayer; }
    [[nodiscard]] const std::string& description() const noexcept { return m_description; }
    [[nodiscard]] std::vector<std::string> debugErrors(std::size_t limit = 20) const;

    void share(QQuickWindow& window) const;

private:
    GraphicsDevice() = default;

    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    bool m_warp = false;
    bool m_debugLayer = false;
    std::string m_description;
    mutable std::mutex m_rendering;
};

}
