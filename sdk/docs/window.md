# window

The phone's mirror window.

## window.focused()

Whether the phone's mirror window is the active window, the one that gets the keyboard; false while another window is in front or the mirror window is closed.

```ts
window.focused(): boolean
```

Returns `boolean`. stable since API 1.

```ts
if (!window.focused()) log.info("the mirror window is in the background");
```

## window.onFocus(handler, options?)

Starts a task each time the phone's mirror window becomes the active window.

```ts
window.onFocus(handler: (event: TaskContext) => void | Promise<void>, options?: ListenOptions): Subscription
```

| Parameter | Type | Description |
|---|---|---|
| `handler` | `(event: TaskContext) => void \| Promise<void>` | What to run when the phone's mirror window becomes the active window. |
| `options` (optional) | `ListenOptions` | What happens when it comes again while the last task is running. |

Returns `Subscription`. stable since API 1.

Errors:

- `GS-API-008` when the handler is not a function. Pass a function.
- `GS-API-009` when again is not one of ignore, restart, stop, queue or parallel. Use one of the five rules.

```ts
window.onFocus(() => hints.show());
```

## window.onBlur(handler, options?)

Starts a task each time the phone's mirror window stops being the active window, such as when another window comes to the front.

```ts
window.onBlur(handler: (event: TaskContext) => void | Promise<void>, options?: ListenOptions): Subscription
```

| Parameter | Type | Description |
|---|---|---|
| `handler` | `(event: TaskContext) => void \| Promise<void>` | What to run when the phone's mirror window stops being the active window. |
| `options` (optional) | `ListenOptions` | What happens when it comes again while the last task is running. |

Returns `Subscription`. stable since API 1.

Errors:

- `GS-API-008` when the handler is not a function. Pass a function.
- `GS-API-009` when again is not one of ignore, restart, stop, queue or parallel. Use one of the five rules.

```ts
window.onBlur(() => touch.releaseAll());
```
