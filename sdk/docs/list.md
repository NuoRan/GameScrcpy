# list

Lists of values, changed in place.

## list.add(items, value)

Adds a value to the end of the list.

```ts
list.add(items: unknown[], value: unknown): void
```

| Parameter | Type | Description |
|---|---|---|
| `items` | `unknown[]` | The list. |
| `value` | `unknown` | The value. |

Returns `void`. stable since API 1.

Errors:

- `GS-API-011` when the list is not a list. Pass a list, such as a variable of the list kind.
- `GS-API-017` when the position is not a whole number in the list. Pass a position from 0 up to the length of the list.

```ts
list.add(names, "bo");
```

## list.insert(items, index, value)

Puts a value into the list at a position; the rest move up.

```ts
list.insert(items: unknown[], index: number, value: unknown): void
```

| Parameter | Type | Description |
|---|---|---|
| `items` | `unknown[]` | The list. |
| `index` | `number` | The position, counted from 0. |
| `value` | `unknown` | The value. |

Returns `void`. stable since API 1.

Errors:

- `GS-API-011` when the list is not a list. Pass a list, such as a variable of the list kind.
- `GS-API-017` when the position is not a whole number in the list. Pass a position from 0 up to the length of the list.

```ts
list.insert(names, 0, "bo");
```

## list.removeAt(items, index)

Takes the value at a position out of the list and gives it back.

```ts
list.removeAt(items: unknown[], index: number): unknown
```

| Parameter | Type | Description |
|---|---|---|
| `items` | `unknown[]` | The list. |
| `index` | `number` | The position, counted from 0. |

Returns `unknown`. stable since API 1.

Errors:

- `GS-API-011` when the list is not a list. Pass a list, such as a variable of the list kind.
- `GS-API-017` when the position is not a whole number in the list. Pass a position from 0 up to the length of the list.

```ts
const first = list.removeAt(names, 0);
```

## list.remove(items, value)

Takes the first value equal to it out of the list; says whether it found one.

```ts
list.remove(items: unknown[], value: unknown): boolean
```

| Parameter | Type | Description |
|---|---|---|
| `items` | `unknown[]` | The list. |
| `value` | `unknown` | The value. |

Returns `boolean`. stable since API 1.

Errors:

- `GS-API-011` when the list is not a list. Pass a list, such as a variable of the list kind.
- `GS-API-017` when the position is not a whole number in the list. Pass a position from 0 up to the length of the list.

```ts
list.remove(names, "bo");
```

## list.indexOf(items, value)

The position of the first value equal to it, or -1 when it is not there.

```ts
list.indexOf(items: unknown[], value: unknown): number
```

| Parameter | Type | Description |
|---|---|---|
| `items` | `unknown[]` | The list. |
| `value` | `unknown` | The value. |

Returns `number`. stable since API 1.

Errors:

- `GS-API-011` when the list is not a list. Pass a list, such as a variable of the list kind.
- `GS-API-017` when the position is not a whole number in the list. Pass a position from 0 up to the length of the list.

```ts
if (list.indexOf(names, "bo") < 0) await ui.toast("missing");
```

## list.at(items, index)

The value at a position, or undefined when the list is shorter.

```ts
list.at(items: unknown[], index: number): unknown
```

| Parameter | Type | Description |
|---|---|---|
| `items` | `unknown[]` | The list. |
| `index` | `number` | The position, counted from 0. |

Returns `unknown`. stable since API 1.

Errors:

- `GS-API-011` when the list is not a list. Pass a list, such as a variable of the list kind.
- `GS-API-017` when the position is not a whole number in the list. Pass a position from 0 up to the length of the list.

```ts
const first = list.at(names, 0);
```

## list.setAt(items, index, value)

Replaces the value at a position.

```ts
list.setAt(items: unknown[], index: number, value: unknown): void
```

| Parameter | Type | Description |
|---|---|---|
| `items` | `unknown[]` | The list. |
| `index` | `number` | The position, counted from 0. |
| `value` | `unknown` | The value. |

Returns `void`. stable since API 1.

Errors:

- `GS-API-011` when the list is not a list. Pass a list, such as a variable of the list kind.
- `GS-API-017` when the position is not a whole number in the list. Pass a position from 0 up to the length of the list.

```ts
list.setAt(names, 0, "bo");
```

## list.size(items)

How many values the list has.

```ts
list.size(items: unknown[]): number
```

| Parameter | Type | Description |
|---|---|---|
| `items` | `unknown[]` | The list. |

Returns `number`. stable since API 1.

Errors:

- `GS-API-011` when the list is not a list. Pass a list, such as a variable of the list kind.
- `GS-API-017` when the position is not a whole number in the list. Pass a position from 0 up to the length of the list.

```ts
if (list.size(names) === 0) await ui.toast("empty");
```

## list.has(items, value)

Whether the list has a value equal to it.

```ts
list.has(items: unknown[], value: unknown): boolean
```

| Parameter | Type | Description |
|---|---|---|
| `items` | `unknown[]` | The list. |
| `value` | `unknown` | The value. |

Returns `boolean`. stable since API 1.

Errors:

- `GS-API-011` when the list is not a list. Pass a list, such as a variable of the list kind.
- `GS-API-017` when the position is not a whole number in the list. Pass a position from 0 up to the length of the list.

```ts
if (list.has(names, "bo")) await sleep(10);
```

## list.sort(items)

Sorts the list in place: numbers by size, other values as text.

```ts
list.sort(items: unknown[]): void
```

| Parameter | Type | Description |
|---|---|---|
| `items` | `unknown[]` | The list. |

Returns `void`. stable since API 1.

Errors:

- `GS-API-011` when the list is not a list. Pass a list, such as a variable of the list kind.
- `GS-API-017` when the position is not a whole number in the list. Pass a position from 0 up to the length of the list.

```ts
list.sort(names);
```

## list.shuffle(items)

Mixes the list into a random order, in place.

```ts
list.shuffle(items: unknown[]): void
```

| Parameter | Type | Description |
|---|---|---|
| `items` | `unknown[]` | The list. |

Returns `void`. stable since API 1.

Errors:

- `GS-API-011` when the list is not a list. Pass a list, such as a variable of the list kind.
- `GS-API-017` when the position is not a whole number in the list. Pass a position from 0 up to the length of the list.

```ts
list.shuffle(names);
```

## list.clear(items)

Takes every value out of the list.

```ts
list.clear(items: unknown[]): void
```

| Parameter | Type | Description |
|---|---|---|
| `items` | `unknown[]` | The list. |

Returns `void`. stable since API 1.

Errors:

- `GS-API-011` when the list is not a list. Pass a list, such as a variable of the list kind.
- `GS-API-017` when the position is not a whole number in the list. Pass a position from 0 up to the length of the list.

```ts
list.clear(names);
```
