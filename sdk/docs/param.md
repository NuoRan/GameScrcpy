# param

Settings a task declares in static params; the Tasks page shows them and passes the values to the task.

## param.number(value, options?)

A number setting of a task, shown as a number box on the Tasks page.

```ts
param.number(value: number, options?: ParamOptions): TaskParam
```

| Parameter | Type | Description |
|---|---|---|
| `value` | `number` | The value it starts with. |
| `options` (optional) | `ParamOptions` | Its label, and the smallest and largest value. |

Returns `TaskParam`. stable since API 1.

Errors:

- `GS-API-034` when the value is not of the parameter's kind. Pass a number, a string or true or false.

```ts
static params = { rounds: param.number(10, { min: 1 }) };
```

## param.text(value, options?)

A text setting of a task, shown as a text box on the Tasks page.

```ts
param.text(value: string, options?: ParamOptions): TaskParam
```

| Parameter | Type | Description |
|---|---|---|
| `value` | `string` | The text it starts with. |
| `options` (optional) | `ParamOptions` | Its label. |

Returns `TaskParam`. stable since API 1.

Errors:

- `GS-API-034` when the value is not of the parameter's kind. Pass a number, a string or true or false.

```ts
static params = { map: param.text("Erangel") };
```

## param.toggle(value, options?)

An on or off setting of a task, shown as a switch on the Tasks page.

```ts
param.toggle(value: boolean, options?: ParamOptions): TaskParam
```

| Parameter | Type | Description |
|---|---|---|
| `value` | `boolean` | Whether it starts on. |
| `options` (optional) | `ParamOptions` | Its label. |

Returns `TaskParam`. stable since API 1.

Errors:

- `GS-API-034` when the value is not of the parameter's kind. Pass a number, a string or true or false.

```ts
static params = { claim: param.toggle(true) };
```
