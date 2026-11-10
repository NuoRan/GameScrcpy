#include <gamescrcpy_ext.h>

#include <gs/esp32/TouchMethod.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace esp32 = gs::esp32;

const gs_ext_host* g_host = nullptr;

void say(uint32_t level, std::string_view text)
{
    if (g_host != nullptr && g_host->log != nullptr) {
        g_host->log(g_host->context, level, text.data(), text.size());
    }
}

std::string_view failureText(esp32::OpenFailure failure)
{
    switch (failure) {
    case esp32::OpenFailure::NoBoard:
        return "no ESP32 touch box was found";
    case esp32::OpenFailure::PortMissing:
        return "the chosen serial port is not there";
    case esp32::OpenFailure::PortBusy:
        return "another program is using the serial port";
    case esp32::OpenFailure::NoAnswer:
        return "the board does not answer; flash its firmware";
    case esp32::OpenFailure::NoKey:
        return "Wi-Fi has not been set up over the serial port yet";
    case esp32::OpenFailure::BadAddress:
        return "the board's address is not valid";
    case esp32::OpenFailure::Stopped:
        return "opening was stopped";
    }
    return "the board could not be opened";
}

std::string textOf(const nlohmann::json& options, const char* key)
{
    const auto found = options.find(key);
    return found != options.end() && found->is_string() ? found->get<std::string>() : std::string();
}

esp32::Esp32Settings settingsOf(const char* text, size_t size)
{
    esp32::Esp32Settings settings;
    if (text == nullptr || size == 0) {
        return settings;
    }
    const nlohmann::json options = nlohmann::json::parse(std::string_view(text, size), nullptr, false);
    if (!options.is_object()) {
        return settings;
    }
    if (const std::string port = textOf(options, "port"); !port.empty()) {
        settings.port = port;
    }
    settings.link = textOf(options, "link") == "wifi" ? esp32::LinkMode::Wifi : esp32::LinkMode::Serial;
    settings.address = textOf(options, "address");
    const std::string boards = textOf(options, "boardsFile");
    settings.boardsFile = std::filesystem::path(std::u8string(boards.begin(), boards.end()));
    return settings;
}

esp32::Esp32TouchMethod& methodOf(void* session)
{
    return *static_cast<esp32::Esp32TouchMethod*>(session);
}

esp32::ContactAction actionOf(uint32_t action)
{
    switch (action) {
    case GS_EXT_TOUCH_DOWN:
        return esp32::ContactAction::Down;
    case GS_EXT_TOUCH_UP:
        return esp32::ContactAction::Up;
    case GS_EXT_TOUCH_CANCEL:
        return esp32::ContactAction::Cancel;
    default:
        return esp32::ContactAction::Move;
    }
}

uint16_t pictureUnits(double ratio)
{
    return static_cast<uint16_t>(std::lround(std::clamp(ratio, 0.0, 1.0) * 65535.0));
}

int32_t openTouch(void*, const gs_ext_touch_open* info, void** session)
{
    if (info == nullptr || session == nullptr) {
        return GS_EXT_BAD_ARGUMENT;
    }
    auto method = std::make_unique<esp32::Esp32TouchMethod>();
    const auto opened = method->open(settingsOf(info->options, info->options_size));
    if (!opened) {
        say(GS_EXT_LOG_WARNING, std::string("the ESP32 touch box did not open: ") +
                                    std::string(failureText(opened.error())));
        return GS_EXT_FAILED;
    }
    *session = method.release();
    return GS_EXT_OK;
}

void closeTouch(void*, void* session)
{
    const std::unique_ptr<esp32::Esp32TouchMethod> method(static_cast<esp32::Esp32TouchMethod*>(session));
    if (method != nullptr) {
        method->close();
    }
}

int32_t frameTouch(void*, void* session, const gs_ext_touch_frame* frame)
{
    if (frame == nullptr || (frame->count > 0 && frame->contacts == nullptr)) {
        return GS_EXT_BAD_ARGUMENT;
    }
    std::vector<esp32::Contact> contacts;
    contacts.reserve(frame->count);
    for (uint32_t index = 0; index < frame->count; ++index) {
        const gs_ext_contact& contact = frame->contacts[index];
        contacts.push_back(esp32::Contact{.pointerId = contact.id,
                                          .action = actionOf(contact.action),
                                          .x = pictureUnits(contact.x),
                                          .y = pictureUnits(contact.y)});
    }
    return methodOf(session).frame(contacts, static_cast<int>(frame->rotation & 3U)) ? GS_EXT_OK : GS_EXT_FAILED;
}

int32_t keyTouch(void*, void* session, const gs_ext_key* key)
{
    if (key == nullptr) {
        return GS_EXT_BAD_ARGUMENT;
    }
    return methodOf(session).key(key->keycode, key->down != 0) ? GS_EXT_OK : GS_EXT_FAILED;
}

int32_t textTouch(void*, void* session, const char* text, size_t size)
{
    if (text == nullptr || size == 0) {
        return GS_EXT_BAD_ARGUMENT;
    }
    const std::string_view typed(text, size);
    if (!std::ranges::all_of(typed, [](char character) { return static_cast<unsigned char>(character) < 0x80; })) {
        return GS_EXT_UNSUPPORTED;
    }
    return methodOf(session).text(typed) ? GS_EXT_OK : GS_EXT_FAILED;
}

int32_t mouseTouch(void*, void* session, const gs_ext_mouse* mouse)
{
    if (mouse == nullptr) {
        return GS_EXT_BAD_ARGUMENT;
    }
    const auto buttons = static_cast<uint8_t>(mouse->buttons & (GS_EXT_MOUSE_LEFT | GS_EXT_MOUSE_RIGHT | GS_EXT_MOUSE_MIDDLE));
    return methodOf(session).mouse(buttons, mouse->dx, mouse->dy, mouse->wheel) ? GS_EXT_OK : GS_EXT_FAILED;
}

int32_t gamepadTouch(void*, void* session, const gs_ext_gamepad* pad)
{
    if (pad == nullptr) {
        return GS_EXT_BAD_ARGUMENT;
    }
    const auto axis = [](int32_t value) { return static_cast<int16_t>(std::clamp<int32_t>(value, -32768, 32767)); };
    const auto trigger = [](uint32_t value) { return static_cast<uint8_t>(std::min<uint32_t>(value, 255U)); };
    const esp32::Esp32Gamepad state{.buttons = pad->buttons,
                                    .hat = static_cast<uint8_t>(std::min<uint32_t>(pad->hat, GS_ESP32_HAT_CENTERED)),
                                    .lx = axis(pad->left_x),
                                    .ly = axis(pad->left_y),
                                    .rx = axis(pad->right_x),
                                    .ry = axis(pad->right_y),
                                    .lt = trigger(pad->left_trigger),
                                    .rt = trigger(pad->right_trigger)};
    return methodOf(session).gamepad(state) ? GS_EXT_OK : GS_EXT_FAILED;
}

const gs_ext_touch_method kMethods[] = {
    {"esp32", static_cast<uint32_t>(esp32::kMaxContacts), 0, 0, nullptr, &openTouch, &closeTouch, &frameTouch, &keyTouch,
     &textTouch, &mouseTouch, &gamepadTouch},
};

}

GS_EXT_EXPORT int32_t gs_ext_entry(const gs_ext_host* host, gs_ext_module* module)
{
    if (host == nullptr || module == nullptr || host->abi_version != GS_EXT_ABI_VERSION) {
        return GS_EXT_UNSUPPORTED;
    }
    g_host = host;
    module->abi_version = GS_EXT_ABI_VERSION;
    module->touch_method_count = 1;
    module->touch_methods = kMethods;
    return GS_EXT_OK;
}
