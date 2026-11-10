#pragma once

#include <gs/ext/Manifest.h>
#include <gs/exthost/Module.h>
#include <gs/input/Geometry.h>
#include <gs/input/Modules.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gs::app {

struct ModuleEntry {
    std::string module;
    std::string extension;
    exthost::InstanceKind kind = exthost::InstanceKind::Attachment;
    std::shared_ptr<exthost::Module> host;
    std::size_t index = 0;
    std::vector<ext::DeclaredParam> params;
    std::vector<std::string> appliesTo;
    input::BehaviorShape shape;
};

struct ModuleNotice {
    input::ModuleTroubleKind kind = input::ModuleTroubleKind::Missing;
    std::string extension;
    std::string module;
    std::string control;
};

[[nodiscard]] std::string moduleParams(const std::vector<ext::DeclaredParam>& declared, std::string_view chosen,
                                       input::Vec2 position, input::Vec2 unit);
[[nodiscard]] bool moduleFits(const std::vector<std::string>& appliesTo, std::string_view type) noexcept;

class ExtensionModules final : public input::ModuleFactory
{
public:
    using Notify = std::function<void(const ModuleNotice& notice)>;

    ExtensionModules(std::vector<ModuleEntry> entries, Notify notify, std::vector<input::GlobalAttachment> globals = {});

    [[nodiscard]] std::unique_ptr<input::AttachmentInstance> attachment(const input::ModuleRequest& request) override;
    [[nodiscard]] std::unique_ptr<input::BehaviorInstance> behavior(const input::ModuleRequest& request,
                                                                    input::BehaviorShape& shape) override;
    void trouble(const input::ModuleTrouble& trouble) override;
    [[nodiscard]] const std::vector<input::GlobalAttachment>& globals() const noexcept override { return m_globals; }

    [[nodiscard]] const std::vector<ModuleEntry>& entries() const noexcept { return m_entries; }
    [[nodiscard]] input::ModuleCatalog catalog() const;

private:
    [[nodiscard]] const ModuleEntry* find(std::string_view module, exthost::InstanceKind kind) const;
    [[nodiscard]] std::uint64_t open(const ModuleEntry& entry, const input::ModuleRequest& request);

    std::vector<ModuleEntry> m_entries;
    Notify m_notify;
    std::vector<input::GlobalAttachment> m_globals;
};

}
