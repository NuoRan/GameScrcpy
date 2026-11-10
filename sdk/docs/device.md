# device

The phone this script runs on.

## device.info()

The phone this script runs on: model, Android version, screen size and rotation.

```ts
device.info(): DeviceInfo
```

Returns `DeviceInfo`. stable since API 1.

Errors:

- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
const { width, height } = device.info();
```

## device.back()

Presses the phone's Back key.

```ts
device.back(): Action<void>
```

Returns `Action<void>`. stable since API 1.

Errors:

- `GS-API-030` when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
await device.back();
```

## device.home()

Presses the phone's Home key.

```ts
device.home(): Action<void>
```

Returns `Action<void>`. stable since API 1.

Errors:

- `GS-API-030` when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
await device.home();
```

## device.recents()

Opens the list of recent apps.

```ts
device.recents(): Action<void>
```

Returns `Action<void>`. stable since API 1.

Errors:

- `GS-API-030` when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
await device.recents();
```

## device.menu()

Presses the phone's Menu key.

```ts
device.menu(): Action<void>
```

Returns `Action<void>`. stable since API 1.

Errors:

- `GS-API-030` when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
await device.menu();
```

## device.power()

Presses the phone's Power key.

```ts
device.power(): Action<void>
```

Returns `Action<void>`. stable since API 1.

Errors:

- `GS-API-030` when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
await device.power();
```

## device.volumeUp()

Turns the phone's volume up one step.

```ts
device.volumeUp(): Action<void>
```

Returns `Action<void>`. stable since API 1.

Errors:

- `GS-API-030` when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
await device.volumeUp();
```

## device.volumeDown()

Turns the phone's volume down one step.

```ts
device.volumeDown(): Action<void>
```

Returns `Action<void>`. stable since API 1.

Errors:

- `GS-API-030` when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
await device.volumeDown();
```

## device.mute()

Mutes or unmutes the phone.

```ts
device.mute(): Action<void>
```

Returns `Action<void>`. stable since API 1.

Errors:

- `GS-API-030` when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
await device.mute();
```

## device.notifications()

Pulls down the phone's notification shade, as the More menu of the mirror window's toolbar does; true once the phone is asked, false when it cannot be asked now, such as while GameScrcpy reconnects to it. When the phone does not allow it, its mirror window says so. The shade stays down when the logic stops.

```ts
device.notifications(): boolean
```

Returns `boolean`. stable since API 1.

Errors:

- `GS-API-090` when the logic does not run on a phone. Start the script on a phone that is mirroring.
- `GS-API-091` when the phone is connected without the GameScrcpy server, such as in a window without a picture or through a capture card. Mirror the phone over USB or WiFi to reach its notification shade.

```ts
if (device.notifications()) await sleep(800);
```

## device.quickSettings()

Opens the phone's quick settings, as the More menu of the mirror window's toolbar does; true once the phone is asked, false when it cannot be asked now. They stay open when the logic stops.

```ts
device.quickSettings(): boolean
```

Returns `boolean`. stable since API 1.

Errors:

- `GS-API-090` when the logic does not run on a phone. Start the script on a phone that is mirroring.
- `GS-API-091` when the phone is connected without the GameScrcpy server, such as in a window without a picture or through a capture card. Mirror the phone over USB or WiFi to reach its notification shade.

```ts
device.quickSettings();
```

## device.collapsePanels()

Closes the phone's notification shade and quick settings, as the More menu of the mirror window's toolbar does; true once the phone is asked, false when it cannot be asked now.

```ts
device.collapsePanels(): boolean
```

Returns `boolean`. stable since API 1.

Errors:

- `GS-API-090` when the logic does not run on a phone. Start the script on a phone that is mirroring.
- `GS-API-091` when the phone is connected without the GameScrcpy server, such as in a window without a picture or through a capture card. Mirror the phone over USB or WiFi to reach its notification shade.

```ts
device.collapsePanels();
```

## device.pressKey(key)

Presses and releases an Android key on the phone.

```ts
device.pressKey(key: AndroidKey): Action<void>
```

| Parameter | Type | Description |
|---|---|---|
| `key` | `AndroidKey` | The Android key to press. |

Returns `Action<void>`. stable since API 1.

Errors:

- `GS-API-004` when the key name is not one of AndroidKey. Use a name from the AndroidKey type, such as "back".
- `GS-API-030` when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
await device.pressKey("back");
```

## device.type(text)

Types text into the box that has the focus on the phone.

```ts
device.type(text: string): void
```

| Parameter | Type | Description |
|---|---|---|
| `text` | `string` | The text to type into the focused box on the phone. |

Returns `void`. stable since API 1.

Errors:

- `GS-API-011` when the text is not a string or is longer than 65536 bytes. Pass shorter text.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
device.type("gg");
```

## device.launch(packageName, options?)

Starts an app on the phone; false when the phone cannot be asked now.

```ts
device.launch(packageName: string, options?: LaunchOptions): boolean
```

| Parameter | Type | Description |
|---|---|---|
| `packageName` | `string` | The app's package name, such as "com.tencent.tmgp.pubgmhd". |
| `options` (optional) | `LaunchOptions` | Whether to stop the app first. |

Returns `boolean`. stable since API 1.

Errors:

- `GS-API-011` when the name is not a string or is empty. Pass a name.

```ts
device.launch("com.android.settings");
```

## device.screenOff()

Turns the phone's screen off while mirroring goes on.

```ts
device.screenOff(): boolean
```

Returns `boolean`. stable since API 1.

```ts
device.screenOff();
```

## device.screenOn()

Turns the phone's screen back on.

```ts
device.screenOn(): boolean
```

Returns `boolean`. stable since API 1.

```ts
device.screenOn();
```

## device.foregroundApp()

The app in front on the phone, when the phone has said which.

```ts
device.foregroundApp(): ForegroundApp | null
```

Returns `ForegroundApp | null`. stable since API 1.

```ts
const app = device.foregroundApp();
```

## device.onForegroundApp(handler, options?)

Starts a task each time another app comes to the front.

```ts
device.onForegroundApp(handler: (event: TaskContext & ForegroundApp) => void | Promise<void>, options?: ListenOptions): Subscription
```

| Parameter | Type | Description |
|---|---|---|
| `handler` | `(event: TaskContext & ForegroundApp) => void \| Promise<void>` | What to run when another app comes to the front. |
| `options` (optional) | `ListenOptions` | What happens when it comes again while the last task is running. |

Returns `Subscription`. stable since API 1.

Errors:

- `GS-API-008` when the handler is not a function. Pass a function.
- `GS-API-009` when again is not one of ignore, restart, stop, queue or parallel. Use one of the five rules.

```ts
device.onForegroundApp(({ game }) => { if (game) mapping.on(); });
```

## device.onRotate(handler, options?)

Starts a task each time the phone's screen turns.

```ts
device.onRotate(handler: (event: TaskContext & DeviceInfo) => void | Promise<void>, options?: ListenOptions): Subscription
```

| Parameter | Type | Description |
|---|---|---|
| `handler` | `(event: TaskContext & DeviceInfo) => void \| Promise<void>` | What to run when the screen turns; the event has the new size and rotation. |
| `options` (optional) | `ListenOptions` | What happens when it turns again while the last task is running. |

Returns `Subscription`. stable since API 1.

Errors:

- `GS-API-008` when the handler is not a function. Pass a function.
- `GS-API-009` when again is not one of ignore, restart, stop, queue or parallel. Use one of the five rules.

```ts
device.onRotate(({ rotation }) => { if (rotation % 2 === 1) mapping.on(); });
```

## device.connected()

Whether the phone is connected now; false while GameScrcpy is reconnecting to it. Taps, swipes, key presses and other actions made while it is not connected wait and happen once it is back; the logic and tasks go on, and stop only when reconnecting is given up.

```ts
device.connected(): boolean
```

Returns `boolean`. stable since API 1.

```ts
if (!device.connected()) log.warn("waiting for the phone");
```

## device.onDisconnect(handler, options?)

Starts a task each time the connection to the phone drops and reconnecting begins.

```ts
device.onDisconnect(handler: (event: TaskContext) => void | Promise<void>, options?: ListenOptions): Subscription
```

| Parameter | Type | Description |
|---|---|---|
| `handler` | `(event: TaskContext) => void \| Promise<void>` | What to run when the connection to the phone drops and GameScrcpy starts reconnecting. |
| `options` (optional) | `ListenOptions` | What happens when it drops again while the last task is running. |

Returns `Subscription`. stable since API 1.

Errors:

- `GS-API-008` when the handler is not a function. Pass a function.
- `GS-API-009` when again is not one of ignore, restart, stop, queue or parallel. Use one of the five rules.

```ts
device.onDisconnect(() => log.warn("the phone dropped"));
```

## device.onReconnect(handler, options?)

Starts a task each time the phone is connected again after it dropped.

```ts
device.onReconnect(handler: (event: TaskContext) => void | Promise<void>, options?: ListenOptions): Subscription
```

| Parameter | Type | Description |
|---|---|---|
| `handler` | `(event: TaskContext) => void \| Promise<void>` | What to run when the phone is connected again after it dropped. |
| `options` (optional) | `ListenOptions` | What happens when it comes back again while the last task is running. |

Returns `Subscription`. stable since API 1.

Errors:

- `GS-API-008` when the handler is not a function. Pass a function.
- `GS-API-009` when again is not one of ignore, restart, stop, queue or parallel. Use one of the five rules.

```ts
device.onReconnect(async () => { await device.home(); });
```

## device.onInputBox(handler, options?)

Starts a task each time an input box opens, changes or closes on the phone. It needs the GameScrcpy Companion input method on the phone; without it, and in replays, it never fires.

```ts
device.onInputBox(handler: (event: TaskContext & InputBox) => void | Promise<void>, options?: ListenOptions): Subscription
```

| Parameter | Type | Description |
|---|---|---|
| `handler` | `(event: TaskContext & InputBox) => void \| Promise<void>` | What to run when an input box opens or closes on the phone; the event says which and what kind of box. |
| `options` (optional) | `ListenOptions` | What happens when it changes again while the last task is running. |

Returns `Subscription`. stable since API 1.

Errors:

- `GS-API-008` when the handler is not a function. Pass a function.
- `GS-API-009` when again is not one of ignore, restart, stop, queue or parallel. Use one of the five rules.

```ts
device.onInputBox(({ shown, action }) => { if (shown && action === "send") log.info("chat box"); });
```

## device.rotate(orientation)

Keeps the phone's screen upright or sideways, or lets it follow the phone again, like the screen direction in the mirror toolbar; false when the phone cannot be asked now.

```ts
device.rotate(orientation: "auto" | "portrait" | "landscape"): boolean
```

| Parameter | Type | Description |
|---|---|---|
| `orientation` | `"auto" \| "portrait" \| "landscape"` | Keep the screen upright or sideways, or let it follow the phone again. |

Returns `boolean`. stable since API 1.

Errors:

- `GS-API-017` when the orientation is not "auto", "portrait" or "landscape". Write one of the three.

```ts
device.rotate("landscape");
```
