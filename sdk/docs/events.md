# events

Events that the native DLL of an extension sends to the extension's own logic and panels.

## events.on(topic, handler, options?)

Starts a task each time the native DLL of this extension sends an event on the topic. Only the extension's own logic and panels hear its events; for each topic the DLL passes on the first 100 in a second and merges the rest of that second into the last one.

```ts
events.on(topic: string, handler: (event: TaskContext & { readonly topic: string; readonly value: StoreValue }) => void | Promise<void>, options?: ListenOptions): Subscription
```

| Parameter | Type | Description |
|---|---|---|
| `topic` | `string` | The topic the extension's DLL sends: 1 to 64 lowercase letters, digits, dots and dashes. |
| `handler` | `(event: TaskContext & { readonly topic: string; readonly value: StoreValue }) => void \| Promise<void>` | What to run for each event; value is what the DLL sent with it. |
| `options` (optional) | `ListenOptions` | What happens when another event comes while the last task is running. |

Returns `Subscription`. stable since API 1.

Errors:

- `GS-API-068` when the topic is not 1 to 64 lowercase letters, digits, dots and dashes. Use the topic that the extension's DLL sends.
- `GS-API-008` when the handler is not a function. Pass a function.
- `GS-API-009` when again is not one of ignore, restart, stop, queue or parallel. Use one of the five rules.

```ts
events.on("board.added", ({ value }) => log.info("a board came", value));
```
