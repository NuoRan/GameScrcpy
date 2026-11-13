# Testing extensions

[简体中文](testing.md) · English

Most problems of an extension can be found without GameScrcpy running and without a phone. These commands come with `gs.exe`, next to the program.

| Command | What it does |
|:--|:--|
| `gs validate <folder \| package.gsext \| bundle.gsbundle> [--json]` | Checks the manifest and the files, compiles filters, reads profiles, checks the signature and each DLL's processor and entry point |
| `gs check <folder> [--json]` | Type-checks the TypeScript in the folder and applies the project rules |
| `gs types <folder>` | Writes the script API's types into `types/` for your editor |
| `gs ext test <folder \| package.gsext> [--test <file>]... [--hosted] [--json]` | Loads the manifest, the DLL and the scripts in a mock host and runs `tests/*.json` |
| `gs ext init <native \| script \| theme> <folder> [--id <extension id>]` | Writes a minimal extension with a test |
| `gs ext <extension id \| folder \| package.gsext> <command> [--<parameter> <value>]... [--hosted] [--json]` | Runs a command that the extension declares in `contributes.cli` |

## `gs validate`

```powershell
gs validate .\my-extension
gs validate .\alice.lap-timer-1.2.0.gsext --json
```

It runs the same checks as GameScrcpy's installer: every field of the manifest, the files of the package, their paths and sizes. It also compiles every filter pass (`--compile-timeout <seconds>` changes how long each shader may take, 20 seconds by default), reads every profile and patch, checks the package's signature, and checks the processor of each DLL in `contributes.native.dll` and that it exports `gs_ext_main`. Each result is one line with the severity, the code, the place and the detail, followed by a line with the fix; the last line is `<id> <version>: ok` or the number of errors and warnings. `--json` writes the same for tools. The exit code is 1 when there are errors and 2 for a wrong command line.

## `gs check` and `gs types`

`gs check <folder>` runs the TypeScript compiler that comes with GameScrcpy over every `.ts` file in the folder (except `types/`, `node_modules` and folders starting with a dot), and checks for syntax GameScrcpy cannot erase (`enum`, decorators and the like) and a few rules (loops that never `await`, a fixed `sleep` before looking for a picture). It needs a `tsconfig.json` and a `src/main.ts` (or `src/main.js`) in the folder. When everything passes, the last line is `no problems`.

`gs types <folder>` writes `types/gamescrcpy.d.ts` and `types/extensions.d.ts` (the nodes of the native extensions installed on this computer), so your editor can complete and check; run it again after GameScrcpy is updated.

## The mock host: `gs ext test`

```powershell
gs ext test .\my-extension
gs ext test .\my-extension --hosted
gs ext test .\alice.lap-timer-1.2.0.gsext --test .\more-tests\flash.json --json
```

`gs ext test` runs an extension without GameScrcpy and without a phone:

1. It checks the manifest and the files with the rules of `gs validate`, and stops on errors (exit code 2).
2. It loads each script for 2 seconds of test time with a mock phone. Calls to `ui.*` are recorded instead of drawn; a script that uses a service the mock host does not have (such as the network, files, local connections or speech) is marked `limited`, and a script that fails by itself is marked `failed`, which means the extension cannot be loaded.
3. It loads the DLL: in the process of `gs` by default, or with `--hosted` in `gs-exthost.exe`, as GameScrcpy runs them; a DLL with `"sandbox": "restricted"` in its manifest always runs in `gs-exthost.exe` inside an AppContainer of its own (made from `gs-test:<extension id>`, removed after the test), with or without `--hosted`. Its host functions record logs, notifications, events and device changes, and follow the rules of jobs, settings, hardware claims and services.
4. It runs every `.json` in the extension's `tests/` folder (in the order of their names), or the files given with `--test`. Each test starts afresh: the DLL loaded again, the default settings of the manifest, every permission the manifest asks for.

The report has a line for each test and below it the result of each step (`pass` or `FAIL`, its number, the test clock in milliseconds and its name), and ends with "N passed, M failed". The exit code is 0 when everything passed, 1 when a step failed (or there is no test at all) and 2 when the extension cannot be loaded. `--json` writes the same report.

### Test files

```json
{
  "format": "gamescrcpy.ext-test",
  "version": 1,
  "name": "flashing",
  "granted": ["hardware"],
  "settings": { "speed": 115200 },
  "deviceSettings": { "mock-phone": { "port": "COM7" } },
  "holders": [ { "resource": "serial:COM9", "by": "AOA session", "yields": true } ],
  "timeout": 20000,
  "steps": [
    { "do": "call", "node": "version", "expect": { "value": "1.0.2" } },
    { "do": "job", "job": "flash", "args": { "port": "COM9" },
      "expect": { "result": { "written": true }, "progress": [ { "text": "erasing" }, { "fraction": 1 } ] } },
    { "do": "devices", "expect": { "count": 1, "devices": [ { "state": "ready" } ] } },
    { "do": "settings", "settings": { "speed": 9600 }, "expect": { "changed": ["speed"] } },
    { "do": "wait", "ms": 1500 },
    { "do": "events", "expect": { "events": [ { "topic": "board.added" } ] } }
  ]
}
```

| Field | Meaning |
|:--|:--|
| `format`, `version` | May be left out; when given, `gamescrcpy.ext-test` and `1` |
| `name` | The name in the report, the file's name by default |
| `granted` | The permissions this test allows (they must be in the manifest); all of them when left out |
| `settings`, `deviceSettings` | The settings at the start; `deviceSettings` goes by phone, and the mock phone is `mock-phone` |
| `holders` | Somebody else holding hardware at the start: `{ resource, by, yields }`, where `yields` says it lets go when asked |
| `timeout` | How long each step waits for its results, in milliseconds (1 to 600000, 10000 by default) |
| `steps` | 1 to 1000 steps; `do` says what each one does, `name` describes it, `expect` holds what to check and `timeout` overrides the wait of that step |

A test file is UTF-8 JSON of at most 1 MiB, nested at most 64 levels deep; a larger or deeper file is not run, and the report says why.

### Steps

| `do` | Input | What `expect` can check |
|:--|:--|:--|
| `call` | `node`, `args` (an array) | `value`, `error` |
| `job` | `job`, `args` (an object), `id`, `wait` (`true` by default; `false` only starts it) | `result`, `error`, `progress`, `logs`, `outputs` |
| `job.cancel` | `id` | `ended` |
| `job.wait` | `id` | `result`, `error`, `progress`, `logs`, `outputs` |
| `touch.open` | `method`, `session`, `phone`, `device`, `touchBox`, `options` | `opened` |
| `touch.frame` | `session`, `contacts` (`{ id, action, x, y, … }` with `action` `down`, `move`, `up` or `cancel`), `rotation` | `ok`, `sent` |
| `touch.key` | `session`, `key` or `keycode`, `down`, `repeat`, `meta` | `ok` |
| `touch.text` | `session`, `text` | `ok` |
| `touch.mouse` | `session`, `buttons`, `dx`, `dy`, `wheel` | `ok` |
| `touch.pad` | `session`, `buttons`, `hat`, `leftX`, `leftY`, `rightX`, `rightY`, `leftTrigger`, `rightTrigger` | `ok` |
| `touch.close` | `session` | — |
| `devices` | `provider` | `devices`, `count`, `error`, `problems` |
| `device.act` | `provider`, `device`, `action` | `ok`, `error` |
| `events` | — | Shows the events since the last `events` step |
| `settings` | `settings`, `deviceSettings` | `changed` (the keys `settings_changed` got) |
| `wait` | `ms` | Moves the test clock `ms` milliseconds ahead (an in-process DLL sees it in `now_us` too) |
| `holder` | `resource`, `by`, `yields` | Has somebody else hold a piece of hardware |
| `holder.release` | `resource` | `released` |
| `claim.want` | `resource`, `by` | `held`, `yielded`, `released`: how the extension answers when somebody wants hardware it holds |
| `claims` | — | `held`, `holders` |
| `scripts` | — | `loaded` (the scripts that must have loaded), `logs` (pieces of log lines that must appear in order) |
| `manifest` | — | `value` (the manifest as read) |

Every step can also check four kinds of happenings, each matched in order within the step's wait: `events` (events the DLL sent, `{ topic, value }`), `logged` (log lines, `{ level, text }` or a piece of text), `notified` (notifications, `{ title, text }` or a piece of text) and `devicesChanged` (the providers named in `devices_changed`).

How values are compared: objects only by the keys that `expect` writes; arrays by length and element by element; numbers with a tiny tolerance; `error` as `true`, `false` or a piece of the error's text; lists such as `progress`, `logs` and `outputs` (the parts of the result a job hands over with `job_output`) in order, where a text matches when it contains the piece.

When you test a restricted DLL with the `gs.exe` of a GameScrcpy build made from source, the built `gs-exthost.exe` belongs to Administrators, and without elevation the container cannot be given the right to read and run it (error 5): copy `gs.exe`, `gs-exthost.exe` and the DLLs next to them into a folder of your own and run them from there. An installed GameScrcpy (under Program Files) does not have this problem.

### Testing script extensions

The mock host draws nothing and has no phone, so for a script extension it mainly checks that the scripts load and what they log:

```json
{ "steps": [ { "do": "scripts", "expect": { "loaded": ["main"], "logs": ["ready"] } } ] }
```

Whether panels look right and how the extension works with the program is for GameScrcpy itself (see below).

## Starting points: `gs ext init`

```powershell
gs ext init script .\lap-timer --id alice.lap-timer
gs ext init native .\board-tools
gs ext init theme .\night-sky
```

- `script`: `src/main.ts` (an app script that draws a page of the main window), `tsconfig.json`, `types/gamescrcpy.d.ts` and `tests/basic.json`; it passes `gs check`, `gs validate` and `gs ext test` as it is.
- `native`: a CMake project, a copy of `include/gamescrcpy_ext.h`, `src/native.cpp` (a node `greet` and a job `countdown`), a `contributes.cli` command in the manifest and `tests/basic.json`. Build it with Visual Studio 2022: `cmake -S . -B .build`, then `cmake --build .build --config Release`; the DLL lands in the folder.
- `theme`: a theme with colors for the dark and the light look, and its test.

Each comes with an `AGENTS.md` that says how to check, test and pack it, for you and for AI coding assistants. Without `--id` the id is `my.<folder name>`; ids starting with `gamescrcpy.` are refused. The folder must be empty.

## Command-line commands

`contributes.cli` in the manifest turns native jobs into commands (see the [manifest reference](manifest.en.md#cli-up-to-16)):

```powershell
gs ext .\board-tools --help
gs ext .\board-tools countdown --from 5
gs ext alice.board flash --port COM9 --json
```

- The first argument is a folder, a `.gsext`, or the id of an extension installed on this computer (its newest version).
- Parameters are written `--name value` or `--name=value` and read by the job parameter's type: `true` / `false`, numbers, a point as `x,y`, a region as `x,y,width,height`, `json` as JSON; missing ones take the manifest's `default`, and a required one that is missing is pointed out.
- Progress prints line by line (`[ 40%] text`), logs print indented, and the result prints as JSON at the end; `--json` writes a report instead. Ctrl+C cancels the job. `--hosted` runs the DLL in `gs-exthost.exe`.
- The exit code is 0 when the job worked, 1 when it failed and 2 when the extension cannot be loaded or the command line is wrong.

## Trying it in GameScrcpy

1. Pack it with `gs pack <folder>` (see [Packing and publishing](publishing.en.md)), install it with "Install from file…" on the Extensions page, turn it on and allow its permissions.
2. After a change, pack and install again: the same version number is replaced too. Installed extensions live in `%APPDATA%\GameScrcpy\extensions\<extension id>\<version>\` (`data\extensions` for the portable build).
3. Look for `[extensions]` and `logic …` lines in the program log (`%APPDATA%\GameScrcpy\logs`); Details on the extension's card lists manifest problems, panel problems and its health.
4. The mirror window works without a phone: `GameScrcpy.exe --demo-device tests\data\video\emulator_h265.media` adds a demo device that loops a recording (the file is in the source repository).
