# speech

Reading text aloud and listening for voice commands, with Windows or an engine that an extension provides; the logic of an extension needs the permission speech.listen to listen.

## speech.say(text, options?)

Reads a text aloud with the engine, voice, rate and volume set in Settings, Audio, unless the options pick others. Each engine reads one text at a time and the others wait their turn. Resolves with true once the text has been read, and with false when it is cut off by interrupt, speech.stop(), the task being cancelled or the logic stopping.

```ts
speech.say(text: string, options?: SpeechSayOptions): Promise<boolean>
```

| Parameter | Type | Description |
|---|---|---|
| `text` | `string` | What to read, 1 to 1000 characters. |
| `options` (optional) | `SpeechSayOptions` | The engine, voice, rate and volume, and whether to cut in at once. |

Returns `Promise<boolean>`. stable since API 1.

Errors:

- `GS-API-110` when there is no such engine, it cannot read aloud, or it has no such voice. Pick an engine and a voice from speech.engines() and speech.voices(), or leave them out.
- `GS-API-111` when the text is not 1 to 1000 characters, rate is not from 0.5 to 2, volume is not from 0 to 1, or the logic has 32 texts waiting already. Pass a shorter text and values in range, or wait for the texts before it.
- `GS-API-112` when the engine failed; the message says why. Try another voice or engine, or check its extension.

```ts
await speech.say("The boss comes in ten seconds", { rate: 1.2 });
```

## speech.stop()

Stops what this logic is reading and drops the texts it has waiting.

```ts
speech.stop(): void
```

Returns `void`. stable since API 1.

```ts
speech.stop();
```

## speech.engines()

The speech engines: Windows, and those that extensions provide, with what each can do.

```ts
speech.engines(): readonly SpeechEngineInfo[]
```

Returns `readonly SpeechEngineInfo[]`. stable since API 1.

```ts
for (const engine of speech.engines()) log.info(engine.id, engine.say, engine.listen);
```

## speech.voices(options?)

The voices of an engine.

```ts
speech.voices(options?: SpeechVoicesOptions): Promise<readonly SpeechVoice[]>
```

| Parameter | Type | Description |
|---|---|---|
| `options` (optional) | `SpeechVoicesOptions` | The engine whose voices to list; the one in Settings when left out. |

Returns `Promise<readonly SpeechVoice[]>`. stable since API 1.

Errors:

- `GS-API-110` when there is no such engine. Pick an engine from speech.engines().
- `GS-API-112` when the engine could not list its voices; the message says why. Check the engine's extension.

```ts
const voices = await speech.voices(); log.info(voices.map((voice) => voice.name).join(", "));
```

## speech.listen(phrases, handler, options?)

Listens to the microphone for voice commands with the engine and language set in Settings, Audio, unless the options pick others. Each command heard with at least the confidence asked for (0.6 by default) runs the handler as a task, one after another. A listener opened in a task closes when the task ends, one opened at the top level when the logic stops; it also closes when its engine stops, and the log says why.

```ts
speech.listen(phrases: readonly string[] | null, handler: (heard: TaskContext & SpeechHeard) => void | Promise<void>, options?: SpeechListenOptions): Promise<SpeechListener>
```

| Parameter | Type | Description |
|---|---|---|
| `phrases` | `readonly string[] \| null` | The commands to listen for, 1 to 256 of 1 to 64 characters each; null takes any words when the engine can. |
| `handler` | `(heard: TaskContext & SpeechHeard) => void \| Promise<void>` | What to run for each command heard, one after another. |
| `options` (optional) | `SpeechListenOptions` | The engine, the language and how sure the engine must be. |

Returns `Promise<SpeechListener>`. stable since API 1.

Errors:

- `GS-API-110` when there is no such engine, it cannot listen, it does not know the language, or it only listens for a list of commands. Pick an engine and a language from speech.engines(), or pass a list of commands.
- `GS-API-111` when the commands are not 1 to 256 texts of 1 to 64 characters, confidence is not from 0 to 1, or the logic listens with 8 listeners already. Pass fewer or shorter commands and values in range, or close a listener.
- `GS-API-113` when the logic belongs to an extension whose manifest does not ask for the permission speech.listen. Add "speech.listen" to the permissions in the extension's manifest, with a reason.
- `GS-API-008` when the handler is not a function. Pass a function.

```ts
await speech.listen(["open map", "close bag"], ({ phrase }) => log.info("heard", phrase));
```
