# log

Lines in the script console, with the task that wrote them.

## log.info(values)

Writes an information line to the script console.

```ts
log.info(...values: unknown[]): void
```

| Parameter | Type | Description |
|---|---|---|
| `values` | `...unknown[]` | Values to write, joined with spaces. |

Returns `void`. stable since API 1.

```ts
log.info("started", 3);
```

## log.warn(values)

Writes a warning line to the script console.

```ts
log.warn(...values: unknown[]): void
```

| Parameter | Type | Description |
|---|---|---|
| `values` | `...unknown[]` | Values to write, joined with spaces. |

Returns `void`. stable since API 1.

```ts
log.warn("slow");
```

## log.error(values)

Writes an error line to the script console.

```ts
log.error(...values: unknown[]): void
```

| Parameter | Type | Description |
|---|---|---|
| `values` | `...unknown[]` | Values to write, joined with spaces. |

Returns `void`. stable since API 1.

```ts
log.error("missing");
```

## log.time(name)

Starts a timer with this name; log.timeEnd() with the same name writes how long it has been running.

```ts
log.time(name: string): void
```

| Parameter | Type | Description |
|---|---|---|
| `name` | `string` | The timer's name. |

Returns `void`. stable since API 1.

Errors:

- `GS-API-011` when the name is not a string or is empty. Pass a name.

```ts
log.time("search");
```

## log.timeEnd(name)

Writes an information line with the milliseconds since log.time() with this name and gives that number; with no such timer it writes a warning line and gives null.

```ts
log.timeEnd(name: string): number | null
```

| Parameter | Type | Description |
|---|---|---|
| `name` | `string` | The timer's name. |

Returns `number | null`. stable since API 1.

Errors:

- `GS-API-011` when the name is not a string or is empty. Pass a name.

```ts
log.time("search"); await vision.find("res/chest.png"); log.timeEnd("search");
```
