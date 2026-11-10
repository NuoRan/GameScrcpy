# dict

Dictionaries from keys to values, as JavaScript's Map.

## dict.make()

A new, empty dictionary.

```ts
dict.make(): Map<unknown, unknown>
```

Returns `Map<unknown, unknown>`. stable since API 1.

Errors:

- `GS-API-011` when the dictionary is not a dictionary. Pass a dictionary from dict.make or a variable of the dictionary kind.

```ts
const scores = dict.make();
```

## dict.set(map, key, value)

Stores a value under a key, replacing what was there.

```ts
dict.set(map: Map<unknown, unknown>, key: unknown, value: unknown): void
```

| Parameter | Type | Description |
|---|---|---|
| `map` | `Map<unknown, unknown>` | The dictionary. |
| `key` | `unknown` | The key. |
| `value` | `unknown` | The value. |

Returns `void`. stable since API 1.

Errors:

- `GS-API-011` when the dictionary is not a dictionary. Pass a dictionary from dict.make or a variable of the dictionary kind.

```ts
dict.set(scores, "bo", 3);
```

## dict.get(map, key)

The value stored under a key, or undefined.

```ts
dict.get(map: Map<unknown, unknown>, key: unknown): unknown
```

| Parameter | Type | Description |
|---|---|---|
| `map` | `Map<unknown, unknown>` | The dictionary. |
| `key` | `unknown` | The key. |

Returns `unknown`. stable since API 1.

Errors:

- `GS-API-011` when the dictionary is not a dictionary. Pass a dictionary from dict.make or a variable of the dictionary kind.

```ts
await ui.toast(String(dict.get(scores, "bo")));
```

## dict.has(map, key)

Whether the dictionary has a value under that key.

```ts
dict.has(map: Map<unknown, unknown>, key: unknown): boolean
```

| Parameter | Type | Description |
|---|---|---|
| `map` | `Map<unknown, unknown>` | The dictionary. |
| `key` | `unknown` | The key. |

Returns `boolean`. stable since API 1.

Errors:

- `GS-API-011` when the dictionary is not a dictionary. Pass a dictionary from dict.make or a variable of the dictionary kind.

```ts
if (dict.has(scores, "bo")) await sleep(10);
```

## dict.remove(map, key)

Removes the value under a key; says whether there was one.

```ts
dict.remove(map: Map<unknown, unknown>, key: unknown): boolean
```

| Parameter | Type | Description |
|---|---|---|
| `map` | `Map<unknown, unknown>` | The dictionary. |
| `key` | `unknown` | The key. |

Returns `boolean`. stable since API 1.

Errors:

- `GS-API-011` when the dictionary is not a dictionary. Pass a dictionary from dict.make or a variable of the dictionary kind.

```ts
dict.remove(scores, "bo");
```

## dict.keys(map)

All the keys of the dictionary, as a list.

```ts
dict.keys(map: Map<unknown, unknown>): unknown[]
```

| Parameter | Type | Description |
|---|---|---|
| `map` | `Map<unknown, unknown>` | The dictionary. |

Returns `unknown[]`. stable since API 1.

Errors:

- `GS-API-011` when the dictionary is not a dictionary. Pass a dictionary from dict.make or a variable of the dictionary kind.

```ts
for (const key of dict.keys(scores)) log.info(key);
```

## dict.values(map)

All the values of the dictionary, as a list.

```ts
dict.values(map: Map<unknown, unknown>): unknown[]
```

| Parameter | Type | Description |
|---|---|---|
| `map` | `Map<unknown, unknown>` | The dictionary. |

Returns `unknown[]`. stable since API 1.

Errors:

- `GS-API-011` when the dictionary is not a dictionary. Pass a dictionary from dict.make or a variable of the dictionary kind.

```ts
for (const score of dict.values(scores)) log.info(score);
```

## dict.size(map)

How many keys the dictionary has.

```ts
dict.size(map: Map<unknown, unknown>): number
```

| Parameter | Type | Description |
|---|---|---|
| `map` | `Map<unknown, unknown>` | The dictionary. |

Returns `number`. stable since API 1.

Errors:

- `GS-API-011` when the dictionary is not a dictionary. Pass a dictionary from dict.make or a variable of the dictionary kind.

```ts
if (dict.size(scores) > 0) await sleep(10);
```

## dict.clear(map)

Takes every key out of the dictionary.

```ts
dict.clear(map: Map<unknown, unknown>): void
```

| Parameter | Type | Description |
|---|---|---|
| `map` | `Map<unknown, unknown>` | The dictionary. |

Returns `void`. stable since API 1.

Errors:

- `GS-API-011` when the dictionary is not a dictionary. Pass a dictionary from dict.make or a variable of the dictionary kind.

```ts
dict.clear(scores);
```
