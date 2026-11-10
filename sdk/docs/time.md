# time

The script clock.

## time.now()

Milliseconds since the scripts started; virtual time when replaying a recording.

```ts
time.now(): number
```

Returns `number`. stable since API 1.

```ts
const started = time.now();
```
