# math

Numbers and positions.

## math.clamp(value, low, high)

Keeps a number between low and high.

```ts
math.clamp(value: number, low: number, high: number): number
```

| Parameter | Type | Description |
|---|---|---|
| `value` | `number` | The number. |
| `low` | `number` | The smallest it may be. |
| `high` | `number` | The largest it may be. |

Returns `number`. stable since API 1.

Errors:

- `GS-API-017` when a value is not a finite number. Pass numbers.

```ts
math.clamp(1.4, 0, 1);
```

## math.lerp(from, to, t)

The number t of the way from from to to.

```ts
math.lerp(from: number, to: number, t: number): number
```

| Parameter | Type | Description |
|---|---|---|
| `from` | `number` | Where it starts. |
| `to` | `number` | Where it ends. |
| `t` | `number` | How far along, 0 at from and 1 at to. |

Returns `number`. stable since API 1.

Errors:

- `GS-API-017` when a value is not a finite number. Pass numbers.

```ts
math.lerp(0, 10, 0.25);
```

## math.random(low?, high?)

A random number from low up to high.

```ts
math.random(low?: number, high?: number): number
```

| Parameter | Type | Description |
|---|---|---|
| `low` (optional) | `number` | The smallest it may be (0 by default). |
| `high` (optional) | `number` | The largest it may be (1 by default). |

Returns `number`. stable since API 1.

Errors:

- `GS-API-017` when a value is not a finite number. Pass numbers.

```ts
math.random(0.4, 0.6);
```

## math.randomInt(low, high)

A random whole number from low to high, both included.

```ts
math.randomInt(low: number, high: number): number
```

| Parameter | Type | Description |
|---|---|---|
| `low` | `number` | The smallest whole number it may be. |
| `high` | `number` | The largest whole number it may be. |

Returns `number`. stable since API 1.

Errors:

- `GS-API-017` when a value is not a finite number. Pass numbers.

```ts
math.randomInt(1, 6);
```

## math.randomIn(region)

A random position inside a region.

```ts
math.randomIn(region: Region): PicturePoint
```

| Parameter | Type | Description |
|---|---|---|
| `region` | `Region` | Part of the picture, as ratios. |

Returns `PicturePoint`. stable since API 1.

Errors:

- `GS-API-017` when a value is not a finite number. Pass numbers.

```ts
await touch.tap(math.randomIn(res.region("Button")));
```

## math.distance(from, to)

How far apart two positions are, as a ratio of the picture.

```ts
math.distance(from: PicturePoint, to: PicturePoint): number
```

| Parameter | Type | Description |
|---|---|---|
| `from` | `PicturePoint` | The first position. |
| `to` | `PicturePoint` | The second position. |

Returns `number`. stable since API 1.

Errors:

- `GS-API-017` when a value is not a finite number. Pass numbers.

```ts
math.distance(point(0, 0), point(0.3, 0.4));
```

## math.angle(from, to)

The direction from one position to another in degrees: 0 is up, 90 right, 180 down and 270 left.

```ts
math.angle(from: PicturePoint, to: PicturePoint): number
```

| Parameter | Type | Description |
|---|---|---|
| `from` | `PicturePoint` | The first position. |
| `to` | `PicturePoint` | The second position. |

Returns `number`. stable since API 1.

Errors:

- `GS-API-017` when a value is not a finite number. Pass numbers.

```ts
math.angle(point(0.5, 0.5), point(0.5, 0.2));
```

## math.round(value)

The nearest whole number; a half goes up.

```ts
math.round(value: number): number
```

| Parameter | Type | Description |
|---|---|---|
| `value` | `number` | The number. |

Returns `number`. stable since API 1.

Errors:

- `GS-API-017` when a value is not a finite number. Pass numbers.

```ts
math.round(1.6);
```

## math.floor(value)

The largest whole number that is not more than the value.

```ts
math.floor(value: number): number
```

| Parameter | Type | Description |
|---|---|---|
| `value` | `number` | The number. |

Returns `number`. stable since API 1.

Errors:

- `GS-API-017` when a value is not a finite number. Pass numbers.

```ts
math.floor(1.6);
```

## math.ceil(value)

The smallest whole number that is not less than the value.

```ts
math.ceil(value: number): number
```

| Parameter | Type | Description |
|---|---|---|
| `value` | `number` | The number. |

Returns `number`. stable since API 1.

Errors:

- `GS-API-017` when a value is not a finite number. Pass numbers.

```ts
math.ceil(1.2);
```

## math.abs(value)

The value without its sign.

```ts
math.abs(value: number): number
```

| Parameter | Type | Description |
|---|---|---|
| `value` | `number` | The number. |

Returns `number`. stable since API 1.

Errors:

- `GS-API-017` when a value is not a finite number. Pass numbers.

```ts
math.abs(-3);
```

## math.min(a, b)

The smaller of two numbers.

```ts
math.min(a: number, b: number): number
```

| Parameter | Type | Description |
|---|---|---|
| `a` | `number` | The first number. |
| `b` | `number` | The second number. |

Returns `number`. stable since API 1.

Errors:

- `GS-API-017` when a value is not a finite number. Pass numbers.

```ts
math.min(2, 5);
```

## math.max(a, b)

The larger of two numbers.

```ts
math.max(a: number, b: number): number
```

| Parameter | Type | Description |
|---|---|---|
| `a` | `number` | The first number. |
| `b` | `number` | The second number. |

Returns `number`. stable since API 1.

Errors:

- `GS-API-017` when a value is not a finite number. Pass numbers.

```ts
math.max(2, 5);
```
