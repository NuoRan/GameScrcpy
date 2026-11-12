# Logic guide

[简体中文](logic.md) · English

The logic of an extension is written in TypeScript (or JavaScript) against the same script API as profile logic. It runs in the low-privilege `gs-scripthost.exe` child process, cannot touch files, the network or devices by itself, and does everything through GameScrcpy, which checks it against the permissions the manifest asks for and the user allowed. This page covers what is particular to extensions; the parameters, results and errors of each function are in [sdk/docs](../../sdk/docs/README.md).

## Scripts

```json
"requires": { "script": "^1.0" },
"contributes": {
  "scripts": [
    { "name": "main", "entry": "src/main.ts", "run": "app" },
    { "name": "phone", "entry": "src/phone.ts", "run": "session" }
  ]
}
```

| | App script `"run": "app"` | Session script `"run": "session"` |
|:--|:--|:--|
| How many | One while the extension is on | One for each phone that is mirroring |
| When it runs | From when the extension is turned on and GameScrcpy starts until it is turned off or replaced, or the program quits | From when the phone starts mirroring until it disconnects |
| What it can use | The places of the main window (pages, Settings, card menus, the tray…), services, MCP tools, local connections, the network and files, speech, hardware, program events | The places of the mirror window (side panel, right-click menu, status area, toolbar button), and this phone's touches, vision and recognition, key mapping and clipboard, as well as the network and files |
| What it cannot use | Functions that need a phone (`touch.*`, `vision.*`, `ui.toast` and the like) give `GS-API-073` or `GS-API-001` | The places of the main window |

- A function that needs a permission the extension did not ask for, or the user did not allow, throws `GS-API-072`, naming the permission. The user's own logic has no such limits.
- Scripts can import other files of the package with `import ... from "./util.ts"`. GameScrcpy does not compile TypeScript, it only erases the types, so `enum`, namespaces with code, constructor parameter properties, decorators, `import x = …` and `export =` cannot be used.
- What a task opens (a task started by an event handler, `every` and the like) closes when the task ends, and what the top level opens closes when the logic stops: listeners, servers of `net.listen`, speech listeners, hardware devices and claims alike.
- `store.get` / `store.set` is the script's own store (up to 1 MiB of JSON, one for each phone in a session script), kept across restarts. Scripts cannot read the values of the manifest's `settings`; they only get the `settings.changed` event (with the keys that changed). Keep a script's own options in a panel and in `store`.
- `log.info` and the like write to the program log (`logic <name> on <phone>: …`).
- When the sandbox process of an app script stops unexpectedly, GameScrcpy starts it again a second later; after three stops within a minute, it stops the extension's logic.
- While developing, put a `tsconfig.json` in the extension folder (copy the one in [examples/components](../../examples/components)) and type-check with `gs check <folder>`; `gs types <folder>` writes the API's types into `types/` for your editor. `gs check` wants a `src/main.ts` (or `src/main.js`), so that is the easiest place for one of the scripts.

## Panels and places in the interface

An extension runs no interface code: its script gives a tree that describes the interface, GameScrcpy draws it with its own controls and the current theme, and what the user does comes back to the script as events. Panels are declared in `contributes.panels` of the manifest (with the extension points `panel` and `script`; see the [manifest reference](manifest.en.md)).

```ts
const page = ui.panel("laps");
let laps: number[] = [];

function draw(): void {
    page.render({
        type: "column",
        children: [
            { type: "text", text: `${laps.length} laps`, style: "heading" },
            { type: "table", columns: ["Lap", "Time"], rows: laps.map((ms, index) => [index + 1, `${ms} ms`]) },
            { type: "button", id: "clear", title: "Clear", style: "danger", enabled: laps.length > 0 },
        ],
    });
}

page.on("click", ({ id }) => {
    if (id === "clear") {
        laps = [];
        draw();
    }
});

draw();
```

- `render(tree)` replaces what the panel showed, and `render(null)` empties it. A tree is at most 256 KiB, 2000 nodes and 16 levels deep; GameScrcpy draws at most 30 trees a second, so of quicker ones the last shows. A tree that is not right throws `GS-API-075`, naming the node; a panel not declared for this script throws `GS-API-076`.
- Nodes: `column`, `row`, `group` (a card with a title), `text` (up to 4000 characters; with `format: "markdown"` it shows bold, italics, code and lists, and never opens links or shows pictures), `button`, `field`, `number`, `toggle`, `choice`, `slider`, `list`, `table` (up to 16 columns and 200 rows), `chart`, `canvas`, `image` (a PNG or SVG of at most 2 MiB in the extension folder), `progress`, `separator`, `deviceItem`, `steps`, `qr` and `keyValue`. Nodes the user works with need an `id`.
- Events: `on(type, handler)` with the type `click` (buttons), `change` (fields, numbers, toggles, choices and sliders, with the new `value`), `submit` (Enter in a field), `select` (list items), `open` and `close` (the panel shown and hidden). The handler gets `{ id, value, signal }`.
- The texts in a tree come from the script and do not go through the manifest's `l10n`.

How each place is used from a script:

| `where` | In the script |
|:--|:--|
| `mirror`, `page`, `settings`, `extension`, `device` | `render` / `on` of `ui.panel(name)`; for `device` the `id` of `open` / `close` is the device id; `settings` gets `open` / `close` only while that section of Settings is open |
| `menu` | `ui.menu(items)` puts items into the right-click menu of the mirror picture, and `ui.menu.on("select", …)` hears the one picked |
| `status` | `ui.status(name).render(tree)`: a line of text, icons and progress, at most 64 nodes and 100 characters of text |
| `toolbar` | `ui.toolbar(name).set({ title, icon, checked, enabled })` changes the button, `set(null)` brings back what the manifest declares, and `on("click", …)` hears clicks. Once `ui.panel` of the same name has drawn a tree, a click also opens it under the button |
| `contextMenu` | `ui.contextMenu(name).set(items)` (up to 16) puts items into the More menu of cards; `on("choose", ({ id, value, target }) => …)`, where `target` is `{ kind: "device" or "profile", id, name }` |
| `tray` | `ui.tray(name).set(items)` (up to 16) puts items into the tray menu; `on("choose", …)` |

Menu items are `{ id, title, icon, checked, enabled }`; an item with `checked` shows a check mark, and picking it sends the opposite as `value`. Icons are GameScrcpy icon names; panels, toolbar buttons and `deviceItem` also take `svg:<package path>` for an SVG of the extension (rules in the [manifest reference](manifest.en.md)).

## Program events

```ts
events.on("profile.applied", ({ value }) => {
    log.info("profile applied", JSON.stringify(value));
});
```

`events.on(topic, handler, options)` starts a task for each event; the handler gets `{ topic, value, signal }`. The topics and their values are in the table of the [native guide](native.en.md#program-events) and in [sdk/docs/events.md](../../sdk/docs/events.md). Who hears what:

- The user's own logic hears every event.
- The logic of an extension always hears `extension.enabled`, `extension.disabled` and `settings.changed` (only for its own settings and the language and look settings).
- A session script of an extension also hears the events of its own phone; the events of other phones, and phone events for app scripts and tasks, need the permission `events.app`. Without it those events just do not come; nothing fails.
- Any other topic is one that a native DLL sends with `emit_event`: the extension's own logic writes the topic alone, and an extension it depends on is written `"<extension id>:<topic>"` (listed in `dependencies`).
- Each topic delivers its first 100 events in a second at once and merges the rest of that second into its last one.

## Services between extensions

A service is a function that an extension offers to other extensions (and to the user's own logic). The extension that offers it declares it in the manifest and answers it in an app script:

```json
"services": [
  { "name": "best", "description": "The best lap so far, in milliseconds, or null.",
    "input": { "type": "object", "properties": { "track": { "type": "string" } }, "required": ["track"] },
    "output": { "type": ["integer", "null"] },
    "script": "main" }
]
```

```ts
const best = new Map<string, number>();

ext.provide<{ track: string }>("best", ({ track }, { caller }) => {
    log.info("asked by", caller === "" ? "the user's own logic" : caller);
    return best.get(track) ?? null;
});
```

The caller lists the provider in `dependencies` (optional dependencies count too), then:

```ts
try {
    const ms = await ext.call<number | null>("alice.lap-timer/best", { track: "monza" });
    log.info("best lap", ms);
} catch (error) {
    log.warn("no best lap", String(error));
}
```

- The name is `<extension id>/<service>`. The logic of an extension may call only the services of the extensions it depends on and its own (otherwise `GS-API-147`); the user's own logic may call any service of an extension that is on.
- Each call is a task of its own in the provider; the handler gets the arguments and `{ caller, permissions, signal }`: the calling extension's id (an empty string for the user's own logic), the permissions the call may use, and a signal that aborts when the caller cancels or after 10 seconds. What it returns is the result (nothing gives `null`); what it throws is the failure.
- **Permissions are those both sides have**: when the handler uses something that needs a permission (such as `hardware.*`, `input.source`, `net.*` or `speech.listen`), the caller needs that permission too, or it is refused (`GS-API-072`). A service that only reads data and needs no permission can be called by anyone.
- Arguments and results are JSON of at most 252 KiB that match the manifest's `input` / `output` (with only `type`, `properties`, `required`, `items`, `enum`, `minimum`, `maximum`, `maxLength`, `title` and `description`).
- Errors: `GS-API-145` (`ext.provide` of a service the manifest does not declare for this script), `GS-API-146` (no extension that is on offers it, or its app script did not `provide` it within 10 seconds), `GS-API-147` (no dependency declared), `GS-API-148` (arguments or result not matching the schemas, not JSON or too large), `GS-API-149` (the service failed, with its own message, or took longer than 10 seconds).
- Cancelling the calling task cancels the call. A native node can answer a service too; see the [native guide](native.en.md#services).

## Incoming local connections

`net.listen` opens an HTTP server on `127.0.0.1` that programs on this computer (a Stream Deck plugin, OBS, automation scripts, pages in a browser) can call. The extension asks for the permission `network.listen`; the user can also turn "Accept connections from programs on this computer" off in the extension's details.

```ts
const server = await net.listen(8765, async (request) => {
    if (request.method !== "POST") {
        return { status: 405, headers: { allow: "POST" }, json: { error: "send POST" } };
    }
    const body = request.json();
    log.info("asked", request.path, JSON.stringify(body));
    return { json: { ok: true } };
});
log.info("listening on", server.port);
```

- Ports go from 1024 to 65535. A port in use, or a fifth server of the extension, throws `GS-API-102`; a port or origin not written right throws `GS-API-101`; no permission, or the switch turned off, throws `GS-API-100`.
- Each request goes to the handler as a task of its own, several at a time. A request is `{ method, path, query, headers, text, bytes, json() }`: `path` is decoded and without the query, and header names are lowercase.
- The answer: nothing gives 204; a text gives 200 as plain text; an object gives `{ status, headers, text | json | bytes }`. A handler that throws answers 500. The answer has to come within 10 seconds, or 504 goes back.
- Safety: the server listens on `127.0.0.1` only, so other computers cannot reach it; `Host` must be `127.0.0.1:<port>` or `localhost:<port>`; a request with an `Origin` passes only from pages of the server itself and the sites in the `origins` option (up to 16, written as `https://example.com`), and the program answers their preflight requests; an `Origin` of `null` (local files, and sandboxed frames that any site can open) is refused. Headers are at most 16 KiB and bodies 1 MiB, each server takes at most 16 connections at once, and every answer closes its connection. It speaks HTTP only, not WebSocket.
- A web page can make a browser send a GET without an `Origin` (an image, for one), so accept actions that change something only as POST and use GET for reading.
- When the user turns the switch off, the servers close; turning it on again does not reopen them, so the logic has to call `net.listen` again (or the extension be restarted). A server's `isOpen` tells whether it is still open.

A complete example: [deck-bridge](../../examples/extensions/deck-bridge).

## The network

```ts
const reply = await net.fetch("https://api.github.com/repos/NuoRan/GameScrcpy/releases/latest", {
    headers: { accept: "application/vnd.github+json" },
});
if (reply.status === 200) {
    const release = reply.json() as { tag_name?: string };
    log.info("newest release", release.tag_name ?? "?");
}
```

- `net.fetch(url, { method, headers, body, timeoutMs })` has GameScrcpy make an https request and resolves with the answer whatever its status: `{ status, url, headers, text, bytes, json() }` (`url` is the address after redirects, header names are lowercase). The logic of an extension needs the permission `network` and reaches only the hosts listed in `hosts` of the manifest (the rules are in the [manifest reference](manifest.en.md#permissions)), following redirects only to them and at most 5 times; the user's own logic may reach any https address.
- The body is at most 1 MiB and the answer at most 8 MiB, and each logic has at most 8 requests going at once; `timeoutMs` is 1000 to 60000 (15000 by default) for the whole request. Cookies are not kept and nothing is cached; set the `cookie` header yourself when a service needs one. The headers GameScrcpy sets itself, such as `host` and `content-length`, and those starting with `proxy-` or `sec-` cannot be set. An invalid certificate always fails. Cancelling the task cancels the request.
- `net.socket(url, { protocols })` opens a WebSocket over wss and resolves with a `NetSocket` once the server took it: `send(text or bytes)` (at most 1 MiB each), `close(code, reason)`, `onMessage(handler)` (queued by default, so none is lost), `onClose(handler)`, `isOpen` and `protocol`. The hosts are those of `net.fetch`, and each logic has at most 4 sockets open; one opened in a task closes when the task ends, one opened at the top level when the logic stops.
- Errors: `GS-API-155` (no permission, an unlisted host, or a redirect to an unlisted host or an address that is not https), `GS-API-156` (not an https / wss address, a method or header that is not allowed, a body that is neither text nor bytes, `timeoutMs` out of range), `GS-API-157` (too large a body, answer or backlog, or too many requests or sockets), `GS-API-158` (unreachable, invalid certificate, broken connection, too many redirects, timeout; the message says which), `GS-API-159` (sending on a closed socket).

## Files

```ts
const file = await files.pick({ filters: [{ name: "Key lists", extensions: ["keys"] }] });
if (file) {
    log.info(file.name, (await file.text()).length);
}

const notes = await files.folder("notes");
await notes.write("today.txt", "Bring the map.");
for (const entry of await notes.list()) {
    log.info(entry.path, entry.size);
}
```

- `files.pick({ save, filters, suggestedName })` shows the Windows dialog to open a file (to save one with `save`) and resolves with a handle to the file the user picked, or with `null` when the user cancels. The handle reads and writes only that file (up to 64 MiB) and lasts until the logic stops: `read()`, `text()`, `write(text or bytes)`, `close()`. The logic of an extension needs the permission `files.pick`.
- `files.folder(name)` opens a folder chosen for this logic. The name is declared in `folders` of the permission `files.paths` in the manifest (the user's own logic may use any 1 to 32 lowercase letters, digits and hyphens). The first time, GameScrcpy asks the user to choose a real folder and remembers it (the dialog shows the `purpose`); the user can forget it in the extension's settings (for the user's own logic, in Settings › General). The handle lists, reads, writes and removes only what is inside that folder: `list(subfolder)` (up to 4096 entries), `read(path)`, `text(path)`, `write(path, data)` (making the missing folders), `remove(path)` and `close()`. Paths use `/`; `..`, drives and links that lead out of the folder are refused.
- Each logic has at most 64 files and folders open and at most 8 requests waiting for the user; writes are atomic, replacing the old file only when complete.
- Errors: `GS-API-160` (no permission, or the manifest does not declare the folder), `GS-API-161` (options, a name or a path that is not right), `GS-API-162` (Windows cannot do it, for example the file is in use or changed while it was read), `GS-API-163` (the file is too large, or too many are open or waiting), `GS-API-164` (the handle is closed, or the user closed the dialog without choosing a folder).

A complete example: [notes-panel](../../examples/extensions/notes-panel).

## Reading aloud and voice commands

```ts
await speech.say("Ready", { rate: 1.1 });

const listener = await speech.listen(["open map", "close map"], async ({ phrase, confidence }) => {
    log.info("heard", phrase, confidence);
    await speech.say(phrase === "open map" ? "Map open" : "Map closed", { interrupt: true });
}, { confidence: 0.7 });
```

- `speech.say(text, { engine, voice, rate, volume, interrupt })` reads 1 to 1000 characters aloud and gives `true` once it is read, `false` when it is cut off by interrupt, `speech.stop()`, a cancelled task or the logic stopping. Each engine reads one text at a time and the others wait; `interrupt: true` cuts off what is being read and drops what waits. Reading aloud needs no permission.
- `speech.listen(commands, handler, { engine, language, confidence })` listens to the microphone for 1 to 256 commands of 1 to 64 characters each; each command heard with at least `confidence` (0.6 by default) runs the handler, one after another, with `{ phrase, text, confidence }`. With an engine that takes any words, the commands can be `null`. It needs the permission `speech.listen` (otherwise `GS-API-113`). A listener opened in a task closes with the task and one opened at the top level when the logic stops; it also closes when its engine stops, and the log says why; `isOpen` tells whether it still listens.
- `speech.engines()` lists the engines: `system` is the one built into Windows (classic and OneCore voices for reading aloud; the installed recognizers, with a list of commands only, for listening), the others are `<extension id>/<name>` engines from extensions. `speech.voices({ engine })` lists voices. Options left out come from Settings › Audio › "Reading aloud and voice commands".
- Errors: `GS-API-110` (no such engine, or it cannot read aloud, cannot listen, does not know the language or has no such voice), `GS-API-111` (wrong arguments, or too many texts waiting or listeners open), `GS-API-112` (the engine failed, such as without a microphone), `GS-API-113` (the permission is missing).

A complete example: [voice-commands](../../examples/extensions/voice-commands).

## Hardware and home-made devices

- `hardware.request({ kind, filters })` lists the connected serial ports, HID or Bluetooth LE devices for the user to tick; `hardware.list()` lists those already allowed; `hardware.open(id, serial options)` opens one (a device can be open only once in GameScrcpy); `hardware.forget(id)` gives it up. A device opened in a task closes with the task, one opened at the top level when the logic stops. The logic of an extension needs the permission `hardware` (`GS-API-080`), and every logic can only open the devices the user ticked (`GS-API-081`).
- `hardware.claim(resource, { wait, onWanted })` / `hardware.release(resource)` tell other extensions and GameScrcpy's own sessions that this logic is using the hardware; resources are named as for the native `claim`, and `hardware.open` claims a device by itself.
- `input.source(name)` gives an input source: `press(key)`, `release(key)` and `axis(key, x, y)` feed keys and axes into the key mapping, which profiles bind as `Ext.<extension id>.<name>.<key>` (`Ext.mine.<name>.<key>` for the user's own logic), and while the key editor waits for a key, a press is recorded. An app script has no phone, so its keys go to the phone whose mirror window is in front. The logic of an extension needs the permission `hardware`.

## Native nodes and jobs

The nodes and jobs that the extension's DLL offers hang under `ext.<extension>` in logic (each part of the extension id in lowerCamelCase):

```ts
const version = ext.alice.board.version();
const job = ext.alice.board.flash({ port: "COM9" });
job.onProgress((fraction, text) => log.info(Math.round(fraction * 100), text));
job.onOutput((value) => log.info("found", JSON.stringify(value)));
const result = await job.result;
```

A node gives its result at once (within 100 ms); a job gives an `ExtensionJob`: `result` settles when it ends, `onProgress`, `onLog` and `onOutput` hear its progress, its lines and the parts of the result it hands over on the way, `follow(progress, done, failed)` is how a blueprint's job block is written, and `cancel()` asks it to stop; jobs are cancelled when the logic stops. The error codes are in the [native guide](native.en.md#errors-that-logic-sees).

## Frames of the phone's picture

A session script gets a few frames of the phone's picture with `media.frames(handler, { fps, size, region })` (0.1 to 10 a second, 2 by default; the long side 64 to 1920 pixels, 640 by default), for analysis or AI: the handler gets `{ image, time }`, where `image` is what `vision.capture()` gives. The logic of an extension needs the permission `media.read`; an app script has no phone and gets `GS-API-073`; wrong options give `GS-API-120`, a phone without a picture (such as a touch box without a capture card) `GS-API-121`, and a fifth subscription `GS-API-122`.

## Recognition

```ts
const lines = await vision.recognize("system", "ocr", { x: 0, y: 0.6, width: 0.4, height: 0.3 }, { language: "zh-Hans" });
for (const line of lines ?? []) {
    log.info(line.text, line.confidence);
}
```

- `vision.recognize(engine, kind, region, params)` recognizes in the next frame (or in the picture already shown when no new frame comes within 0.25 s): `ocr` reads text, `detect` finds things and `classify` tells what the picture shows. The results come in the order the engine gives; `x` and `y` are the middle and `width` and `height` the size, as ratios of the whole picture, and `confidence` goes from 0 to 1. It resolves with `null` when there is no picture within 2 seconds. The region may be a region name from `res/resources.json`; without one it is the whole picture.
- `vision.engines()` lists the engines: `system` is the text recognition of Windows (its param `language` is a tag such as `en` or `zh-Hans`, the user's languages by default, and `words: true` gives each word instead of each line); the others are the recognition engines of extensions, `<extension id>/<name>`, with the kinds and languages they recognize. `vision.engine(id)` gives one engine, whose `read()`, `detect()` and `classify()` are short ways to call `recognize`.
- The logic of an extension needs the permission `device.screen`, and only session scripts can recognize (an app script has no phone and gets `GS-API-073`). An engine works on one picture at a time, and GameScrcpy stops waiting for an extension's engine after 5 seconds.
- Errors: `GS-API-170` (no such engine, or its extension is off or did not load), `GS-API-171` (the engine does not recognize this kind or language), `GS-API-172` (the engine, kind or params are not right), `GS-API-173` (the engine failed or its result is not right), `GS-API-174` (it took longer than 5 seconds, is still busy with an earlier picture, or has 16 waiting already).

A complete example: [read-aloud](../../examples/extensions/read-aloud); writing an engine of your own is in the [native guide](native.en.md#recognition-engines).

## MCP tools and AI

- An app script can offer tools to AI clients on this computer (through GameScrcpy's MCP server) with `mcp.tool(name, { description, input }, handler)`; clients see them as `ext_<extension id>_<name>` (dots and hyphens of the id written as underscores). The user confirms each call, the answer has to come within 60 seconds, and each extension offers at most 32 tools.
- `ai.ask(question, { picture, timeoutMs })` asks the service set up in Settings › AI; it needs the permission `ai`, and the user has to turn on "Let logic call AI" there. Each logic asks at most 10 times a minute. App scripts cannot send a picture. The service chosen there may be a model service that another extension provides (see the [native guide](native.en.md#ai-providers)); when that extension is not installed, is off or may not provide AI, `ai.ask` gets `GS-API-150`.

## Converters

`contributes.converters` in the manifest (see the [manifest reference](manifest.en.md#converters-extension-point-script-up-to-16)) names a script whose export named `convert` (or default export) is a function. For each file it converts, GameScrcpy loads that script in the logic sandbox and calls the function once, without a phone:

```ts
export function convert(input: ConvertInput): ConvertOutput {
    if (input.to === "profile") {
        return { name: "Imported.json", json: profileFrom(input.text) };
    }
    return { text: textFrom(input.json()) };
}
```

- The function may be `async`; `input.signal` aborts when the user cancels or after 30 seconds. An import and an export can share one script and tell themselves apart by `input.from` and `input.to`.
- At most 32 MiB go in and 32 MiB come out; the message of what it throws is what the user sees. Errors: `GS-API-165` (the script exports no such function), `GS-API-166` (the result is nothing GameScrcpy can write), `GS-API-167` (the result is over 32 MiB), `GS-API-168` (the file to convert could not be read).

A complete example: [key-list](../../examples/extensions/key-list).

## Guided tours

An extension can bring up to 4 guided tours in `contributes.tours` (extension point `tour`), written as the [manifest reference](manifest.en.md) describes. Tours need no script, but a step that lights up a page or a section of Settings needs that panel to be the extension's own (with `where` `page` or `settings`) and drawn by an app script.

- A tour with `start: "enabled"` plays once after the extension is first turned on and loaded, unless the user turned tours off in Settings. Finished or skipped tours are remembered.
- Each step first goes to the page where its target is and then lights it up; a step whose target is not there (no device, a panel not drawn yet) is skipped, so render the panel first.
- The extension's Details has a replay button for each tour; a help article replays it with the link `action:tour:ext/<extension id>/<tour id>`.
- Titles are at most 60 characters, texts 300, with 2 to 12 steps. Translations go into `l10n`: `tours.<id>.title`, `tours.<id>.<step>.title` and `tours.<id>.<step>.text` (steps counted from 0).

## Help articles

An extension can bring up to 16 help articles (`contributes.help`, extension point `help`). They appear in the Extensions group of the help page's left column, the help search finds them, and the AI assistant's `read_help` reads them (naming the extension they come from). `file` is the English Markdown and `locales` gives a file for each language; when the interface language has none, the English one shows.

Articles are written like GameScrcpy's own help:

- `#` headings, paragraphs, `-` lists, `1.` numbered lists (a **bold** start of an item becomes the title of that step), tables (shown as a shortcut table when the first column holds keys like `<kbd>Ctrl</kbd>`) and code blocks with a language (` ```ts `).
- Inline: `**bold**`, `` `code` ``, `<kbd>key</kbd>`, “names” in curly quotes, and paths such as `**Settings > Audio**`.
- Callouts: quote blocks starting with `> [!TIP]`, `> [!NOTE]` or `> [!WARNING]`; when the last line is a single link, it becomes the callout's button.
- Links: `help:<article>` opens one of GameScrcpy's own articles, `help:ext/<extension id>/<id>` an article of an extension, `action:tour:ext/<extension id>/<tour id>` replays an extension's tour, and `https://` opens the browser.
- Pictures: `![caption](images/pic.png)` takes only relative paths inside the extension folder; a picture with a `:` or one outside the folder is dropped, so the program's own figures (`figure:`) are not available.

Previous and next only move between the articles of the same extension. A complete example: [tour-and-help](../../examples/extensions/tour-and-help).

## Common error codes

| Code | Meaning |
|:--|:--|
| `GS-API-001` | The logic has no phone connected (an app script used a `ui.*` function that needs one) |
| `GS-API-072` | The extension did not ask for the permission or the user did not allow it; in a service, the caller may lack it |
| `GS-API-073` | The logic has no phone: an app script used a function that needs one |
| `GS-API-075`, `GS-API-076` | A panel or menu tree is not right; the manifest does not declare the panel for this script |
| `GS-API-080` to `GS-API-085` | Hardware: no permission, a device not allowed, not there, cannot be opened, wrong options |
| `GS-API-100` to `GS-API-102` | Local connections: no permission or switched off, a wrong port or origin, a port in use or too many servers |
| `GS-API-110` to `GS-API-113` | Speech: the engine, the arguments, an engine failure, the permission |
| `GS-API-120` to `GS-API-122` | Picture frames: the options, no picture, too many subscriptions |
| `GS-API-145` to `GS-API-149` | Services: not declared, nobody offers it, no dependency, not matching the schemas, failed or too slow |
| `GS-API-150` | The model service chosen in Settings › AI belongs to an extension that cannot be used |
| `GS-API-155` to `GS-API-159` | The network: no permission or unlisted host, address or options not right, too many or too large, unreachable or too slow, socket closed |
| `GS-API-160` to `GS-API-164` | Files: no permission or not declared, name or path not right, Windows cannot do it, too many or too large, handle closed or no folder chosen |
| `GS-API-165` to `GS-API-168` | Converters: no function exported, a result that cannot be written, a result too large, a file that cannot be read |
| `GS-API-170` to `GS-API-174` | Recognition: no such engine, kind not recognized, params not right, the engine failed, too slow or too busy |
