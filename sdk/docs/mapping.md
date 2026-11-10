# mapping

The controls of the profile in use, by id; the script's output never feeds back into its input.

## mapping.controls()

The controls of the profile in use, with their ids and whether they are on.

```ts
mapping.controls(): ControlInfo[]
```

Returns `ControlInfo[]`. stable since API 1.

Errors:

- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
const ids = mapping.controls().map((control) => control.id);
```

## mapping.get(id)

The component with this id, or null.

```ts
mapping.get(id: string): Component | null
```

| Parameter | Type | Description |
|---|---|---|
| `id` | `string` | The control id from the profile in use. |

Returns `Component | null`. stable since API 1.

```ts
mapping.get("fire")?.trigger();
```

## mapping.all(type?)

All components of the profile, or those of one type, of one class or with one interface.

```ts
mapping.all(type?: ComponentType | Interface<unknown> | { readonly prototype: Component } | { readonly component?: Component }): Component[]
```

| Parameter | Type | Description |
|---|---|---|
| `type` (optional) | `ComponentType \| Interface<unknown> \| { readonly prototype: Component } \| { readonly component?: Component }` | Only components of this type, of this class or with this interface. |

Returns `Component[]`. stable since API 1.

```ts
for (const stick of mapping.all("wasd")) stick.reset();
```

## mapping.trigger(id)

Presses and releases a control as if its key were tapped; the script's own output never triggers scripts.

```ts
mapping.trigger(id: string): Action<void>
```

| Parameter | Type | Description |
|---|---|---|
| `id` | `string` | The control id from the profile in use. |

Returns `Action<void>`. stable since API 1.

Errors:

- `GS-API-005` when there is no control with this id in the profile in use. Use an id from mapping.controls().
- `GS-API-030` when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
await mapping.trigger("jump");
```

## mapping.hold(id, durationMs)

Holds a control down for a while, then releases it; released early if the task is cancelled.

```ts
mapping.hold(id: string, durationMs: number): Action<void>
```

| Parameter | Type | Description |
|---|---|---|
| `id` | `string` | The control id from the profile in use. |
| `durationMs` | `number` | How long to hold it down, in milliseconds. |

Returns `Action<void>`. stable since API 1.

Errors:

- `GS-API-005` when there is no control with this id in the profile in use. Use an id from mapping.controls().
- `GS-API-003` when the duration is missing, not more than 0 or more than 60000. Pass a duration in milliseconds.
- `GS-API-030` when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
await mapping.hold("sprint", 1500);
```

## mapping.setEnabled(id, enabled)

Turns a control on or off until the script ends or the profile changes; this ends a binding of the control's enabled.

```ts
mapping.setEnabled(id: string, enabled: boolean): void
```

| Parameter | Type | Description |
|---|---|---|
| `id` | `string` | The control id from the profile in use. |
| `enabled` | `boolean` | Whether the control reacts to its keys. |

Returns `void`. stable since API 1.

Errors:

- `GS-API-005` when there is no control with this id in the profile in use. Use an id from mapping.controls().
- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
mapping.setEnabled("fire", false);
```

## mapping.on()

Turns key mapping on for this phone.

```ts
mapping.on(): boolean
```

Returns `boolean`. stable since API 1.

```ts
mapping.on();
```

## mapping.off()

Turns key mapping off for this phone, so keys go to the phone as they are.

```ts
mapping.off(): boolean
```

Returns `boolean`. stable since API 1.

```ts
mapping.off();
```

## mapping.toggle()

Turns key mapping on or off for this phone.

```ts
mapping.toggle(): boolean
```

Returns `boolean`. stable since API 1.

```ts
mapping.toggle();
```

## mapping.isOn()

Whether key mapping is on for this phone.

```ts
mapping.isOn(): boolean
```

Returns `boolean`. stable since API 1.

```ts
if (!mapping.isOn()) mapping.on();
```
