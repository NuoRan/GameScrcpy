# store

Values this script keeps between runs (up to 1 MiB of JSON).

## store.get(key)

Reads a value this script stored earlier, or undefined.

```ts
store.get(key: string): StoreValue | undefined
```

| Parameter | Type | Description |
|---|---|---|
| `key` | `string` | The name of the value. |

Returns `StoreValue | undefined`. stable since API 1.

Errors:

- `GS-API-006` when the key is not a string or the value is not a JSON value. Store plain data only.
- `GS-API-007` when the store would be larger than 1 MiB. Remove keys you no longer need.

```ts
const best = store.get("best") ?? 0;
```

## store.set(key, value)

Stores a value that stays after the script and the program restart.

```ts
store.set(key: string, value: StoreValue): void
```

| Parameter | Type | Description |
|---|---|---|
| `key` | `string` | The name of the value. |
| `value` | `StoreValue` | A JSON value: null, a boolean, number, string, array or plain object. |

Returns `void`. stable since API 1.

Errors:

- `GS-API-006` when the key is not a string or the value is not a JSON value. Store plain data only.
- `GS-API-007` when the store would be larger than 1 MiB. Remove keys you no longer need.

```ts
store.set("best", 42);
```

## store.remove(key)

Removes a stored value.

```ts
store.remove(key: string): void
```

| Parameter | Type | Description |
|---|---|---|
| `key` | `string` | The name of the value. |

Returns `void`. stable since API 1.

Errors:

- `GS-API-006` when the key is not a string or the value is not a JSON value. Store plain data only.
- `GS-API-007` when the store would be larger than 1 MiB. Remove keys you no longer need.

```ts
store.remove("best");
```

## store.keys()

The names of all stored values.

```ts
store.keys(): string[]
```

Returns `string[]`. stable since API 1.

```ts
for (const key of store.keys()) log.info(key);
```
