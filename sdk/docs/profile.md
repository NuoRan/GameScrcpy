# profile

The profile in use on this phone.

## profile.current()

The profile this script belongs to.

```ts
profile.current(): ProfileInfo | null
```

Returns `ProfileInfo | null`. stable since API 1.

```ts
log.info(profile.current()?.name);
```

## profile.switchTo(name)

Switches this phone to another profile by id or name; this script stops and that profile's logic starts.

```ts
profile.switchTo(name: string): boolean
```

| Parameter | Type | Description |
|---|---|---|
| `name` | `string` | The id or the name of a profile in the library. |

Returns `boolean`. stable since API 1.

Errors:

- `GS-API-011` when the name is not a string or is empty. Pass a name.

```ts
profile.switchTo("Driving");
```

## profile.next()

Switches this phone to the next profile in the library.

```ts
profile.next(): boolean
```

Returns `boolean`. stable since API 1.

```ts
profile.next();
```

## profile.previous()

Switches this phone to the previous profile in the library.

```ts
profile.previous(): boolean
```

Returns `boolean`. stable since API 1.

```ts
profile.previous();
```

## profile.save()

Asks in the phone's mirror window whether to keep the changes this logic made to its components (each shown as old and new value) in the profile, and resolves with whether they were saved. Saving reloads the profile and starts the logic again, as saving in the key editor does. Resolves with false at once when nothing was changed, while the key editor is open, for a profile that is not in the library, and in a task.

```ts
profile.save(): Promise<boolean>
```

Returns `Promise<boolean>`. stable since API 1.

Errors:

- `GS-API-001` when no phone is connected to this script. Start the script on a phone that is mirroring.

```ts
controls.fire.setRate(12); await profile.save();
```
