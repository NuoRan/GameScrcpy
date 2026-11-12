# Manifest reference

[简体中文](manifest.md) · English

`manifest.json` sits at the top of the extension folder: a UTF-8 JSON object of at most 256 KiB, nested at most 32 levels deep. GameScrcpy and `gs validate` check it with the same rules: errors keep the extension from installing, warnings (such as `unknown-field` for a field it does not know) only point things out. Every problem has a code, the place where it is (such as `$.contributes.panels[0].where`) and a hint to fix it; `gs validate --json` lists them all.

## An example

```json
{
  "schemaVersion": 1,
  "id": "alice.lap-timer",
  "version": "1.2.0",
  "name": "Lap timer",
  "description": "Times laps with a key and shows them in a page of the main window.",
  "authors": ["Alice"],
  "license": "MIT",
  "repository": "https://github.com/alice/lap-timer",
  "requires": { "script": "^1.1", "panel": "^1.0", "help": "^1.0" },
  "dependencies": { "alice.common": "^2.0" },
  "contributes": {
    "scripts": [
      { "name": "main", "entry": "src/main.ts", "run": "app" },
      { "name": "phone", "entry": "src/phone.ts", "run": "session" }
    ],
    "panels": [
      { "name": "laps", "title": "Laps", "icon": "timer", "where": "page", "script": "main" }
    ],
    "services": [
      {
        "name": "best",
        "description": "The best lap so far, in milliseconds, or null.",
        "output": { "type": ["integer", "null"] },
        "script": "main"
      }
    ],
    "help": [
      { "id": "laps", "title": "Timing laps", "file": "help/laps.md", "locales": { "zh_CN": "help/laps.zh_CN.md" } }
    ]
  },
  "settings": [
    { "key": "sound", "type": "boolean", "title": "Beep on each lap", "default": true }
  ],
  "permissions": [
    { "name": "mapping", "reason": "Listens to the lap key of the profile in use." }
  ],
  "l10n": {
    "zh_CN": {
      "name": "圈速计时",
      "panels.laps.title": "圈速",
      "settings.sound.title": "每圈响一声"
    }
  }
}
```

## Top-level fields

| Field | Required | Rule |
|:--|:--|:--|
| `schemaVersion` | yes | Always `1` |
| `id` | yes | Two or more parts joined by dots, each of lowercase letters, digits and hyphens (no hyphen at either end, never two in a row), at most 64 characters, such as `alice.lap-timer`. Ids starting with `gamescrcpy.` are kept for official extensions |
| `version` | yes | A semantic version `major.minor.patch`, optionally with a prerelease tag, such as `1.2.0-beta.1` |
| `name` | yes | At most 100 characters |
| `description` | | At most 1000 characters |
| `authors` | | Up to 32 names of at most 100 characters each |
| `license` | yes | The license, best as an SPDX identifier such as `MIT` or `Apache-2.0`; at most 100 characters |
| `repository` | | An address starting with `https://` |
| `authorKey` | | The author's minisign public key (56 characters starting with `RW`), used to check the package's signature; see [Packing and publishing](publishing.en.md) |
| `updates` | | `{ "index": "https://…" }`: the address of the update list; see below |
| `aiAssisted` | | `true` says the extension was made with the help of AI |
| `requires` | yes | The extension points used and the versions accepted, at least one; see below |
| `dependencies` | | The extensions it depends on; see below |
| `contributes` | | What the extension offers; see below |
| `settings` | | Up to 128 settings |
| `permissions` | | Up to 32 permissions |
| `l10n` | | Translations of its texts |

### How names are written

The manifest writes different names in different ways; the rest of this page calls them:

| Name | Written as | Used for |
|:--|:--|:--|
| Native name | A lowercase letter followed by letters and digits, at most 64 characters, such as `flashBoard` | The `name` of native contributions, the parameters of nodes and jobs, services, the parameter `key` of attachments and behaviors |
| Panel name | 1 to 32 lowercase letters, digits or hyphens, such as `lap-list` | Panels, the `id` of help articles and tours, the `name` of scripts, converters and the folders of `files.paths` |
| Profile name | 1 to 64 letters, digits, `_` or `-` | The `name` of profiles, components (at most 48) and tasks |
| Setting key | A letter followed by letters and digits, at most 64 characters | `settings[].key` |
| Package path | Relative to the extension folder, separated by `/`, never leaving the folder and without `\ : * ? " < > \|`, at most 260 characters | Every `file`, `entry`, `folder` and `dll` |

Files and folders whose names start with a dot (such as `.build` or `.git`) are neither checked nor packed.

### `requires` and version ranges

The keys of `requires` are extension points (`theme`, `filter`, `profile`, `component`, `task`, `script`, `panel`, `native`, `help`, `tour`, `locale`, and `command` and `preset`, which 2.1.0 does not offer yet); the values are version ranges, written as:

| Range | Meaning |
|:--|:--|
| `^1.0` | 1.0 and later, before 2.0 (with major version 0 only the minor version may grow) |
| `~1.2` | 1.2 and later, before 1.3 |
| `>=1.0.0 <2.0.0` | Several comparisons separated by spaces, all of which must hold |
| `1.2.3` or `=1.2.3` | Exactly this version |
| `*` | Any version |

A version with a prerelease tag matches only a range that names a prerelease of the same version.

#### How the version of an extension point grows

A version has a major and a minor part:

- When GameScrcpy adds to an extension point (new manifest fields, contributions, script interfaces, native host functions, events), the minor version grows and extensions written for the old one keep working.
- When it changes or removes something, so that an extension written for the old version no longer runs as it did, the major version grows and the program offers only the new major.
- Tightening a single field or permission (such as the fixed paths of `files.paths` in 2.1) does not grow the major version; the install and load checks warn about extensions that use it.

Write the version you built against: when you use something a minor version added, write that minor version; `^1.0` is still accepted by 1.1, so write `^1.0` when you use nothing new.

| Extension point | 2.0.0 offers | 2.1.0 offers | What 2.1.0 added |
|:--|:--|:--|:--|
| `filter` | 1.0 | 1.0 | — |
| `theme` | 1.0 | 1.0 | — |
| `profile` | 1.0 | 1.0 | — |
| `script` | 1.0 | 1.1 | New script interfaces `net`, `files`, `speech`, `events` and `media`, `ext.provide` and `ext.call`, converters, new places of `ui.*` |
| `panel` | 1.0 | 1.1 | New places `device`, `settings`, `extension`, `toolbar`, `contextMenu` and `tray`, and `on` |
| `component` | 1.0 | 1.1 | Component scripts can use the new script interfaces |
| `task` | 1.0 | 1.1 | Task scripts can use the new script interfaces |
| `native` | 1.0 | 2.0 | Interface 2: `gs_ext_main`, jobs, events, settings, device providers, speech engines, input sources, transports, media, frame taps, vision engines, AI providers, the restricted sandbox |
| `help` | — | 1.0 | New in 2.1 |
| `tour` | — | 1.0 | New in 2.1 |
| `locale` | — | 1.0 | New in 2.1 |
| `command`, `preset` | — | — | Not offered yet |

#### The three results of the check

When it loads an extension, GameScrcpy checks `requires` against the table above: an extension that does not fit is not turned on, and its card says which of the three it is. `gs validate` and `gs pack` report the same three results, each with how to fix it.

| Code | Meaning | What the card says |
|:--|:--|:--|
| `host-api-missing` | The program does not have this extension point | Needs a newer GameScrcpy |
| `host-api-version` | The version asked for is newer than the program offers | Needs a newer GameScrcpy |
| `host-api-outdated` | Every version the program offers lies above the range: the extension was made for an older GameScrcpy | Made for an older GameScrcpy |

A range with only a lower bound (such as `>=1.0`) is never `host-api-outdated`. An extension made for an older version has to write the version the program offers, such as `"native": "^2.0"`; the user asks the author for a new version or checks for updates. The first time GameScrcpy starts after an update it lists the extensions you had turned on that no longer open for one of these reasons, with "Check for updates" and "Open Extensions".

### `dependencies`

```json
"dependencies": {
  "alice.common": "^2.0",
  "bob.deck-tools": { "version": ">=1.1.0", "optional": true }
}
```

The keys are extension ids and the values version ranges, or `{ "version": range, "optional": true }`. When a required dependency is not installed, not on or of the wrong version, the extension stays off and its card says what is missing; an optional one may be absent. Dependencies decide the order of loading and also whose services an extension may call and whose events it may hear (see the [logic guide](logic.en.md)). An extension cannot depend on itself.

### `updates`

```json
"updates": { "index": "https://example.com/gamescrcpy/index.json" }
```

`index` is the address of the update list: `https://` with a host, without a user name, in printable ASCII, at most 2048 characters; anything else is `url-invalid`. GameScrcpy fetches it only when the user turned on "Check extensions for updates" in Settings › General (at most once a day, when it starts) or chooses "Check for updates" on the Extensions page, and the request does not carry the extension's id. The format of the list and how to publish it are in [Packing and publishing](publishing.en.md#update-lists).

## `contributes`

Each kind of contribution needs its extension point in `requires`, or `contribution-undeclared` is reported. Within a kind, `name` (or `id`) must be unique.

### `themes` (extension point `theme`, up to 32)

```json
"themes": [
  { "name": "Slate",
    "dark": { "bgBase": "#0E1116", "textPrimary": "#E6EAF0" },
    "light": { "bgBase": "#E6EAF1", "textPrimary": "#0F1520" } }
]
```

`name` is at most 64 characters; `dark` and `light` map design tokens to colors: a token is a lowercase letter followed by letters and digits (such as `bgBase` or `textSecondary`), a color is `#RRGGBB` or `#RRGGBBAA`. Tokens left out keep GameScrcpy's own colors. See [slate-theme](../../examples/extensions/slate-theme).

### `filters` (extension point `filter`, up to 32)

`{ "name": "Scanlines", "file": "shaders/scanlines.hlsl" }`: `file` is an `.hlsl` file (at most 1 MiB) whose `//!` directives declare its parameters, passes and textures:

| Directive | Meaning |
|:--|:--|
| `//! PARAM <name> <float\|int\|bool> <default> [<min> <max> [<step>]]` | A parameter (up to 64); a setting of the manifest with the same key sets its value |
| `//! PASS <name> [scale=<ratio>] [format=rgba8\|rgba16f]` | A pass (at least 1, up to 16), the function `float4 <name>(float2 uv)` of the shader; `scale` above 0 and at most 4 |
| `//! TEXTURE <name> <file.png>` | A texture (up to 8) |

The shader can use `Original`, `Source`, `Linear`, `Point`, `SourceSize`, `OriginalSize`, `OutputSize` and `Frame`; names starting with `Gs` are kept for GameScrcpy. `gs validate` compiles every pass. See [crt-scanlines](../../examples/extensions/crt-scanlines).

### `profiles` and `profilePatches` (extension point `profile`, up to 256 each)

- `profiles`: `{ "name": "pubg-default", "file": "profiles/pubg.json" }`, where `name` is a profile name and `file` a profile as the Profiles page exports it (the `gamescrcpy.profile` format, at most 1 MiB).
- `profilePatches`: `{ "target": "alice.pubg/pubg-default", "file": "patches/left-hand.json" }`, where `target` is "extension id/profile name" and `file` a patch (`"format": "gamescrcpy.profile-patch", "version": 1`): `meta` and `screen` are merged into the target profile, `controls` and `vars` are changed with `remove` (a list of ids), `change` (objects with an `id`) and `add` (new objects), and `gamepad` is replaced. A patch whose target is not installed has no effect. `gs validate` checks the patch's format, and for a target that is a profile of the same extension also that the controls it names exist (a warning when they do not).

### `components` and `tasks` (extension points `component` / `task`, up to 256 each)

- `components`: `{ "name": "burst", "folder": "components/burst" }`, a folder with `component.ts` (a class that extends `Component`; see [examples/components](../../examples/components)).
- `tasks`: `{ "name": "daily-check-in", "folder": "tasks/daily" }`, a folder with `task.json` (the `gamescrcpy.task` format: name and schedule), `task.ts` (a class that extends `Task`) and the pictures it uses; see [examples/tasks](../../examples/tasks).

The logic of components and tasks also runs in the low-privilege child process and is checked against the extension's permissions.

### `scripts` (extension point `script`, up to 8)

```json
"scripts": [ { "name": "main", "entry": "src/main.ts", "run": "app" } ]
```

| Field | Rule |
|:--|:--|
| `name` | Required, written as a panel name |
| `entry` | Required, a `.ts` file (not `.d.ts`) or a `.js` file in the package |
| `run` | Required: `session` runs once for each phone that is mirroring; `app` runs once while GameScrcpy runs |

### `panels` (extension point `panel`, with `script`; up to 8)

```json
"panels": [
  { "name": "laps", "title": "Laps", "icon": "timer", "where": "page", "script": "main" },
  { "name": "more", "title": "Lap timer", "where": "contextMenu", "on": ["device", "profile"], "script": "main" }
]
```

| Field | Rule |
|:--|:--|
| `name` | Required, a panel name; scripts use it in `ui.panel(name)` and the like |
| `title` | Required, at most 40 characters |
| `icon` | The name of a GameScrcpy icon (such as `timer` or `keyboard`; unknown names show a puzzle piece), or `svg:<package path>` for an SVG icon of its own |
| `where` | Required, the place; see the table below |
| `script` | Required, the `name` of the script that draws the panel |
| `on` | Only for `contextMenu`, where it is required: `device` (device cards), `profile` (profile cards) or both |

| `where` | Place | Script |
|:--|:--|:--|
| `mirror` | The side panel of the mirror window | session script |
| `menu` | A group of items in the right-click menu of the mirror picture; the panel must be named `menu`, at most one | session script |
| `status` | A widget in the status area of the mirror window | session script |
| `toolbar` | A button on the mirror window's toolbar, which may open the panel of the same name | session script |
| `page` | A page in the left column of the main window | app script |
| `settings` | A card in Settings › Extensions | app script |
| `extension` | A group in the window that the Settings button of the extension's card opens | app script |
| `device` | The settings window of a device card (for the devices of a device provider) | app script |
| `contextMenu` | A group of items in the More menu of device cards and profile cards | app script |
| `tray` | A group of items in the tray menu | app script |

A panel with the wrong kind of script (such as a page drawn by a session script) still installs; its problem shows on the extension's card. An SVG icon is read only from the extension folder, at most 32 KiB, with the root element `svg`, an empty `viewBox` or `0 0 24 24`, and only `path`, `circle`, `rect`, `line`, `polyline` and `polygon` (plus `g`, `title` and `desc`), without `transform`, DTDs or entities; GameScrcpy turns it into a path and draws it with the theme's line width and color. Each extension can have up to 64.

### `help` (extension point `help`, up to 16 articles)

```json
"help": [
  { "id": "laps", "title": "Timing laps", "file": "help/laps.md",
    "locales": { "zh_CN": "help/laps.zh_CN.md" }, "icon": "timer" }
]
```

`id` is a panel name; `title` is required, at most 60 characters; `file` is the English Markdown (`.md`, at most 1 MiB); `locales` maps languages, written like `zh_CN` or `en`, to files, up to 8; `icon` as for panels. The articles appear in the Extensions group of the help page at `help:ext/<extension id>/<id>`. How to write them: [logic guide](logic.en.md#help-articles).

### `tours` (extension point `tour`, up to 4)

```json
"tours": [
  { "id": "intro", "title": "Lap timer in two minutes", "start": "enabled",
    "steps": [
      { "title": "Welcome", "text": "This tour shows where the lap timer lives." },
      { "target": "card", "title": "Its card", "text": "Turn it on or off here." },
      { "target": "page:laps", "title": "Your laps", "text": "Every lap lands on this page." }
    ] }
]
```

| Field | Rule |
|:--|:--|
| `id` | Required, a panel name |
| `title` | Required, at most 60 characters |
| `start` | `enabled` (the default: it plays once after the extension is first turned on and loaded) or `manual` (it only plays when asked) |
| `steps` | 2 to 12 steps, each with `title` (required, at most 60 characters) and `text` (required, at most 300 characters) |
| `steps[].target` | Left out: a card in the middle of the window; `card`: the extension's card on the Extensions page; `device`: the first device the extension provides on the Devices page (it needs a device provider); `page:<panel name>`: the extension's page in the left column (a panel with `where` `page`); `settings:<panel name>`: the panel's card in Settings › Extensions (a panel with `where` `settings`) |

A step whose target is not there (no device, a panel not drawn yet) is skipped. A tour that was finished or skipped is remembered and does not play by itself again; the extension's Details has a button to replay each tour, and a help article can replay it with the link `action:tour:ext/<extension id>/<id>`.

### `services` (up to 32)

```json
"services": [
  { "name": "best", "title": "Best lap", "description": "The best lap so far, in milliseconds, or null.",
    "input": { "type": "object", "properties": { "track": { "type": "string", "maxLength": 64 } } },
    "output": { "type": ["integer", "null"] },
    "script": "main" }
]
```

| Field | Rule |
|:--|:--|
| `name` | Required, a native name; other extensions call `<extension id>/<name>` |
| `title` | At most 40 characters |
| `description` | Required, at most 500 characters |
| `input`, `output` | JSON Schemas of the arguments and the result (at most 16 KiB, nested at most 16 levels), with only `type`, `properties`, `required`, `items`, `enum`, `minimum`, `maximum`, `maxLength`, `title` and `description`; left out, nothing is checked |
| `script` | Answered by this app script (`ext.provide`); the script must have `"run": "app"` |
| `node` | Or answered by this native node, which takes one `json` parameter and returns `json` |

Give either `script` or `node`. Services need no extension point of their own, but a script needs `script` and a node needs `native`. How calls work: [logic guide](logic.en.md#services-between-extensions).

### `native` (extension point `native`)

```json
"native": {
  "dll": "bin/board.dll",
  "nodes": [ { "name": "version", "returns": "string", "description": "The firmware version." } ],
  "jobs": [ { "name": "flash", "title": "Flash firmware", "params": [ { "name": "port", "type": "string" } ],
              "returns": "json", "permissions": ["hardware"], "most": 1 } ],
  "deviceProviders": [ { "name": "boards", "title": "Boards", "kinds": ["touchBox"] } ]
}
```

`dll` is required: a `.dll` in the package. It can also name one DLL per processor, as `{ "x64": "bin/x64/board.dll", "arm64": "bin/arm64/board.dll" }` (up to 8 entries; only `x64` and `arm64` count, other names are ignored): GameScrcpy loads the one for this computer's processor and otherwise says that the extension has no version for this computer; `gs validate` checks the processor and the entry point `gs_ext_main` of each. The `name` of every entry below is a native name, `title` is at most 40 characters and `description` at most 500, and the names must match the ones the DLL lists in its contribution table (see the [native guide](native.en.md)).

| Key | Up to | Each entry |
|:--|:--|:--|
| `touchMethods` | 8 | `name`, `title` (required), `description`, `wantsDevice` (`true` lets the user pick a USB, serial or Bluetooth device for it in Settings) |
| `nodes` | 64 | `name`, `params` (up to 16 `{ name, type }`), `returns`, `description`, `permissions` (up to 10, each also in the top-level `permissions`) |
| `attachments` | 32 | `name`, `title` (required), `description`, `appliesTo` (component types such as `tap` or `wasd`, or `*`), `params` (up to 24 parameters, see below) |
| `behaviors` | 32 | `name`, `title` (required), `description`, `slots` (up to 8 `{ name, kind, title }` with `kind` `key`, `axis2` or `axis1`), `fingers` (1 to 10), `frames` (whether it is called every frame), `params` (up to 24) |
| `jobs` | 32 | `name`, `title` (required), `params` (up to 16), `returns`, `description`, `permissions` (up to 10), `most` (1 to 4 running at once, 4 by default); not named like a node |
| `deviceProviders` | 8 | `name`, `title` (required), `kinds` (required; only `touchBox` for now); needs the permission `devices.provide` |
| `speechEngines` | 4 | `name`, `title` (required), `say` (the job that reads aloud), `listen` (the job that listens), `voices` (the node that lists voices), `dictation` (whether it takes any words), `languages` (up to 32, such as `zh-CN`); at least `say` or `listen`, and the jobs and the node take the agreed parameters (see the [native guide](native.en.md#speech-engines)) |
| `inputSources` | 8 | `name` (1 to 32 lowercase letters and digits, starting with a letter), `title` (required), `slots` (1 to 64 `{ name, kind, title }`, `name` 1 to 32 letters, digits, `_` or `-`, `kind` `button`, `axis` or `stick`); needs the permission `input.provide` |
| `transports` | 8 | `name`, `title` (required), `icon`; needs the permission `transport` |
| `videoSources` | 8 | `name`, `title` (required), `kinds` (whether the picture goes with a `phone` or a `touchBox`); needs the permission `media.capture` |
| `audioSources` | 8 | `name`, `title` (required); needs the permission `media.capture` |
| `mediaSinks` | 8 | `name`, `title` (required), `video` (`{ fps: 1 to 60, width, height: 16 to 4096 }`, at most 3840 × 2160 pixels), `audio` (`true` to take the sound); at least `video` or `audio`; needs the permission `media.output` |
| `recordingExporters` | 8 | `name`, `title` (required), `extension` (required: the file extension it writes, lowercase letters and digits without the dot, not `gsrec`); needs the permission `media.output` |
| `frameTaps` | 8 | `name`, `title` (required), `fps` (1 to 60, 30 by default), `size` (the long side, 64 to 3840 pixels, 1280 by default); needs the permission `media.read` |
| `aiProviders` | 4 | `name`, `title` (required), `models` (required: the node that lists the models, without parameters, returning `json`), `chat` (required: the job that answers, with one parameter `request` (`json`), returning `json`); needs the permission `ai.provide`; see the [native guide](native.en.md#ai-providers) |
| `visionEngines` | 4 | `name`, `title` (required), `kinds` (required: one or more of `ocr`, `detect` and `classify`, each once), `languages` (the languages it reads, up to 32, such as `en` or `zh-Hans`); the DLL registers a `gs_ext_vision_engine` of the same name in its contribution table |

`sandbox` is `full` (the default) or `restricted`. A `restricted` DLL runs in an AppContainer of the extension's own, without rights of its own: the install check no longer asks for permissions that cover the network or file functions it imports (they do not work in the sandbox anyway), and the install dialog lists the system functions the sandbox blocks. What such a DLL can and cannot do is in the [native guide](native.en.md#restricted-mode).

The parameters and results of nodes and jobs use these types: `boolean`, `number`, `string`, `point` (a point of the picture, as ratios), `region` (a part of the picture, as ratios), `color` (`#RRGGBB`), `duration` (milliseconds) and `json`.

The `params` of attachments and behaviors are what the user sets in the key mapping editor, each `{ key, type, title, description, default, min, max, step, unit, log, options, shape }`: `key` is a native name, `type` one of `number`, `integer`, `toggle`, `choice`, `key`, `point`, `region`, `curve` and `color`, and `title` is required (at most 60 characters); `min`, `max`, `step`, `unit` (at most 16 characters) and `log` (a logarithmic scale, which needs `min` above 0) are for numbers only, `options` (up to 32 native names) only for `choice`, and `shape` (`circle` or `rect`) only for `region`. `default` must fit the type: a point is `[x, y]`, a region `[x, y, r]` or `[x, y, w, h]`, a curve 2 to 16 rising `[x, y]`, all within 0 to 1.

### `cli` (up to 16)

```json
"cli": [
  { "name": "flash", "description": "Writes the firmware to a board.", "job": "flash",
    "params": [ { "name": "port", "description": "The serial port, such as COM9", "default": "COM3" } ] }
]
```

`gs ext <extension> <name> --<parameter> <value>` runs the native job `job` in the mock host of `gs` (see [Testing extensions](testing.en.md#command-line-commands)). `name` is 1 to 32 lowercase letters, digits or hyphens; `description` at most 200 characters; `params` names parameters of that job only, with a `default` of the parameter's type; the job's parameters cannot be called `json`, `hosted` or `help`, which are options of `gs` itself. No extension point is needed.

### `converters` (extension point `script`, up to 16)

```json
"converters": [
  { "name": "import-keys", "title": "Key list", "from": "keys", "to": "profile",
    "script": "src/main.ts", "extensions": ["keys"] },
  { "name": "export-keys", "title": "Key list", "from": "profile", "to": "keys", "script": "src/main.ts" }
]
```

| Field | Rule |
|:--|:--|
| `name` | Required, a panel name |
| `title` | Required, at most 40 characters; shown among the file types of the import dialog and in the menu item "Export as <title>…" |
| `from`, `to` | Required: `profile`, `task`, `recording`, or a file extension without the dot (1 to 16 lowercase letters and digits); exactly one of them is a GameScrcpy format |
| `script` | Required, a `.ts` or `.js` in the package whose export named `convert` (or default export) is the function |
| `extensions` | The file extensions an import reads, up to 8, written the same way |

- A converter whose `to` is `profile` imports: Import on the Profiles page, the import in the mirror window and dropping a file on the window all accept the files of its `extensions`. A converter whose `from` is `profile`, `task` or `recording` exports: the menus of profile cards, task settings and recordings get "Export as <title>…".
- Imports only make profiles: the manifest allows converters whose `to` is `task` or `recording`, but the interface never uses them.
- A conversion runs in the logic sandbox without a phone, with at most 32 MiB going in and 32 MiB coming out, in at most 30 seconds. The function gets a `ConvertInput` (the file name, `from`, `to`, `bytes`, `text` read as UTF-8, `json()` and the cancel `signal`; a profile comes as its JSON file, a task as its `task.ts`, a recording as its `session.json`) and returns a `ConvertOutput` (`bytes`, `text` or `json`, and for an import a `name` that names the new profile), or just the text, the bytes or the profile object. What it throws is what the user sees. GameScrcpy checks an imported profile as it checks any file it imports. Errors: `GS-API-165` (the script exports no such function), `GS-API-166` (the result is nothing GameScrcpy can write), `GS-API-167` (the result is over 32 MiB). See [key-list](../../examples/extensions/key-list).

### `locales` (extension point `locale`, up to 8)

```json
"locales": [
  { "locale": "ja", "name": "日本語", "translations": "i18n/ja.qm", "help": "help/ja" }
]
```

| Field | Rule |
|:--|:--|
| `locale` | Required, a language code with an optional region, such as `ja` or `zh_TW`; `en` and `zh_CN` come with GameScrcpy and cannot be used |
| `name` | Required, at most 40 characters: the language's name in that language, listed under "Interface language" in Settings › General |
| `translations` | Required, a compiled Qt translation (`.qm`, at most 16 MiB) in the package. GameScrcpy and `gs pack` do not compile `.ts` files; a `.ts` here is `locale-invalid` |
| `help` | A folder in the package with help articles in that language (at least one `.md`) |

- Making the translation: copy `src/app/i18n/gamescrcpy_zh_CN.ts` from GameScrcpy's source repository (its source texts are English), change `language` at its top to your language, translate every entry in Qt Linguist, and compile it with Qt's `lrelease`: `lrelease ja.ts -qm ja.qm`. The translation goes over the built-in English, and missing entries show in English; the script API reference in the help page's Reference group uses it too.
- The `help` folder holds `<article>.md` files named like GameScrcpy's own articles (such as `connecting.md` or `faq.md`; the names are in `src/app/help/en/` of the source repository). In that interface language each replaces the article of the same name, and the others stay in English; they are written like the built-in help.
- A language pack is plain data and needs no permission. When the extension that provides it is turned off or removed, the language chosen in Settings stays, and the interface follows the language of Windows meanwhile.

### `commands` and `presets`

Kept in the format; 2.1.0 does not offer these extension points.

## `settings`

```json
"settings": [
  { "key": "port", "type": "text", "title": "Serial port", "default": "COM3", "maxLength": 16, "scope": "device" },
  { "key": "speed", "type": "choice", "title": "Speed", "choices": ["9600", "115200"], "default": "115200" },
  { "key": "strength", "type": "number", "title": "Strength", "min": 0, "max": 1, "step": 0.05, "default": 0.35, "live": true }
]
```

| Field | Rule |
|:--|:--|
| `key` | Required, a setting key |
| `type` | Required: `choice`, `integer`, `number`, `boolean` or `text` |
| `title` | Required, at most 100 characters |
| `description` | At most 1000 characters |
| `keywords` | Words for the settings search, at most 500 characters |
| `choices` | Required for `choice`, up to 64 |
| `min`, `max`, `step` | Only for `integer` and `number` (whole numbers for `integer`, `step` above 0) |
| `maxLength` | Only for `text`, 1 to 4096 |
| `default` | Required, of the type and in range |
| `scope` | `global` (the default) or `device`: each phone can have its own value |
| `live` | `true` when a change takes effect at once, as filter parameters do |

Settings appear in Settings › Extensions and in the settings window of the extension's card. Filter parameters read the setting of the same key; a native DLL reads them with `setting_get` and hears of changes in `settings_changed`; logic gets the `settings.changed` event (with the keys that changed).

## `permissions`

```json
"permissions": [
  { "name": "network", "reason": "Downloads the newest firmware.", "hosts": ["api.github.com", "*.githubusercontent.com"] },
  { "name": "files.paths", "reason": "Saves your notes.",
    "folders": [ { "name": "notes", "purpose": "Where your notes are kept as text files." } ] },
  { "name": "hardware", "reason": "Opens the board's serial port." }
]
```

| Field | Rule |
|:--|:--|
| `name` | Required, a permission; see the [overview](README.en.md#permissions) |
| `reason` | Required, at most 120 characters, shown next to the permission in the install dialog |
| `hosts` | Required for `network`, up to 64 host names; see below |
| `folders` | Required for `files.paths` (up to 16): `{ name, purpose }`, where `name` is a panel name that logic opens with `files.folder(name)` and `purpose` (at most 120 characters) is shown when the user chooses the folder for it the first time |
| `paths` | The old way: fixed paths give nothing and get the warning `permission-paths-fixed`; use `folders` |
| `maxDevices` | Only for `devices.other`, 1 to 64 |

Each entry of `hosts` is a host name with an optional `:port` (without a port only 443 matches); `*.example.com` covers all its subdomains (not `example.com` itself); a lone `*` covers any host and gets the warning `permission-any-host`. Names compare without regard to case; write internationalized names in their ASCII form (`xn--…`); IPv6 addresses cannot be written. `net.fetch` and `net.socket` in logic reach only these hosts and follow redirects only to them.

Only extensions that declare `script`, `component`, `task` or `native` may ask for permissions. Every permission in the `permissions` of a node or a job must be here too.

## `l10n`

```json
"l10n": {
  "zh_CN": {
    "name": "圈速计时",
    "description": "按一个键记一圈，在主窗口的一页里列出来。",
    "panels.laps.title": "圈速",
    "help.laps.title": "记录圈速",
    "settings.sound.title": "每圈响一声"
  }
}
```

The keys are languages (for `zh_CN` GameScrcpy looks in `zh_CN`, then in `zh`); the values map text keys to translations. Texts without a translation stay as the manifest writes them. The texts that can be translated:

| Text key | For |
|:--|:--|
| `name`, `description` | The extension's name and description |
| `settings.<key>.title`, `.description`, `.keywords`, `.choices.<choice>` | Settings |
| `panels.<name>.title` | Panels |
| `help.<id>.title` | Help article titles |
| `tours.<id>.title`, `tours.<id>.<step>.title`, `tours.<id>.<step>.text` | Tours (steps counted from 0) |
| `services.<name>.title`, `.description` | Services |
| `themes.<name>.name` | Themes |
| `touchMethods.<name>.title`, `.description` | Touch methods |
| `nodes.<name>.description` | System nodes |
| `attachments.<name>.title`, `.description`, `.params.<key>.title`, `.params.<key>.description`, `.params.<key>.options.<option>` | Attachments |
| `behaviors.<name>.title`, `.description`, `.params.<key>…`, `.slots.<slot>.title` | Behaviors |
| `inputSources.<name>.title`, `.slots.<slot>.title` | Input sources |
| `speechEngines.<name>.title` | Speech engines |
| `transports.<name>.title`, `videoSources.<name>.title`, `audioSources.<name>.title`, `mediaSinks.<name>.title`, `recordingExporters.<name>.title`, `frameTaps.<name>.title` | Transports, picture and sound sources, media sinks, recording exporters, frame taps |
| `aiProviders.<name>.title`, `visionEngines.<name>.title` | AI providers, recognition engines |
| `converters.<name>.title` | Converters |

The texts that scripts put into panels and menus are up to the scripts and do not go through `l10n`.

## Common results of the check

Each result of `gs validate` has a code and a hint; these are the ones met most often while writing a manifest:

| Code | Meaning |
|:--|:--|
| `missing-field`, `wrong-type`, `too-long`, `too-many`, `out-of-range`, `duplicate` | A missing field, the wrong type, too long, too many, a value that is not allowed, a name used twice |
| `unknown-field` | A field the format does not know (a warning) |
| `id-invalid`, `id-reserved` | The `id` is not written right, or starts with `gamescrcpy.` |
| `version-invalid`, `range-invalid` | A version or version range is not written right |
| `path-invalid`, `file-missing` | A package path is not written right, or the file is not in the package |
| `extension-point-unknown`, `contribution-undeclared` | An unknown extension point, or a contribution without its extension point |
| `host-api-missing`, `host-api-version`, `host-api-outdated` | The program does not have this extension point, the version asked for is newer than it offers, or the extension was made for an older program (see "The three results of the check" above) |
| `permission-unknown`, `permission-reason-missing`, `permission-not-allowed`, `permission-never-granted` | An unknown permission, no reason, a kind of extension that cannot ask for permissions, or `adb.shell` |
| `permission-hosts-missing`, `permission-host-invalid`, `permission-any-host` | `hosts` of `network` missing, not written right, or only `*` (a warning) |
| `permission-paths-missing`, `permission-paths-fixed`, `permission-field-unused` | `files.paths` without `folders`; `paths` that do nothing (a warning); a field on a permission it does not belong to (a warning) |
| `url-invalid` | `repository` or `updates.index` is not an `https://` address with a host |
| `converter-invalid`, `locale-invalid` | A converter's formats are not right; a language code is not right or built in, or the translation is not a `.qm` |
| `ai-provider-invalid`, `vision-engine-invalid` | An AI provider's node or job does not fit the convention; a recognition engine's `kinds` are not right |
| `signature-invalid`, `signature-missing`, `publisher-changed`, `signing-key-mismatch` | The signature does not match, an update lost its signature, the key changed, or the private key used for packing does not fit `authorKey` (see [Packing and publishing](publishing.en.md#signing)) |
| `native-permission-undeclared` | A node, job or native contribution needs a permission that `permissions` does not list |
| `native-name-invalid`, `native-type-invalid`, `native-dll-invalid` | A native name, type or DLL path is not written right |
| `panel-name-invalid`, `script-name-invalid`, `script-entry-invalid` | A panel name, script name or script entry is not right |
| `tour-target-invalid` | A tour step's `target` is not written right, or names a panel that is not there or a device without a device provider |
| `speech-engine-invalid` | A speech engine names a job or node that is not there, or with the wrong parameters |
| `service-target-invalid`, `service-schema-invalid` | A service's `script` / `node` is not right, or its JSON Schema uses something unsupported |
| `filter-compile-failed`, `filter-directive-invalid` | A filter does not compile, or a directive is not written right |
| `dependency-cycle` | The extension depends on itself |

GameScrcpy generates a JSON Schema of the manifest (draft 2020-12) from the same table of fields, but does not publish it as a file yet; `gs validate` is the reference.
