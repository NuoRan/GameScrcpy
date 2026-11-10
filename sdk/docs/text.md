# text

Text: joining, filling in, finding, cutting and changing case.

## text.join(parts, separator?)

Joins values into one text, with a separator between them.

```ts
text.join(parts: unknown[], separator?: string): string
```

| Parameter | Type | Description |
|---|---|---|
| `parts` | `unknown[]` | The values to join, in order. |
| `separator` (optional) | `string` | What goes between them (nothing by default). |

Returns `string`. stable since API 1.

Errors:

- `GS-API-011` when one of the arguments is not text. Pass text.

```ts
const line = text.join(names, ", ");
```

## text.format(template, values)

Fills {0}, {1} and so on in a template with the values.

```ts
text.format(template: string, ...values: unknown[]): string
```

| Parameter | Type | Description |
|---|---|---|
| `template` | `string` | The text, with {0}, {1} and so on where the values go. |
| `values` | `...unknown[]` | The values, in the order of the numbers. |

Returns `string`. stable since API 1.

Errors:

- `GS-API-011` when one of the arguments is not text. Pass text.

```ts
await ui.toast(text.format("Round {0} of {1}", round, total));
```

## text.find(text, part)

Where the part starts, or -1 when it is not there.

```ts
text.find(text: string, part: string): number
```

| Parameter | Type | Description |
|---|---|---|
| `text` | `string` | The text. |
| `part` | `string` | What to look for. |

Returns `number`. stable since API 1.

Errors:

- `GS-API-011` when one of the arguments is not text. Pass text.

```ts
if (text.find(line, "Round") === 0) await sleep(10);
```

## text.has(text, part)

Whether the text contains the part.

```ts
text.has(text: string, part: string): boolean
```

| Parameter | Type | Description |
|---|---|---|
| `text` | `string` | The text. |
| `part` | `string` | What to look for. |

Returns `boolean`. stable since API 1.

Errors:

- `GS-API-011` when one of the arguments is not text. Pass text.

```ts
if (text.has(line, "Round")) await sleep(10);
```

## text.split(text, separator)

Cuts the text wherever the separator is and gives the pieces as a list.

```ts
text.split(text: string, separator: string): string[]
```

| Parameter | Type | Description |
|---|---|---|
| `text` | `string` | The text to cut. |
| `separator` | `string` | Where to cut it. |

Returns `string[]`. stable since API 1.

Errors:

- `GS-API-011` when one of the arguments is not text. Pass text.

```ts
const fields = text.split(line, ",");
```

## text.replace(text, part, instead)

Gives the text with the first part replaced by the other text.

```ts
text.replace(text: string, part: string, instead: string): string
```

| Parameter | Type | Description |
|---|---|---|
| `text` | `string` | The text. |
| `part` | `string` | What to look for; only the first one is replaced. |
| `instead` | `string` | What to put there. |

Returns `string`. stable since API 1.

Errors:

- `GS-API-011` when one of the arguments is not text. Pass text.

```ts
const fixed = text.replace(line, "bo", "Bo");
```

## text.upper(text)

The same text in upper case.

```ts
text.upper(text: string): string
```

| Parameter | Type | Description |
|---|---|---|
| `text` | `string` | The text. |

Returns `string`. stable since API 1.

Errors:

- `GS-API-011` when one of the arguments is not text. Pass text.

```ts
text.upper(name);
```

## text.lower(text)

The same text in lower case.

```ts
text.lower(text: string): string
```

| Parameter | Type | Description |
|---|---|---|
| `text` | `string` | The text. |

Returns `string`. stable since API 1.

Errors:

- `GS-API-011` when one of the arguments is not text. Pass text.

```ts
text.lower(name);
```

## text.trim(text)

The same text without spaces at either end.

```ts
text.trim(text: string): string
```

| Parameter | Type | Description |
|---|---|---|
| `text` | `string` | The text. |

Returns `string`. stable since API 1.

Errors:

- `GS-API-011` when one of the arguments is not text. Pass text.

```ts
text.trim(name);
```

## text.length(text)

How many characters the text has.

```ts
text.length(text: string): number
```

| Parameter | Type | Description |
|---|---|---|
| `text` | `string` | The text. |

Returns `number`. stable since API 1.

Errors:

- `GS-API-011` when one of the arguments is not text. Pass text.

```ts
if (text.length(name) > 8) await sleep(10);
```
