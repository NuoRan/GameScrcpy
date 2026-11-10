# set

Sets of values that each appear once, as JavaScript's Set.

## set.make()

A new, empty set.

```ts
set.make(): Set<unknown>
```

Returns `Set<unknown>`. stable since API 1.

Errors:

- `GS-API-011` when the set is not a set. Pass a set from set.make or a variable of the set kind.

```ts
const seen = set.make();
```

## set.add(items, value)

Adds a value to the set; a value that is already there stays once.

```ts
set.add(items: Set<unknown>, value: unknown): void
```

| Parameter | Type | Description |
|---|---|---|
| `items` | `Set<unknown>` | The set. |
| `value` | `unknown` | The value. |

Returns `void`. stable since API 1.

Errors:

- `GS-API-011` when the set is not a set. Pass a set from set.make or a variable of the set kind.

```ts
set.add(seen, "bo");
```

## set.has(items, value)

Whether the set has that value.

```ts
set.has(items: Set<unknown>, value: unknown): boolean
```

| Parameter | Type | Description |
|---|---|---|
| `items` | `Set<unknown>` | The set. |
| `value` | `unknown` | The value. |

Returns `boolean`. stable since API 1.

Errors:

- `GS-API-011` when the set is not a set. Pass a set from set.make or a variable of the set kind.

```ts
if (set.has(seen, "bo")) await sleep(10);
```

## set.remove(items, value)

Takes a value out of the set; says whether it was there.

```ts
set.remove(items: Set<unknown>, value: unknown): boolean
```

| Parameter | Type | Description |
|---|---|---|
| `items` | `Set<unknown>` | The set. |
| `value` | `unknown` | The value. |

Returns `boolean`. stable since API 1.

Errors:

- `GS-API-011` when the set is not a set. Pass a set from set.make or a variable of the set kind.

```ts
set.remove(seen, "bo");
```

## set.values(items)

All the values of the set, as a list.

```ts
set.values(items: Set<unknown>): unknown[]
```

| Parameter | Type | Description |
|---|---|---|
| `items` | `Set<unknown>` | The set. |

Returns `unknown[]`. stable since API 1.

Errors:

- `GS-API-011` when the set is not a set. Pass a set from set.make or a variable of the set kind.

```ts
for (const name of set.values(seen)) log.info(name);
```

## set.size(items)

How many values the set has.

```ts
set.size(items: Set<unknown>): number
```

| Parameter | Type | Description |
|---|---|---|
| `items` | `Set<unknown>` | The set. |

Returns `number`. stable since API 1.

Errors:

- `GS-API-011` when the set is not a set. Pass a set from set.make or a variable of the set kind.

```ts
if (set.size(seen) > 0) await sleep(10);
```

## set.clear(items)

Takes every value out of the set.

```ts
set.clear(items: Set<unknown>): void
```

| Parameter | Type | Description |
|---|---|---|
| `items` | `Set<unknown>` | The set. |

Returns `void`. stable since API 1.

Errors:

- `GS-API-011` when the set is not a set. Pass a set from set.make or a variable of the set kind.

```ts
set.clear(seen);
```
