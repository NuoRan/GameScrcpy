# events

Events of GameScrcpy itself, such as a phone connecting, and events that the native DLL of an extension sends to the extension's own logic and panels.

## events.on(topic, handler, options?)

Starts a task each time an event of the topic comes. GameScrcpy's own topics and their values: device.connected and device.disconnected { phone, name }, session.started { phone, name }, session.ended { phone, name, failed }, profile.applied { phone, name, profile, profileName }, app.foreground { phone, name, package, game }, orientation.changed { phone, name, rotation } in quarter turns, recording.started { phone, name, path }, recording.ended { phone, name, path, durationMs }, window.focused { phone, name, window: "mirror" or "grid", focused }, extension.enabled and extension.disabled { id, name, version }, and settings.changed { keys, values, extension, device }; phone is the phone's serial and name the name it shows, and values holds the new language and look settings among keys. Your own logic hears all of them. The logic of an extension hears extension.enabled, extension.disabled and settings.changed, which then tells only of its own settings and the language and look settings, and a session script also hears its own phone; the other phones, and app and task scripts, need the permission events.app. Any other topic is one the native DLL of this extension sends, which only the extension's own logic and panels hear. Of each topic a listener gets the first 100 events in a second at once and the rest of that second merged into the last one.

```ts
events.on(topic: string, handler: (event: TaskContext & { readonly topic: string; readonly value: StoreValue }) => void | Promise<void>, options?: ListenOptions): Subscription
```

| Parameter | Type | Description |
|---|---|---|
| `topic` | `string` | One of GameScrcpy's own topics, such as "device.connected", or a topic the extension's DLL sends: 1 to 64 lowercase letters, digits, dots and dashes. |
| `handler` | `(event: TaskContext & { readonly topic: string; readonly value: StoreValue }) => void \| Promise<void>` | What to run for each event; value is what came with it. |
| `options` (optional) | `ListenOptions` | What happens when another event comes while the last task is running. |

Returns `Subscription`. stable since API 1.

Errors:

- `GS-API-068` when the topic is not 1 to 64 lowercase letters, digits, dots and dashes. Use one of GameScrcpy's own topics or the topic that the extension's DLL sends.
- `GS-API-008` when the handler is not a function. Pass a function.
- `GS-API-009` when again is not one of ignore, restart, stop, queue or parallel. Use one of the five rules.

```ts
events.on("app.foreground", ({ value }) => log.info("another app is in front", value));
```
