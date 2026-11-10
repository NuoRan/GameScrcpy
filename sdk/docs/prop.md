# prop

Properties a component made in logic declares in static props.

## prop.key(key?, options?)

A key property: the component gets it as a Key object in props.

```ts
prop.key(key?: string, options?: PropOptions): ComponentProp
```

| Parameter | Type | Description |
|---|---|---|
| `key` (optional) | `string` | The key it starts with, such as "F" or "MouseLeft". |
| `options` (optional) | `PropOptions` | Its label. |

Returns `ComponentProp`. stable since API 1.

Errors:

- `GS-API-034` when the value is not of the parameter's kind. Pass a number, a string or true or false.

```ts
static props = { key: prop.key("F") };
```

## prop.position(at?, options?)

A position property: the component gets it as a point in props.

```ts
prop.position(at?: PicturePoint, options?: PropOptions): ComponentProp
```

| Parameter | Type | Description |
|---|---|---|
| `at` (optional) | `PicturePoint` | The position it starts with. |
| `options` (optional) | `PropOptions` | Its label. |

Returns `ComponentProp`. stable since API 1.

Errors:

- `GS-API-034` when the value is not of the parameter's kind. Pass a number, a string or true or false.

```ts
static props = { at: prop.position(point(0.5, 0.8)) };
```

## prop.number(value, options?)

A number property.

```ts
prop.number(value: number, options?: NumberPropOptions): ComponentProp
```

| Parameter | Type | Description |
|---|---|---|
| `value` | `number` | The value it starts with. |
| `options` (optional) | `NumberPropOptions` | Its label, and the smallest and largest value. |

Returns `ComponentProp`. stable since API 1.

Errors:

- `GS-API-034` when the value is not of the parameter's kind. Pass a number, a string or true or false.

```ts
static props = { count: prop.number(3) };
```

## prop.text(value, options?)

A text property.

```ts
prop.text(value: string, options?: PropOptions): ComponentProp
```

| Parameter | Type | Description |
|---|---|---|
| `value` | `string` | The text it starts with. |
| `options` (optional) | `PropOptions` | Its label. |

Returns `ComponentProp`. stable since API 1.

Errors:

- `GS-API-034` when the value is not of the parameter's kind. Pass a number, a string or true or false.

```ts
static props = { label: prop.text("Go") };
```

## prop.toggle(value, options?)

An on or off property.

```ts
prop.toggle(value: boolean, options?: PropOptions): ComponentProp
```

| Parameter | Type | Description |
|---|---|---|
| `value` | `boolean` | Whether it starts on. |
| `options` (optional) | `PropOptions` | Its label. |

Returns `ComponentProp`. stable since API 1.

Errors:

- `GS-API-034` when the value is not of the parameter's kind. Pass a number, a string or true or false.

```ts
static props = { loop: prop.toggle(false) };
```

## prop.component(of?, options?)

A property that names another component; the component gets that component object in props, or null when the named one is not of the class or interface given.

```ts
prop.component(of?: string | Interface<unknown> | { readonly prototype: Component } | { readonly component?: Component }, options?: PropOptions): ComponentProp
```

| Parameter | Type | Description |
|---|---|---|
| `of` (optional) | `string \| Interface<unknown> \| { readonly prototype: Component } \| { readonly component?: Component }` | The id of the component it starts with; or a component class, whose type is then the only one the editor lists, or an interface. |
| `options` (optional) | `PropOptions` | Its label. |

Returns `ComponentProp`. stable since API 1.

Errors:

- `GS-API-034` when the value is not of the parameter's kind. Pass a number, a string or true or false.

```ts
static props = { target: prop.component("fire"), scope: prop.component(Hold) };
```
