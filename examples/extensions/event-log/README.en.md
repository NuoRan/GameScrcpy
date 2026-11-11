# Event log (event-log)

[简体中文](README.md) · English

Subscribes to every topic of GameScrcpy's own events with `events.on` and lists them by time on a page of the main window: phones connecting and disconnecting, mirroring starting and ending, profiles applied, the app in front, the screen orientation, recordings, window focus, extensions turned on and off, and settings changed.

## Files

| File | Contents |
|:--|:--|
| `manifest.json` | An app script, a page of the main window, and the permission `events.app` |
| `src/main.ts` | Subscribing, turning events into table rows, and pausing, filtering and clearing on the page |
| `tsconfig.json` | For `gs check` and your editor |

## Try it

1. `gs validate examples\extensions\event-log`, `gs check examples\extensions\event-log`.
2. `gs pack examples\extensions\event-log -o event-log.gsext`, then "Install from file…" on the Extensions page; turn it on and allow its permission.
3. Open "Event log" in the left column, then connect a phone, start mirroring, switch profiles, change the app on the phone, and turn an extension off and on: each of these appears in the table, the newest on top.

## Who hears which events

- The logic of an extension always hears `extension.enabled`, `extension.disabled` and `settings.changed` (only for its own settings and the language and look settings).
- The events of phones and windows (`device.*`, `session.*`, `profile.applied`, `app.foreground`, `orientation.changed`, `recording.*`, `window.focused`): a session script hears those of its own phone; app scripts, tasks and other phones need the permission `events.app`. This example uses an app script, so it asks for the permission; without it, the table only shows the events of extensions and settings.
- Each topic delivers its first 100 events in a second at once and merges the rest of that second into its last one.

The value of each event is described in [sdk/docs/events.md](../../../sdk/docs/events.md). The Phone column shows the value's `name` (or `phone`, the serial, when there is none); the other fields go into Details.
