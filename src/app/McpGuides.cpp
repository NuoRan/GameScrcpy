#include "McpGuides.h"

#include <QCoreApplication>
#include <QStringList>

#include <algorithm>
#include <array>
#include <string_view>

namespace gs::app {

namespace {

constexpr auto kCommon = QLatin1StringView(
    R"GS(You are working with GameScrcpy: it mirrors Android phones to this computer, maps keys to touches, and runs TypeScript logic (also shown as blueprints) on the phones. Work in this order.
1. Understand the request. If anything is unclear, ask the user before writing code: which app or game, which screen it happens on, what should happen and when (on which key, or by itself), and how to tell that it worked. Ask a few short questions at a time.
2. Look before you guess. Call list_phones and list_profiles, read_profile for the profile in use, and get_frame to see the phone's screen. Positions are ratios of the picture, from (0, 0) at the top left to (1, 1) at the bottom right.
3. Materials. Pictures to find (templates), colors, regions and points live in the profile's res/resources.json; read_profile lists their names. Never invent the name of a template, color, region or point, and never guess a color or a position. When the job needs one that is missing, call request_materials with every item it needs, each named as the logic will use it and with a short note for the user; GameScrcpy opens the vision workbench on the phone's picture with that list, the user frames, picks and boxes each item, and what they give lands in this client's draft (res/resources.json and its pictures).
4. Look up every API you have not used yet in this conversation with api_reference. The recipes (api_reference with the query "recipes", or this server's resources) show complete working code.
5. Write in a draft: write_profile, write_component, write_task or edit_blueprint. Then call check with draft: true and fix every problem it reports.
6. Try it on a recording with run_replay (draft: true, trace: true) and read the trace. list_recordings lists recordings; start_recording and stop_recording make one, after the user confirms.
7. When it works, call apply_draft; the user confirms it in GameScrcpy. Then tell the user what changed and how to use it: which key, and what happens.)GS");

constexpr auto kMakeKeymap = QLatin1StringView(
    R"GS(Suggest controls with propose_keymap: they appear in the user's key editor as dashed hints, and the user accepts or drags each one. Write each control as in profile.json (api_reference with the query "profile format"). Use one control per button on the screen; movement is a wasd control and the camera a camera control. Put each control on the button it presses, as seen in get_frame. When the profile's logic listens to a tap, hold, long press, turbo, swipe or multi-press control's key (onPress, onRelease or whileHeld), the key belongs to the logic and the control does not touch by itself, unless the event is given { consume: false }.)GS");

constexpr auto kVisionLogic = QLatin1StringView(
    R"GS(Find pictures with vision.find, vision.waitFor or vision.findAndTap, and colors with the vision functions api_reference lists for colors. Search a region instead of the whole picture whenever you can: it is faster and less likely to match the wrong place. Give each search a threshold and a timeoutMs. Before you build logic around a template, try it on the live picture with find_image and look at the score. When a picture, a color, a region or a point is missing, ask for it with request_materials instead of guessing; name each item as the logic will use it. Start long loops from a key and give the user a way to stop them, as the recipe "toggle-loop" does.)GS");

constexpr auto kMakeComponent = QLatin1StringView(
    R"GS(list_components lists the components: examples/ are read-only, mine/ are the user's. Write a new one with write_component and mine/<name>; its component.ts default-exports a class that extends Component or a built-in component class, and declares its settings in static props. Try it with run_replay and component in a profile that uses it.)GS");

constexpr auto kMakeTask = QLatin1StringView(
    R"GS(list_tasks lists the tasks. Write a new one with write_task and a new id; its task.ts default-exports an async function or a class that extends Task with run(). Tasks run on a schedule or when the user starts them; they can read controls and listen to keys without taking them from the profile. Try it with run_replay and task.)GS");

constexpr auto kMakeExtension = QLatin1StringView(
    R"GS(create_extension makes a working starter: a color theme, a video filter or a script component. Change it with write_extension, check it with validate_extension and pack it with pack_extension. The user installs the package on GameScrcpy's Extensions page.)GS");

constexpr auto kFindAndTap = QLatin1StringView(
    R"GS(# Find a picture and tap it

Looks for the template once in a region and taps its center. Searching a region is faster than searching the whole picture.

```ts
keys.F.onPress(async () => {
    const button = await vision.find("Start", { region: "Bottom bar", threshold: 0.8, timeoutMs: 1000 });
    if (button !== null) {
        await touch.tap(button.x, button.y);
    } else {
        ui.toast("Start is not on the screen");
    }
});
```)GS");

constexpr auto kWaitThenTap = QLatin1StringView(
    R"GS(# Wait for a picture, then tap it

Waits up to 30 seconds for the template, for example a button that appears after loading.

```ts
keys.F.onPress(async () => {
    const ready = await vision.waitFor("Confirm", { timeoutMs: 30000 });
    if (ready !== null) {
        await touch.tap(ready.x, ready.y);
    }
});
```)GS");

constexpr auto kToggleLoop = QLatin1StringView(
    R"GS(# Start and stop a loop with one key

The first press starts tapping a point again and again; the next press stops it. again: "parallel" lets the second press run while the loop is still going.

```ts
let running = false;
keys.G.onPress(async () => {
    running = !running;
    ui.toast(running ? "Auto tap on" : "Auto tap off");
    while (running) {
        await touch.tap(res.point("Attack"));
        await sleep(150);
    }
}, { again: "parallel" });
```)GS");

constexpr auto kHoldToRepeat = QLatin1StringView(
    R"GS(# Repeat while a key is held

Taps a point every 100 ms while the key is held; the task is cancelled when the key comes up.

```ts
keys.H.whileHeld(async ({ signal }) => {
    while (!signal.aborted) {
        await touch.tap(res.point("Fire"));
        await sleep(100);
    }
});
```)GS");

constexpr auto kSwipePath = QLatin1StringView(
    R"GS(# Drag from one point to another

Swipes from one named point to another in 300 ms, for example to drag an item.

```ts
keys.J.onPress(async () => {
    await touch.swipe(res.point("Item"), res.point("Bag"), 300);
});
```)GS");

constexpr auto kReadNumber = QLatin1StringView(
    R"GS(# Read a number from the screen

Reads the text in a region and keeps only its digits.

```ts
keys.K.onPress(async () => {
    const seen = await vision.readText({ region: res.region("Gold") });
    if (seen !== null) {
        const gold = Number(seen.text.replace(/\D/g, ""));
        log.info("gold", gold);
    }
});
```)GS");

constexpr auto kKeyForLogic = QLatin1StringView(
    R"GS(# A control whose key runs only logic

When the logic listens to a control's key, the key belongs to the logic: the control no longer touches by itself, and the logic presses it when it wants. Give { consume: false } to keep the control's own touch, which then comes first.

```ts
controls.fire.onPress(async () => {
    await sleep(50);
    controls.fire.press();
});
controls.fire.onRelease(() => {
    controls.fire.release();
});
```)GS");

constexpr auto kSkillStart = QLatin1StringView(
    R"GS(---
name: gamescrcpy
description: Use when the user wants to automate an Android game or app with GameScrcpy (key mapping profiles, logic in TypeScript or blueprints, finding pictures and colors, custom components, scheduled tasks, extensions). Needs GameScrcpy's MCP server to be connected.
---

# Working with GameScrcpy)GS");

constexpr auto kSkillEnd = QLatin1StringView(
    R"GS(## Good to know
- Use this server's prompts for common jobs (make_keymap, vision_logic, make_component, make_task, make_extension) and its recipes for complete code.
- Drafts change nothing the user runs until apply_draft, which the user confirms; so try freely in a draft.
- run_script runs code on a phone once the user confirms. Prefer run_replay on a recording, which never touches a phone.
- Keep messages to the user short and concrete: what you found, what you will change, and what they should press to try it.)GS");

struct Guide {
    QString name;
    QString title;
    QString description;
    QString text;
};

std::array<Guide, 5> allGuides()
{
    return {{
        {QStringLiteral("make_keymap"),
         QCoreApplication::translate("McpGuides", "Make a key mapping profile"),
         QCoreApplication::translate(
             "McpGuides", "Set up keys for a game: what to ask first, how to place the controls and check them"),
         QString(kMakeKeymap)},
        {QStringLiteral("vision_logic"),
         QCoreApplication::translate("McpGuides", "Logic that finds pictures and colors"),
         QCoreApplication::translate(
             "McpGuides", "Write logic that reacts to the screen, with the pictures it needs"),
         QString(kVisionLogic)},
        {QStringLiteral("make_component"),
         QCoreApplication::translate("McpGuides", "Make a custom component"),
         QCoreApplication::translate("McpGuides", "Write a component with its own settings and try it on a recording"),
         QString(kMakeComponent)},
        {QStringLiteral("make_task"),
         QCoreApplication::translate("McpGuides", "Make a scheduled task"),
         QCoreApplication::translate("McpGuides", "Write a task that runs on a schedule or on demand"),
         QString(kMakeTask)},
        {QStringLiteral("make_extension"),
         QCoreApplication::translate("McpGuides", "Make an extension"),
         QCoreApplication::translate("McpGuides", "Start, check and pack a theme, video filter or script component"),
         QString(kMakeExtension)},
    }};
}

struct Recipe {
    QLatin1StringView id;
    QLatin1StringView title;
    QLatin1StringView text;
};

constexpr std::array<Recipe, 7> kRecipes{{
    {QLatin1StringView("find-and-tap"), QLatin1StringView("Find a picture and tap it"), kFindAndTap},
    {QLatin1StringView("wait-then-tap"), QLatin1StringView("Wait for a picture, then tap it"), kWaitThenTap},
    {QLatin1StringView("toggle-loop"), QLatin1StringView("Start and stop a loop with one key"), kToggleLoop},
    {QLatin1StringView("hold-to-repeat"), QLatin1StringView("Repeat while a key is held"), kHoldToRepeat},
    {QLatin1StringView("swipe-path"), QLatin1StringView("Drag from one point to another"), kSwipePath},
    {QLatin1StringView("read-number"), QLatin1StringView("Read a number from the screen"), kReadNumber},
    {QLatin1StringView("key-for-logic"), QLatin1StringView("A control whose key runs only logic"), kKeyForLogic},
}};

constexpr auto kSkillUri = QLatin1StringView("gamescrcpy://skill");
constexpr auto kRecipeUriPrefix = QLatin1StringView("gamescrcpy://recipes/");

QJsonObject markdownContents(const QString& uri, const QString& text)
{
    return QJsonObject{
        {QStringLiteral("contents"),
         QJsonArray{QJsonObject{{QStringLiteral("uri"), uri},
                                {QStringLiteral("mimeType"), QStringLiteral("text/markdown")},
                                {QStringLiteral("text"), text}}}}};
}

}

QJsonArray mcpPromptList()
{
    QJsonArray prompts;
    for (const Guide& guide : allGuides()) {
        prompts.append(
            QJsonObject{{QStringLiteral("name"), guide.name},
                        {QStringLiteral("title"), guide.title},
                        {QStringLiteral("description"), guide.description},
                        {QStringLiteral("arguments"),
                         QJsonArray{QJsonObject{
                             {QStringLiteral("name"), QStringLiteral("goal")},
                             {QStringLiteral("description"),
                              QCoreApplication::translate("McpGuides", "What you want, in your own words")},
                             {QStringLiteral("required"), false}}}}});
    }
    return prompts;
}

std::optional<QJsonObject> mcpPrompt(const QString& name, const QJsonObject& arguments)
{
    const std::array<Guide, 5> guides = allGuides();
    const auto found = std::ranges::find_if(guides, [&name](const Guide& guide) { return guide.name == name; });
    if (found == guides.end()) {
        return std::nullopt;
    }
    QString text = QStringLiteral("Job: ") + found->title + QStringLiteral(".");
    const QString goal = arguments.value(QStringLiteral("goal")).toString().trimmed();
    if (!goal.isEmpty()) {
        text += QStringLiteral(" The user wants: ") + goal;
    }
    text += QStringLiteral("\n\n") + QString(kCommon) + QStringLiteral("\n\n") + found->text;
    return QJsonObject{{QStringLiteral("description"), found->description},
                       {QStringLiteral("messages"),
                        QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                               {QStringLiteral("content"),
                                                QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                            {QStringLiteral("text"), text}}}}}}};
}

QJsonArray mcpResourceList()
{
    QJsonArray resources;
    for (const Recipe& recipe : kRecipes) {
        resources.append(QJsonObject{
            {QStringLiteral("uri"), QStringLiteral("gamescrcpy://recipes/") + QString(recipe.id)},
            {QStringLiteral("name"), QString(recipe.id)},
            {QStringLiteral("title"), QString(recipe.title)},
            {QStringLiteral("mimeType"), QStringLiteral("text/markdown")}});
    }
    resources.append(
        QJsonObject{{QStringLiteral("uri"), QString(kSkillUri)},
                    {QStringLiteral("name"), QStringLiteral("skill")},
                    {QStringLiteral("title"), QStringLiteral("How to work with GameScrcpy")},
                    {QStringLiteral("mimeType"), QStringLiteral("text/markdown")}});
    return resources;
}

std::optional<QJsonObject> mcpResource(const QString& uri)
{
    if (uri == QString(kSkillUri)) {
        return markdownContents(uri, mcpSkillText());
    }
    if (uri.startsWith(QString(kRecipeUriPrefix))) {
        if (const std::optional<QString> text = mcpRecipe(QStringView(uri).mid(kRecipeUriPrefix.size()))) {
            return markdownContents(uri, *text);
        }
    }
    return std::nullopt;
}

std::optional<QString> mcpRecipe(QStringView id)
{
    for (const Recipe& recipe : kRecipes) {
        if (QString(recipe.id).compare(id.toString(), Qt::CaseInsensitive) == 0) {
            return QString(recipe.text);
        }
    }
    return std::nullopt;
}

QString mcpRecipeIndex()
{
    QString text = QStringLiteral("## Recipes\n");
    for (const Recipe& recipe : kRecipes) {
        text += QStringLiteral("- ") + QString(recipe.id) + QStringLiteral(": ") + QString(recipe.title) +
                QStringLiteral("\n");
    }
    text += QStringLiteral("Read one with api_reference and its id as the query, or as the MCP resource "
                           "gamescrcpy://recipes/<id>.");
    return text;
}

QString mcpSkillText()
{
    return QString(kSkillStart) + QStringLiteral("\n\n") + QString(kCommon) + QStringLiteral("\n\n") +
           QString(kSkillEnd);
}

}
