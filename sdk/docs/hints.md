# hints

The key hints drawn on the phone picture.

## hints.show()

Shows the key hints on the picture.

```ts
hints.show(): boolean
```

Returns `boolean`. stable since API 1.

```ts
hints.show();
```

## hints.hide()

Hides the key hints on the picture.

```ts
hints.hide(): boolean
```

Returns `boolean`. stable since API 1.

```ts
hints.hide();
```

## hints.toggle()

Shows or hides the key hints.

```ts
hints.toggle(): boolean
```

Returns `boolean`. stable since API 1.

```ts
hints.toggle();
```

## hints.setOpacity(opacity)

Sets how solid the key hints are.

```ts
hints.setOpacity(opacity: number): boolean
```

| Parameter | Type | Description |
|---|---|---|
| `opacity` | `number` | From 0 (invisible) to 1 (solid). |

Returns `boolean`. stable since API 1.

```ts
hints.setOpacity(0.3);
```

## hints.reveal(on)

While on, every key hint of this phone is drawn at full opacity, even when the hints are hidden or faded; typing on the phone still hides them. Each logic and each task has its own switch, which turns off when that logic or task stops.

```ts
hints.reveal(on: boolean): boolean
```

| Parameter | Type | Description |
|---|---|---|
| `on` | `boolean` | true to draw every key hint at full opacity, false to give them back their usual look. |

Returns `boolean`. stable since API 1.

```ts
keys.Tab.onPress(() => { hints.reveal(true); }); keys.Tab.onRelease(() => { hints.reveal(false); });
```
