#include "gs/input/Modules.h"

#include <algorithm>
#include <set>
#include <string>
#include <variant>

namespace gs::input {

namespace {

std::uint64_t mixed(std::uint64_t value) noexcept
{
    value += 0x9E3779B97F4A7C15ULL;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

const ModuleCatalog::Attachment* attachmentNamed(const ModuleCatalog& catalog, std::string_view module)
{
    const auto found = std::ranges::find(catalog.attachments, module, &ModuleCatalog::Attachment::module);
    return found != catalog.attachments.end() ? &*found : nullptr;
}

const ModuleCatalog::Behavior* behaviorNamed(const ModuleCatalog& catalog, std::string_view module)
{
    const auto found = std::ranges::find(catalog.behaviors, module, &ModuleCatalog::Behavior::module);
    return found != catalog.behaviors.end() ? &*found : nullptr;
}

bool sourceFits(BehaviorSlotKind kind, const Chord& chord) noexcept
{
    const bool twoDimensional = kindOf(chord.source) == SourceKind::Axis2D;
    return kind == BehaviorSlotKind::Axis2 ? twoDimensional : !twoDimensional;
}

}

std::string_view moduleExtension(std::string_view module) noexcept
{
    const std::size_t slash = module.rfind('/');
    return slash == std::string_view::npos ? std::string_view() : module.substr(0, slash);
}

bool attachmentFits(const ModuleCatalog::Attachment& attachment, std::string_view type) noexcept
{
    return attachment.appliesTo.empty() || std::ranges::any_of(attachment.appliesTo, [type](const std::string& each) {
               return each == "*" || each == type;
           });
}

bool takesGlobal(const Control& control, const GlobalAttachment& global) noexcept
{
    const std::string_view type = kControlTypeNames[control.kind.index()];
    const bool fits = global.appliesTo.empty() || std::ranges::any_of(global.appliesTo, [type](const std::string& each) {
                          return each == "*" || each == type;
                      });
    return fits && std::ranges::none_of(control.skipGlobal,
                                        [&global](const SkippedGlobal& skipped) { return skipped.module == global.module; }) &&
           std::ranges::none_of(control.attachments,
                                [&global](const Attachment& own) { return own.module == global.module; });
}

const std::vector<GlobalAttachment>& ModuleFactory::globals() const noexcept
{
    static const std::vector<GlobalAttachment> none;
    return none;
}

std::uint64_t moduleSeed(std::uint64_t base, std::uint64_t control, std::uint64_t place) noexcept
{
    return mixed(mixed(mixed(base) ^ control) ^ place);
}

std::vector<Diagnostic> checkModules(const Profile& profile, const ModuleCatalog& catalog)
{
    std::vector<Diagnostic> found;
    for (std::size_t i = 0; i < profile.controls.size(); ++i) {
        const Control& control = profile.controls[i];
        const std::string path = "$.controls[" + std::to_string(i) + "]";
        const std::string_view type = kControlTypeNames[control.kind.index()];
        for (std::size_t j = 0; j < control.attachments.size(); ++j) {
            const std::string& module = control.attachments[j].module;
            if (!isModuleRef(module)) {
                continue;
            }
            const std::string where = path + ".attachments[" + std::to_string(j) + "].module";
            const ModuleCatalog::Attachment* known = attachmentNamed(catalog, module);
            if (known == nullptr) {
                found.push_back({Severity::Warning, DiagnosticCode::UnknownReference, where,
                                 std::string(moduleExtension(module))});
            } else if (!attachmentFits(*known, type)) {
                found.push_back({Severity::Warning, DiagnosticCode::InvalidValue, where, std::string(type)});
            }
        }
        const auto* behavior = std::get_if<BehaviorControl>(&control.kind);
        if (behavior == nullptr || !isModuleRef(behavior->behavior)) {
            continue;
        }
        const ModuleCatalog::Behavior* known = behaviorNamed(catalog, behavior->behavior);
        if (known == nullptr) {
            found.push_back({Severity::Warning, DiagnosticCode::UnknownReference, path + ".behavior",
                             std::string(moduleExtension(behavior->behavior))});
            continue;
        }
        for (const NamedBinding& entry : behavior->inputs) {
            const std::string where = path + ".inputs." + entry.name;
            const auto slot = std::ranges::find(known->shape.slotList, entry.name, &BehaviorSlot::name);
            if (slot == known->shape.slotList.end()) {
                found.push_back({Severity::Warning, DiagnosticCode::UnknownField, where, entry.name});
            } else if (entry.binding && entry.binding->input && !sourceFits(slot->kind, *entry.binding->input)) {
                found.push_back({Severity::Warning, DiagnosticCode::InputKindMismatch, where,
                                 chordName(entry.binding->input)});
            }
        }
    }
    return found;
}

std::vector<std::string> missingExtensions(const Profile& profile, const ModuleCatalog& catalog)
{
    std::set<std::string, std::less<>> missing;
    for (const Control& control : profile.controls) {
        for (const Attachment& attachment : control.attachments) {
            if (attachment.enabled && isModuleRef(attachment.module) &&
                attachmentNamed(catalog, attachment.module) == nullptr) {
                missing.emplace(moduleExtension(attachment.module));
            }
        }
        const auto* behavior = std::get_if<BehaviorControl>(&control.kind);
        if (behavior != nullptr && isModuleRef(behavior->behavior) &&
            behaviorNamed(catalog, behavior->behavior) == nullptr) {
            missing.emplace(moduleExtension(behavior->behavior));
        }
    }
    return {missing.begin(), missing.end()};
}

}
