#include "gs/platform/InputCapture.h"

#include "gs/platform/Foreground.h"
#include "gs/platform/Thread.h"

#include <windows.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <future>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace gs::platform {

namespace {

constexpr USHORT kGenericDesktop = 0x01;
constexpr USHORT kUsageMouse = 0x02;
constexpr USHORT kUsageJoystick = 0x04;
constexpr USHORT kUsageGamepad = 0x05;
constexpr USHORT kUsageKeyboard = 0x06;
constexpr Microseconds kProbeInterval{100'000};
constexpr Microseconds kArrivalProbeWindow{2'000'000};
constexpr wchar_t kWindowClass[] = L"GameScrcpyInputCapture";
constexpr int kAxisMax = 32767;
constexpr int kTriggerMax = 255;

std::string describeError(const char* what)
{
    return std::string(what) + " (error " + std::to_string(GetLastError()) + ")";
}

PadKind kindOf(SDL_GamepadType type) noexcept
{
    switch (type) {
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE:
        return PadKind::Xbox;
    case SDL_GAMEPAD_TYPE_PS4:
        return PadKind::PlayStation4;
    case SDL_GAMEPAD_TYPE_PS5:
        return PadKind::PlayStation5;
    default:
        return PadKind::Other;
    }
}

PadState padStateOf(SDL_Gamepad* pad)
{
    constexpr std::pair<SDL_GamepadButton, std::uint16_t> kButtons[] = {
        {SDL_GAMEPAD_BUTTON_DPAD_UP, 0x0001},       {SDL_GAMEPAD_BUTTON_DPAD_DOWN, 0x0002},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, 0x0004},     {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 0x0008},
        {SDL_GAMEPAD_BUTTON_START, 0x0010},         {SDL_GAMEPAD_BUTTON_BACK, 0x0020},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, 0x0040},    {SDL_GAMEPAD_BUTTON_RIGHT_STICK, 0x0080},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 0x0100}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 0x0200},
        {SDL_GAMEPAD_BUTTON_SOUTH, 0x1000},         {SDL_GAMEPAD_BUTTON_EAST, 0x2000},
        {SDL_GAMEPAD_BUTTON_WEST, 0x4000},          {SDL_GAMEPAD_BUTTON_NORTH, 0x8000},
    };
    std::uint16_t buttons = 0;
    for (const auto& [button, bit] : kButtons) {
        if (SDL_GetGamepadButton(pad, button)) {
            buttons = static_cast<std::uint16_t>(buttons | bit);
        }
    }
    const auto trigger = [pad](SDL_GamepadAxis axis) {
        return static_cast<std::uint8_t>(std::clamp<int>(SDL_GetGamepadAxis(pad, axis), 0, kAxisMax) * kTriggerMax /
                                         kAxisMax);
    };
    const auto upward = [pad](SDL_GamepadAxis axis) {
        return static_cast<std::int16_t>(std::clamp(-static_cast<int>(SDL_GetGamepadAxis(pad, axis)), -kAxisMax - 1, kAxisMax));
    };
    constexpr std::pair<SDL_GamepadButton, std::uint8_t> kExtras[] = {
        {SDL_GAMEPAD_BUTTON_GUIDE, pad_extra::kGuide},
        {SDL_GAMEPAD_BUTTON_MISC1, pad_extra::kMisc},
        {SDL_GAMEPAD_BUTTON_TOUCHPAD, pad_extra::kTouchpad},
    };
    std::uint8_t extra = 0;
    for (const auto& [button, bit] : kExtras) {
        if (SDL_GetGamepadButton(pad, button)) {
            extra = static_cast<std::uint8_t>(extra | bit);
        }
    }
    return {.connected = true,
            .buttons = buttons,
            .leftTrigger = trigger(SDL_GAMEPAD_AXIS_LEFT_TRIGGER),
            .rightTrigger = trigger(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER),
            .leftX = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX),
            .leftY = upward(SDL_GAMEPAD_AXIS_LEFTY),
            .rightX = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX),
            .rightY = upward(SDL_GAMEPAD_AXIS_RIGHTY),
            .extra = extra,
            .kind = kindOf(SDL_GetGamepadType(pad))};
}

}

struct InputCapture::Impl {
    Impl(InputHandler& target, Microseconds interval)
        : handler(target)
        , pollInterval(interval)
        , wakeEvent(CreateEventW(nullptr, FALSE, FALSE, nullptr))
        , timer(CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS))
    {
        if (timer == nullptr) {
            timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        }
    }

    ~Impl()
    {
        if (timer != nullptr) {
            CloseHandle(timer);
        }
        if (wakeEvent != nullptr) {
            CloseHandle(wakeEvent);
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    void run(const std::stop_token& token, std::promise<void>& ready);
    bool createWindow();
    void destroyWindow();
    void registerPads(bool on);
    void registerCapture(bool on);
    void startPads();
    void stopPads();
    void deviceChanged(Microseconds now);
    void probePads(Microseconds now);
    void pollPad(Microseconds now);
    void handleInput(HRAWINPUT input, Microseconds now);
    void onTimer(Microseconds now);
    void runTasks();
    void arm(Microseconds now);
    void fail(std::string what);
    void rumble(std::uint16_t low, std::uint16_t high, std::uint32_t durationMs);
    void rumbleTriggers(std::uint16_t left, std::uint16_t right, std::uint32_t durationMs);

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    InputHandler& handler;
    const Microseconds pollInterval;
    SteadyClock clock;
    HANDLE wakeEvent = nullptr;
    HANDLE timer = nullptr;
    HWND window = nullptr;
    std::uintptr_t captureTarget = 0;
    mutable std::mutex mutex;
    std::deque<std::function<void()>> tasks;
    std::string problem;
    std::atomic<bool> started{false};
    bool captureRegistered = false;
    bool padsRegistered = false;
    bool padsStarted = false;
    SDL_Gamepad* pad = nullptr;
    std::optional<Microseconds> probeUntil;
    std::optional<Microseconds> nextPoll;
    std::optional<Microseconds> armedFor;
    PadState lastPad;
    std::optional<POINT> lastAbsolute;
    std::vector<BYTE> buffer;
    std::jthread thread;
};

void InputCapture::Impl::fail(std::string what)
{
    const std::scoped_lock lock(mutex);
    if (problem.empty()) {
        problem = std::move(what);
    }
}

LRESULT CALLBACK InputCapture::Impl::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* impl = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (impl != nullptr) {
        if (message == WM_INPUT) {
            impl->handleInput(reinterpret_cast<HRAWINPUT>(lParam), impl->clock.now());
        } else if (message == WM_INPUT_DEVICE_CHANGE) {
            impl->deviceChanged(impl->clock.now());
            return 0;
        }
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool InputCapture::Impl::createWindow()
{
    if (wakeEvent == nullptr || timer == nullptr) {
        fail(describeError("could not create the input thread's event or timer"));
        return false;
    }
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &Impl::windowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        fail(describeError("could not register the input window class"));
        return false;
    }
    window = CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);
    if (window == nullptr) {
        fail(describeError("could not create the input window"));
        return false;
    }
    return true;
}

void InputCapture::Impl::destroyWindow()
{
    if (window != nullptr) {
        DestroyWindow(window);
        window = nullptr;
    }
}

void InputCapture::Impl::registerPads(bool on)
{
    if (on == padsRegistered) {
        return;
    }
    const DWORD flags = on ? RIDEV_DEVNOTIFY : RIDEV_REMOVE;
    const HWND target = on ? window : nullptr;
    const RAWINPUTDEVICE devices[] = {
        {kGenericDesktop, kUsageJoystick, flags, target},
        {kGenericDesktop, kUsageGamepad, flags, target},
    };
    if (RegisterRawInputDevices(devices, static_cast<UINT>(std::size(devices)), sizeof(RAWINPUTDEVICE))) {
        padsRegistered = on;
    } else {
        fail(describeError("could not register for gamepad notifications"));
    }
}

void InputCapture::Impl::registerCapture(bool on)
{
    if (on == captureRegistered || window == nullptr) {
        return;
    }
    const DWORD flags = on ? RIDEV_INPUTSINK : RIDEV_REMOVE;
    const HWND target = on ? window : nullptr;
    const RAWINPUTDEVICE devices[] = {
        {kGenericDesktop, kUsageKeyboard, flags, target},
        {kGenericDesktop, kUsageMouse, flags, target},
    };
    if (RegisterRawInputDevices(devices, static_cast<UINT>(std::size(devices)), sizeof(RAWINPUTDEVICE))) {
        captureRegistered = on;
        lastAbsolute.reset();
    } else {
        fail(describeError(on ? "could not start capturing the keyboard and mouse"
                              : "could not stop capturing the keyboard and mouse"));
    }
}

void InputCapture::Impl::startPads()
{
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_WGI, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_GAMEINPUT, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        fail(std::string("could not start gamepad support: ") + SDL_GetError());
        return;
    }
    SDL_SetJoystickEventsEnabled(false);
    SDL_SetGamepadEventsEnabled(false);
    padsStarted = true;
}

void InputCapture::Impl::stopPads()
{
    if (pad != nullptr) {
        SDL_CloseGamepad(pad);
        pad = nullptr;
    }
    if (padsStarted) {
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        padsStarted = false;
    }
}

void InputCapture::Impl::deviceChanged(Microseconds now)
{
    if (pad == nullptr) {
        probeUntil = now + kArrivalProbeWindow;
        probePads(now);
    }
}

void InputCapture::Impl::probePads(Microseconds now)
{
    if (padsStarted) {
        SDL_UpdateGamepads();
        int count = 0;
        if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
            for (int i = 0; i < count && pad == nullptr; ++i) {
                pad = SDL_OpenGamepad(ids[i]);
            }
            SDL_free(ids);
        }
    }
    if (pad != nullptr) {
        probeUntil.reset();
        nextPoll = now;
        return;
    }
    if (probeUntil && now < *probeUntil) {
        nextPoll = now + kProbeInterval;
    } else {
        probeUntil.reset();
        nextPoll.reset();
    }
}

void InputCapture::Impl::pollPad(Microseconds now)
{
    SDL_UpdateGamepads();
    if (!SDL_GamepadConnected(pad)) {
        SDL_CloseGamepad(pad);
        pad = nullptr;
        if (lastPad.connected) {
            lastPad = {};
            handler.gamepad(lastPad, now);
        }
        probePads(now);
        return;
    }
    const PadState current = padStateOf(pad);
    if (current != lastPad) {
        lastPad = current;
        handler.gamepad(current, now);
    }
    nextPoll = now + pollInterval;
}

void InputCapture::Impl::rumble(std::uint16_t low, std::uint16_t high, std::uint32_t durationMs)
{
    if (pad != nullptr) {
        SDL_RumbleGamepad(pad, low, high, durationMs);
    }
}

void InputCapture::Impl::rumbleTriggers(std::uint16_t left, std::uint16_t right, std::uint32_t durationMs)
{
    if (pad != nullptr &&
        SDL_GetBooleanProperty(SDL_GetGamepadProperties(pad), SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN, false)) {
        SDL_RumbleGamepadTriggers(pad, left, right, durationMs);
    }
}

void InputCapture::Impl::handleInput(HRAWINPUT input, Microseconds now)
{
    if (!isInFront(captureTarget)) {
        lastAbsolute.reset();
        return;
    }
    UINT size = 0;
    if (GetRawInputData(input, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 || size == 0) {
        return;
    }
    buffer.resize(size);
    if (GetRawInputData(input, RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER)) != size) {
        return;
    }
    const auto* raw = reinterpret_cast<const RAWINPUT*>(buffer.data());
    if (raw->header.dwType == RIM_TYPEKEYBOARD) {
        const RAWKEYBOARD& keyboard = raw->data.keyboard;
        if (keyboard.MakeCode == KEYBOARD_OVERRUN_MAKE_CODE) {
            return;
        }
        handler.key({.makeCode = keyboard.MakeCode,
                     .e0 = (keyboard.Flags & RI_KEY_E0) != 0,
                     .e1 = (keyboard.Flags & RI_KEY_E1) != 0,
                     .released = (keyboard.Flags & RI_KEY_BREAK) != 0},
                    now);
    } else if (raw->header.dwType == RIM_TYPEMOUSE) {
        const RAWMOUSE& mouse = raw->data.mouse;
        RawMouse report{.buttonFlags = mouse.usButtonFlags, .buttonData = static_cast<std::int16_t>(mouse.usButtonData)};
        if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0) {
            const bool desktop = (mouse.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0;
            const POINT at{MulDiv(mouse.lLastX, GetSystemMetrics(desktop ? SM_CXVIRTUALSCREEN : SM_CXSCREEN), 65535),
                           MulDiv(mouse.lLastY, GetSystemMetrics(desktop ? SM_CYVIRTUALSCREEN : SM_CYSCREEN), 65535)};
            if (lastAbsolute) {
                report.dx = at.x - lastAbsolute->x;
                report.dy = at.y - lastAbsolute->y;
            }
            lastAbsolute = at;
        } else {
            report.dx = mouse.lLastX;
            report.dy = mouse.lLastY;
        }
        if (report.dx != 0 || report.dy != 0 || report.buttonFlags != 0) {
            handler.mouse(report, now);
        }
    }
}

void InputCapture::Impl::onTimer(Microseconds now)
{
    armedFor.reset();
    if (nextPoll && now >= *nextPoll) {
        if (pad != nullptr) {
            pollPad(now);
        } else {
            probePads(now);
        }
    }
    if (const auto due = handler.nextWakeup(); due && now >= *due) {
        handler.wake(now);
    }
}

void InputCapture::Impl::runTasks()
{
    std::deque<std::function<void()>> pending;
    {
        const std::scoped_lock lock(mutex);
        pending.swap(tasks);
    }
    for (std::function<void()>& task : pending) {
        task();
    }
}

void InputCapture::Impl::arm(Microseconds now)
{
    std::optional<Microseconds> due = handler.nextWakeup();
    if (nextPoll && (!due || *nextPoll < *due)) {
        due = nextPoll;
    }
    if (due == armedFor) {
        return;
    }
    armedFor = due;
    if (!due) {
        CancelWaitableTimer(timer);
        return;
    }
    LARGE_INTEGER when{};
    when.QuadPart = -std::max<LONGLONG>(1, static_cast<LONGLONG>((*due - now).count()) * 10);
    if (!SetWaitableTimer(timer, &when, 0, nullptr, nullptr, FALSE)) {
        fail(describeError("could not arm the input timer"));
    }
}

void InputCapture::Impl::run(const std::stop_token& token, std::promise<void>& ready)
{
    setCurrentThreadName("gs-input");
    setCurrentThreadRole(ThreadRole::Input);
    const bool created = createWindow();
    if (created) {
        registerPads(true);
    }
    started.store(created, std::memory_order_release);
    ready.set_value();
    if (!created) {
        return;
    }
    startPads();
    probePads(clock.now());
    while (!token.stop_requested()) {
        arm(clock.now());
        const HANDLE handles[] = {wakeEvent, timer};
        const DWORD result = MsgWaitForMultipleObjectsEx(static_cast<DWORD>(std::size(handles)), handles, INFINITE,
                                                         QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (result == WAIT_OBJECT_0) {
            runTasks();
        } else if (result == WAIT_OBJECT_0 + 1) {
            onTimer(clock.now());
        } else if (result == WAIT_OBJECT_0 + std::size(handles)) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                DispatchMessageW(&message);
            }
        } else {
            fail(describeError("the input thread stopped waiting"));
            break;
        }
    }
    stopPads();
    registerCapture(false);
    registerPads(false);
    destroyWindow();
}

InputCapture::InputCapture(InputHandler& handler, Microseconds pollInterval)
    : m_impl(std::make_unique<Impl>(handler, pollInterval))
{
    std::promise<void> ready;
    std::future<void> created = ready.get_future();
    m_impl->thread = std::jthread([impl = m_impl.get(), done = std::move(ready)](const std::stop_token& token) mutable {
        impl->run(token, done);
    });
    created.wait();
}

InputCapture::~InputCapture()
{
    m_impl->thread.request_stop();
    if (m_impl->wakeEvent != nullptr) {
        SetEvent(m_impl->wakeEvent);
    }
    if (m_impl->thread.joinable()) {
        m_impl->thread.join();
    }
}

bool InputCapture::started() const noexcept
{
    return m_impl->started.load(std::memory_order_acquire);
}

std::string InputCapture::problem() const
{
    const std::scoped_lock lock(m_impl->mutex);
    return m_impl->problem;
}

Microseconds InputCapture::now() noexcept
{
    return SteadyClock{}.now();
}

void InputCapture::post(std::function<void()> task)
{
    {
        const std::scoped_lock lock(m_impl->mutex);
        m_impl->tasks.push_back(std::move(task));
    }
    SetEvent(m_impl->wakeEvent);
}

void InputCapture::setCaptureTarget(std::uintptr_t window)
{
    post([impl = m_impl.get(), window] {
        impl->captureTarget = window;
        impl->registerCapture(window != 0);
    });
}

void InputCapture::rumble(std::uint16_t low, std::uint16_t high, std::uint32_t durationMs)
{
    post([impl = m_impl.get(), low, high, durationMs] { impl->rumble(low, high, durationMs); });
}

void InputCapture::rumbleTriggers(std::uint16_t left, std::uint16_t right, std::uint32_t durationMs)
{
    post([impl = m_impl.get(), left, right, durationMs] { impl->rumbleTriggers(left, right, durationMs); });
}

}
