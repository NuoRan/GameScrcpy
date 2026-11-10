# ai

Questions from logic to the AI service set up in Settings, AI; they work only while Let logic call AI is on there.

## ai.ask(question, options?)

Asks the AI service set up in Settings, AI, the one the assistant in the editor uses, and resolves with the text of its answer; null when it answers without text, when a picture is asked for and the phone has shown none yet, and in replays, which ask nothing. With picture, the phone's picture or a part of it goes along, made smaller so its long side is at most 1024 pixels. It works only while Let logic call AI is on there: each question spends your quota, and each logic may ask at most 10 times a minute. Cancelling the task stops waiting.

```ts
ai.ask(question: string, options?: AiAskOptions): Promise<string | null>
```

| Parameter | Type | Description |
|---|---|---|
| `question` | `string` | What to ask, up to 4000 characters. |
| `options` (optional) | `AiAskOptions` | A picture of the phone to send along, and how long to wait for the answer. |

Returns `Promise<string | null>`. stable since API 1.

Errors:

- `GS-API-011` when the question is not a string of 1 to 4000 characters that are not all spaces, the options are not an object, or picture is not true, false, a region or a region name. Pass the question as text and the options as a plain object.
- `GS-API-003` when timeoutMs is not from 1000 to 120000. Pass milliseconds in that range.
- `GS-API-002` when the region is outside the picture. Regions are picture ratios.
- `GS-API-018` when there is no region with that name. Use a name from res/resources.json.
- `GS-API-020` when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
- `GS-API-095` when Let logic call AI is off in Settings, AI, or no AI service is set up there. Set up the service there, test the connection and turn the setting on.
- `GS-API-096` when this logic called AI 10 times in the last minute. Ask less often, or wait a little.
- `GS-API-097` when the AI service failed or gave no answer within timeoutMs; the message says why. Check the service in Settings, AI, or try again later.
- `GS-API-072` when the logic belongs to an extension whose manifest does not ask for the permission ai. Add "ai" to the permissions in the extension's manifest, with a reason.
- `GS-API-073` when a picture is asked for by logic that has no phone, such as the app script of an extension. Ask without a picture, or from a session script.

```ts
const answer = await ai.ask("Is a boss on the screen? Answer yes or no.", { picture: true }); if (answer?.toLowerCase().startsWith("yes")) ui.notify("A boss is here");
```
