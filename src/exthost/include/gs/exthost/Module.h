#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gs::exthost {

inline constexpr std::size_t kMostTouchMethods = 8;
inline constexpr std::size_t kMostNodes = 64;
inline constexpr std::size_t kMostParams = 16;
inline constexpr std::size_t kMostContacts = 32;
inline constexpr std::size_t kMostTextBytes = 256 * 1024 - 4096;
inline constexpr std::size_t kMostOptionBytes = 16 * 1024;

enum class ValueType : std::uint8_t { None, Boolean, Number, String, Point, Region, Color, Duration, Json };

inline constexpr std::array<std::string_view, 8> kValueTypeNames{"boolean", "number", "string",   "point",
                                                                 "region",  "color",  "duration", "json"};

[[nodiscard]] std::optional<ValueType> valueTypeNamed(std::string_view name) noexcept;
[[nodiscard]] std::string_view valueTypeName(ValueType type) noexcept;
[[nodiscard]] bool validColor(std::string_view text) noexcept;
[[nodiscard]] bool validUtf8(std::string_view text) noexcept;

inline constexpr std::size_t kMostResourceBytes = 260;
inline constexpr std::size_t kMostClaimerBytes = 512;

enum class ClaimResult : std::int32_t { Claimed = 0, Unsupported = 2, BadArgument = 4, Busy = 5 };

[[nodiscard]] bool validResource(std::string_view resource) noexcept;

struct Value {
    ValueType type = ValueType::None;
    bool flag = false;
    double number = 0.0;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    std::string text;

    friend bool operator==(const Value&, const Value&) = default;
};

[[nodiscard]] std::string checkValue(const Value& value, ValueType expected);

struct TouchMethodInfo {
    std::string name;
    int maxContacts = 1;
    bool canCancel = false;
    bool needsSession = true;
    bool keys = false;
    bool text = false;
    bool mouse = false;
    bool gamepad = false;

    friend bool operator==(const TouchMethodInfo&, const TouchMethodInfo&) = default;
};

struct NodeInfo {
    std::string name;
    std::vector<ValueType> params;
    ValueType returns = ValueType::None;

    friend bool operator==(const NodeInfo&, const NodeInfo&) = default;
};

inline constexpr std::size_t kMostModuleAttachments = 32;
inline constexpr std::size_t kMostModuleBehaviors = 32;
inline constexpr std::size_t kMostBehaviorSlots = 8;
inline constexpr std::size_t kMostFingers = 10;
inline constexpr std::size_t kMostStepCommands = 32;
inline constexpr std::size_t kMostStepEvents = 16;
inline constexpr std::size_t kMostParamBytes = 64 * 1024;

enum class SlotKind : std::uint8_t { Key, Axis2, Axis1 };

struct AttachmentInfo {
    std::string name;

    friend bool operator==(const AttachmentInfo&, const AttachmentInfo&) = default;
};

struct BehaviorInfo {
    std::string name;
    std::vector<SlotKind> slotKinds;
    int fingers = 1;
    bool frames = false;

    friend bool operator==(const BehaviorInfo&, const BehaviorInfo&) = default;
};

inline constexpr std::size_t kMostJobs = 32;
inline constexpr std::size_t kMostRunningJobs = 4;
inline constexpr std::size_t kMostJobResultBytes = kMostTextBytes;
inline constexpr std::size_t kMostJobLineBytes = 1024;

struct JobInfo {
    std::string name;

    friend bool operator==(const JobInfo&, const JobInfo&) = default;
};

inline constexpr std::size_t kMostProviders = 8;
inline constexpr std::size_t kMostActionBytes = 64;
inline constexpr std::size_t kMostDeviceBytes = 256;
inline constexpr std::size_t kMostNameBytes = 64;

struct ProviderInfo {
    std::string name;
    bool acts = false;

    friend bool operator==(const ProviderInfo&, const ProviderInfo&) = default;
};

struct JobEvents {
    std::function<void(double fraction, std::string text)> progress;
    std::function<void(int level, std::string text)> log;
    std::function<void(bool worked, std::string text)> done;
    std::function<void(std::string json)> output;
};

inline constexpr std::size_t kMostJobOutputBytes = 64 * 1024;
inline constexpr std::size_t kMostJobOutputsPerSecond = 100;

inline constexpr std::size_t kMostTopicBytes = 64;
inline constexpr std::size_t kMostEventBytes = 64 * 1024;
inline constexpr std::size_t kMostEventsPerSecond = 100;
inline constexpr std::size_t kMostTopics = 1024;
inline constexpr std::size_t kMostSettingsBytes = 192 * 1024;

[[nodiscard]] bool validTopic(std::string_view topic) noexcept;

struct ModuleInfo {
    std::vector<TouchMethodInfo> touchMethods;
    std::vector<NodeInfo> nodes;
    std::vector<AttachmentInfo> attachments;
    std::vector<BehaviorInfo> behaviors;
    std::vector<JobInfo> jobs;
    std::vector<ProviderInfo> providers;

    friend bool operator==(const ModuleInfo&, const ModuleInfo&) = default;
};

enum class InstanceKind : std::uint8_t { Attachment, Behavior };
enum class StepCall : std::uint8_t { Input, Output, Frame, Wake };
enum class InputKind : std::uint8_t { KeyDown, KeyUp, Axis2, Axis1, Mouse, Wheel };
enum class CommandKind : std::uint8_t { Down, Move, Up };

struct InputEvent {
    InputKind kind = InputKind::KeyDown;
    std::uint32_t slot = 0;
    double x = 0.0;
    double y = 0.0;
    std::uint64_t at = 0;

    friend bool operator==(const InputEvent&, const InputEvent&) = default;
};

struct TouchCommand {
    CommandKind kind = CommandKind::Down;
    std::uint32_t finger = 0;
    double x = 0.0;
    double y = 0.0;
    std::uint64_t at = 0;

    friend bool operator==(const TouchCommand&, const TouchCommand&) = default;
};

struct StepInput {
    StepCall call = StepCall::Input;
    InputEvent event;
    TouchCommand command;
    std::uint64_t now = 0;
    std::uint64_t wakeAt = 0;
};

enum class StepStatus : std::uint8_t { Ok, Skipped, Failed };

struct StepOutput {
    StepStatus status = StepStatus::Skipped;
    std::vector<TouchCommand> commands;
    std::vector<InputEvent> events;
    std::uint64_t wakeAt = 0;
    bool fresh = false;
};

enum class TouchAction : std::uint8_t { Down, Move, Up, Cancel };

struct Contact {
    std::uint32_t id = 0;
    TouchAction action = TouchAction::Down;
    double x = 0.0;
    double y = 0.0;
    double naturalX = 0.0;
    double naturalY = 0.0;
    double pressure = 0.0;

    friend bool operator==(const Contact&, const Contact&) = default;
};

struct TouchFrame {
    std::vector<Contact> contacts;
    std::uint32_t rotation = 0;

    friend bool operator==(const TouchFrame&, const TouchFrame&) = default;
};

struct KeyEvent {
    std::uint32_t keycode = 0;
    bool down = true;
    std::uint32_t repeat = 0;
    std::uint32_t meta = 0;

    friend bool operator==(const KeyEvent&, const KeyEvent&) = default;
};

struct MouseEvent {
    std::uint32_t buttons = 0;
    std::int32_t dx = 0;
    std::int32_t dy = 0;
    std::int32_t wheel = 0;

    friend bool operator==(const MouseEvent&, const MouseEvent&) = default;
};

inline constexpr std::uint32_t kPadHatCentred = 8;

struct PadEvent {
    std::uint32_t buttons = 0;
    std::uint32_t hat = kPadHatCentred;
    std::int32_t leftX = 0;
    std::int32_t leftY = 0;
    std::int32_t rightX = 0;
    std::int32_t rightY = 0;
    std::uint32_t leftTrigger = 0;
    std::uint32_t rightTrigger = 0;

    friend bool operator==(const PadEvent&, const PadEvent&) = default;
};

enum class CallStatus : std::uint8_t { Ok, Failed, BadArgument, TooSlow, Unavailable, Crashed };

struct CallResult {
    CallStatus status = CallStatus::Unavailable;
    Value value;
    std::string problem;

    [[nodiscard]] bool ok() const noexcept { return status == CallStatus::Ok; }
};

class HostServices
{
public:
    virtual ~HostServices() = default;

    virtual void log(int level, std::string_view text) = 0;
    [[nodiscard]] virtual bool notify(std::string_view title, std::string_view text) = 0;
    virtual void emitted(std::string_view, std::string_view) {}
    virtual void devicesChanged(std::string_view) {}
    [[nodiscard]] virtual ClaimResult claim(std::string_view, bool) { return ClaimResult::Unsupported; }
    virtual void release(std::string_view) {}
};

class Module
{
public:
    virtual ~Module() = default;

    [[nodiscard]] virtual const ModuleInfo& info() const noexcept = 0;
    [[nodiscard]] virtual CallResult call(std::size_t node, std::span<const Value> arguments) = 0;
    [[nodiscard]] virtual std::uint64_t openTouch(std::size_t method, std::string_view device,
                                                  std::string_view options = {}) = 0;
    [[nodiscard]] virtual bool touch(std::uint64_t session, const TouchFrame& frame) = 0;
    [[nodiscard]] virtual bool key(std::uint64_t session, const KeyEvent& key) = 0;
    [[nodiscard]] virtual bool text(std::uint64_t, std::string_view) { return false; }
    [[nodiscard]] virtual bool mouse(std::uint64_t, const MouseEvent&) { return false; }
    [[nodiscard]] virtual bool gamepad(std::uint64_t, const PadEvent&) { return false; }
    virtual void closeTouch(std::uint64_t session) = 0;
    [[nodiscard]] virtual bool working() const noexcept { return true; }
    [[nodiscard]] virtual std::uint64_t openInstance(InstanceKind, std::size_t, std::string_view, std::uint64_t)
    {
        return 0;
    }
    [[nodiscard]] virtual StepOutput step(std::uint64_t, const StepInput&) { return {}; }
    virtual void resetInstance(std::uint64_t) {}
    virtual void closeInstance(std::uint64_t) {}
    [[nodiscard]] virtual std::uint64_t skippedSteps() const noexcept { return 0; }
    [[nodiscard]] virtual std::uint64_t startJob(std::size_t, std::string_view, JobEvents) { return 0; }
    virtual void cancelJob(std::uint64_t) {}
    virtual void setSettings(std::string_view) {}
    [[nodiscard]] virtual CallResult listDevices(std::size_t) { return {}; }
    [[nodiscard]] virtual CallResult actOnDevice(std::size_t, std::string_view, std::string_view) { return {}; }
    [[nodiscard]] virtual bool claimWanted(std::string_view, std::string_view) { return false; }
};

}
