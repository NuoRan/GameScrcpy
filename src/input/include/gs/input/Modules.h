#pragma once

#include <gs/base/Clock.h>
#include <gs/base/InplaceVector.h>
#include <gs/input/Geometry.h>
#include <gs/input/Profile.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::input {

inline constexpr std::size_t kMostModuleFingers = 10;
inline constexpr std::size_t kMostModuleCommands = 32;
inline constexpr std::size_t kMostModuleEvents = 16;

enum class ModuleInput : std::uint8_t { KeyDown, KeyUp, Axis2, Axis1, Mouse, Wheel };

struct ModuleEvent {
    ModuleInput kind = ModuleInput::KeyDown;
    std::uint32_t slot = 0;
    double x = 0.0;
    double y = 0.0;
    Microseconds at{0};

    friend bool operator==(const ModuleEvent&, const ModuleEvent&) = default;
};

enum class FingerAction : std::uint8_t { Down, Move, Up };

struct FingerCommand {
    FingerAction action = FingerAction::Down;
    std::uint32_t finger = 0;
    Vec2 position;
    Microseconds at{0};

    friend bool operator==(const FingerCommand&, const FingerCommand&) = default;
};

struct ModuleOutput {
    InplaceVector<FingerCommand, kMostModuleCommands> commands;
    InplaceVector<ModuleEvent, kMostModuleEvents> events;
    std::optional<Microseconds> wakeAt;
    bool fresh = false;
};

enum class ModuleStatus : std::uint8_t { Ok, Skipped, Failed };

class AttachmentInstance
{
public:
    virtual ~AttachmentInstance() = default;

    [[nodiscard]] virtual ModuleStatus input(const ModuleEvent& event, ModuleOutput& out) = 0;
    [[nodiscard]] virtual ModuleStatus output(const FingerCommand& command, ModuleOutput& out) = 0;
    [[nodiscard]] virtual ModuleStatus wake(Microseconds now, ModuleOutput& out) = 0;
    virtual void reset() = 0;
};

class BehaviorInstance
{
public:
    virtual ~BehaviorInstance() = default;

    [[nodiscard]] virtual ModuleStatus input(const ModuleEvent& event, ModuleOutput& out) = 0;
    [[nodiscard]] virtual ModuleStatus frame(Microseconds now, ModuleOutput& out) = 0;
    [[nodiscard]] virtual ModuleStatus wake(Microseconds now, ModuleOutput& out) = 0;
    virtual void reset() = 0;
};

enum class BehaviorSlotKind : std::uint8_t { Key, Axis2, Axis1 };
inline constexpr std::array<std::string_view, 3> kBehaviorSlotKindNames{"key", "axis2", "axis1"};

struct BehaviorSlot {
    std::string name;
    BehaviorSlotKind kind = BehaviorSlotKind::Key;

    friend bool operator==(const BehaviorSlot&, const BehaviorSlot&) = default;
};

struct BehaviorShape {
    std::vector<BehaviorSlot> slotList;
    std::uint8_t fingers = 1;
    bool frames = false;

    friend bool operator==(const BehaviorShape&, const BehaviorShape&) = default;
};

struct ModuleRequest {
    std::string_view module;
    std::string_view control;
    std::string_view type;
    std::string_view params;
    Vec2 position;
    Vec2 unit;
    std::uint64_t seed = 0;
};

enum class ModuleTroubleKind : std::uint8_t { Missing, Failed, Stopped };

struct ModuleTrouble {
    ModuleTroubleKind kind = ModuleTroubleKind::Missing;
    std::string_view module;
    std::string_view control;
};

struct GlobalAttachment {
    std::string module;
    std::string params;
    std::vector<std::string> appliesTo;

    friend bool operator==(const GlobalAttachment&, const GlobalAttachment&) = default;
};

class ModuleFactory
{
public:
    virtual ~ModuleFactory() = default;

    [[nodiscard]] virtual std::unique_ptr<AttachmentInstance> attachment(const ModuleRequest& request) = 0;
    [[nodiscard]] virtual std::unique_ptr<BehaviorInstance> behavior(const ModuleRequest& request,
                                                                     BehaviorShape& shape) = 0;
    virtual void trouble(const ModuleTrouble& trouble) = 0;
    [[nodiscard]] virtual const std::vector<GlobalAttachment>& globals() const noexcept;
};

struct ModuleCatalog {
    struct Attachment {
        std::string module;
        std::vector<std::string> appliesTo;
    };
    struct Behavior {
        std::string module;
        BehaviorShape shape;
    };

    std::vector<Attachment> attachments;
    std::vector<Behavior> behaviors;
};

[[nodiscard]] std::string_view moduleExtension(std::string_view module) noexcept;
[[nodiscard]] bool attachmentFits(const ModuleCatalog::Attachment& attachment, std::string_view type) noexcept;
[[nodiscard]] bool takesGlobal(const Control& control, const GlobalAttachment& global) noexcept;
[[nodiscard]] std::uint64_t moduleSeed(std::uint64_t base, std::uint64_t control, std::uint64_t place) noexcept;
[[nodiscard]] std::vector<Diagnostic> checkModules(const Profile& profile, const ModuleCatalog& catalog);
[[nodiscard]] std::vector<std::string> missingExtensions(const Profile& profile, const ModuleCatalog& catalog);

}
