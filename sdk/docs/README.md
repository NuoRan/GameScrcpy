# GameScrcpy script API

Positions are ratios of the phone picture: (0, 0) is the top left corner and (1, 1) the bottom right one, so scripts work at any resolution and orientation.

- [Global functions and values](globals.md)
- [`log`](log.md): Lines in the script console, with the task that wrote them.
- [`time`](time.md): The script clock.
- [`touch`](touch.md): Touches on the phone picture. Positions are ratios of the picture: (0, 0) top left, (1, 1) bottom right.
- [`device`](device.md): The phone this script runs on.
- [`clipboard`](clipboard.md): The phone's clipboard, not the computer's.
- [`mapping`](mapping.md): The controls of the profile in use, by id; the script's output never feeds back into its input.
- [`hints`](hints.md): The key hints drawn on the phone picture.
- [`profile`](profile.md): The profile in use on this phone.
- [`store`](store.md): Values this script keeps between runs (up to 1 MiB of JSON).
- [`ui`](ui.md): Messages, questions and drawings in the phone's mirror window.
- [`input`](input.md): What the user holds and points at, readable only while the phone's mirror window is in front.
- [`hardware`](hardware.md): Serial ports, USB HID devices and Bluetooth LE devices the user allows. Every logic, your own too, can only open the devices the user ticked; the logic of an extension also needs the permission hardware.
- [`net`](net.md): Web services over https and WebSocket, made by GameScrcpy for the logic, and connections from programs on this computer over HTTP on 127.0.0.1. The logic of an extension needs the permission network, which lists the hosts it may reach, and network.listen to listen.
- [`speech`](speech.md): Reading text aloud and listening for voice commands, with Windows or an engine that an extension provides; the logic of an extension needs the permission speech.listen to listen.
- [`window`](window.md): The phone's mirror window.
- [`vision`](vision.md): Finding pictures in the phone's picture: the next frame, or the picture already shown when none comes within 0.25 s.
- [`res`](res.md): The named points, regions and paths of the profile, kept in res/resources.json by the vision workbench.
- [`math`](math.md): Numbers and positions.
- [`list`](list.md): Lists of values, changed in place.
- [`dict`](dict.md): Dictionaries from keys to values, as JavaScript's Map.
- [`set`](set.md): Sets of values that each appear once, as JavaScript's Set.
- [`text`](text.md): Text: joining, filling in, finding, cutting and changing case.
- [`param`](param.md): Settings a task declares in static params; the Tasks page shows them and passes the values to the task.
- [`prop`](prop.md): Properties a component made in logic declares in static props.
- [`mcp`](mcp.md): Tools that the app script of an extension offers to AI clients over the local MCP connection.
- [`events`](events.md): Events of GameScrcpy itself, such as a phone connecting, and events that the native DLL of an extension sends to the extension's own logic and panels and to the logic of the extensions that depend on it.
- [`ai`](ai.md): Questions from logic to the AI service set up in Settings, AI; they work only while Let logic call AI is on there.
- [`media`](media.md): The phone's picture as frames, for analysis and AI; the logic of an extension needs the permission media.read.
- [`files`](files.md): Files the user picks and folders the user chooses for the logic. The logic of an extension needs the permission files.pick to pick files, and files.paths, which names its folders, to open folders.

## Objects

### Component

A component of the profile in use. Every type below has these members too.

| Member | Description |
|---|---|
| `readonly id: string` | The control id from the profile; stays the same when the control is renamed. |
| `readonly name: string` | The name shown in the editor. |
| `readonly position: PicturePoint \| null` | Where the component sits on the picture, as ratios, for the phone's picture when the logic started; it follows moveTo(). null for a component made in logic with new. |
| `readonly type: ComponentType` | The kind of component, such as "tap", "wasd" or "camera". |
| `readonly props: Readonly<Record<string, any>>` | The component's settings: from profile.json, or for a component made in logic its static props with the values given to it (keys become Key objects, positions points and component ids components). |
| `readonly key: InputName \| null` | Its main key, such as "KeyF" or "Mouse.Right"; null when it has none. |
| `readonly inputs: Readonly<Record<string, InputName>>` | Every key of the component by slot, such as { up: "KeyW" }. |
| `readonly hint: Hint` | Its key hint on the picture. |
| `enabled: boolean` | Whether the control reacts to its keys. Setting it lasts until the script stops or the profile changes, and ends a binding made with bind(). |
| `enable(): void` | Turns the component on (the same as enabled = true). |
| `disable(): void` | Turns the component off. |
| `toggle(): void` | Turns the component on or off. |
| `readonly active: boolean` | Whether its key is held down now. |
| `readonly pressedAt: number \| null` | When its key went down (time.now()), or null when it is up. |
| `readonly heldMs: number` | How long its key has been held, in milliseconds; 0 when it is up. |
| `readonly touching: boolean` | Whether it has a finger on the phone's screen now. |
| `trigger(): Action<void>` | Presses and releases the control with its own behavior; the script's own output never triggers scripts. |
| `hold(durationMs: number): Action<void>` | Holds the control down for a while, then releases it; released early if the task is cancelled. Throws GS-API-003 when the duration is not more than 0 and at most 60000. |
| `press(): void` | Puts its finger down and keeps it there until release(). |
| `release(): void` | Lifts its finger. |
| `reset(): void` | Lifts all its fingers and puts it back at rest: a stick back to the middle, a view back to its anchor. |
| `moveTo(x: number \| PicturePoint, y?: number): void` | Moves the component and its key hint to another place on the picture until the logic stops or the profile changes; a finger already down stays where it is, and profile.save() can keep the new place. Throws GS-API-002 for a place outside the picture and GS-API-036 for a component made in logic with new. |
| `moveBy(dx: number, dy: number): void` | Moves the component by picture ratios, as moveTo() does. |
| `resetPosition(): void` | Puts the component back where the profile has it. |
| `resetProps(): void` | Puts back everything the logic changed on this component: its place, rate, duration, radius and sensitivity. |
| `bind(property: "enabled", compute: () => unknown): Binding` | Keeps a property computed from states: compute runs at once and again whenever a state it read last time changes, also when the script changes it. Only "enabled" can be bound. Disposing the binding, or the end of the task that made it, puts the property back to its value from before the binding; binding it again takes over without putting it back, and setting it directly, or with mapping.setEnabled, ends the binding. Throws GS-API-031 for another property, GS-API-008 for a compute that is not a function and GS-API-032 when compute returns a Promise. Later, a compute that throws is reported and keeps the last value, and one that returns a Promise is reported with GS-API-032 and ends the binding as dispose() does. |
| `onPress(handler: (event: ControlEvent) => void \| Promise<void>, options?: KeyListenOptions): Subscription` | Starts a task each time the control's key goes down. In the profile's logic it takes the key of a tap, hold, long press, turbo, swipe or multi-press control unless consume is false (KeyListenOptions). Throws GS-API-008 for a handler that is not a function, GS-API-009 for an unknown again rule and GS-API-011 for a consume that is not true or false. |
| `onRelease(handler: (event: ControlEvent) => void \| Promise<void>, options?: KeyListenOptions): Subscription` | Starts a task each time the control's key comes up. It takes the key as onPress does. |
| `whileHeld(handler: (event: ControlEvent) => void \| Promise<void>, options?: KeyListenOptions): Subscription` | Starts a task when the control's key goes down and cancels it when the key comes up. It takes the key as onPress does. |
| `onEnabled(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time it is turned on. |
| `onDisabled(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time it is turned off. |
| `onTouchStart(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time it puts a finger on the screen. |
| `onTouchEnd(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time it lifts its last finger. |
| `setup(): void` | For a component made in logic: runs once after it is created; set up its listeners here. Create components at the top level of the logic so these listeners last. |
| `event(): ComponentEvent` | Makes an event the component can emit and others can listen to, kept in a field: pressed = this.event(). |
| `child<T extends Component>(kind: { readonly component?: T }): T` | In a field of a component class: one of its child components, a built-in component the editor places with it and moves with it, such as cancel = this.child(Tap) or skills = [this.child(Skill), this.child(Skill)]. The calls take the children in the order the class declares them; null when the profile has no such child. Throws GS-API-008 when kind is not a class. |
| `readonly finger: ComponentFinger` | For a component made in logic: its own finger, which stays down until up() or until the logic stops. |

### Hint

The key hint of a component on the picture; changes last until the script stops.

| Member | Description |
|---|---|
| `show(): void` | Shows this hint again after hide(). |
| `hide(): void` | Hides this hint on the picture; the component still works. |
| `setText(text: string): void` | Writes other words in the hint, up to 16 characters. |
| `moveTo(x: number \| PicturePoint, y?: number): void` | Shows the hint at another place on the picture; the component itself still touches where the profile has it. |
| `flash(): void` | Makes the hint flash twice to draw the eye to it; nothing else changes. |
| `reset(): void` | Puts the hint back as the profile has it. |

### Tap extends Component

A tap component: each key press taps the screen once.

| Member | Description |
|---|---|
| `readonly tapMs: number` | How long each tap holds the finger down, in milliseconds; 0 means two frames. |
| `tap(): Action<void>` | Taps once, holding the finger down for tapMs (60 milliseconds when tapMs is 0). |
| `tapTimes(count: number, gapMs?: number): Action<void>` | Taps count times (1 to 100), gapMs apart (100 by default), each as long as tap(). |
| `setDuration(durationMs: number): void` | Changes how long each tap holds the finger down (0 to 1000 milliseconds; 0 means two frames) until the logic stops or the profile changes. Throws GS-API-017 outside that range. |
| `resetDuration(): void` | Puts back the tap length from the profile. |

### Hold extends Component

A hold component: the finger stays down while the key is held.

| Member | Description |
|---|---|
| `readonly holding: boolean` | Whether it is pressing the screen now. |
| `holdFor(durationMs: number): Action<void>` | Presses for a while, then lets go. |
| `toggleHold(): void` | Presses if it is up, lets go if it is pressing. |
| `onPressed(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time it puts a finger on the screen. |
| `onReleased(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time it lifts its last finger. |

### LongPress extends Component

A long-press component: each key press holds the screen for holdMs.

| Member | Description |
|---|---|
| `readonly holdMs: number` | How long a long press lasts, in milliseconds. |
| `readonly holding: boolean` | Whether it is pressing the screen now. |
| `longPress(durationMs?: number): Action<void>` | Long-presses for holdMs, or for durationMs when given. |
| `setDuration(durationMs: number): void` | Changes how long a long press lasts (100 to 10000 milliseconds) until the logic stops or the profile changes. Throws GS-API-017 outside that range. |
| `resetDuration(): void` | Puts back the long press length from the profile. |
| `onStart(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time it puts a finger on the screen. |
| `onEnd(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time it lifts its last finger. |

### Turbo extends Component

A turbo component: holding the key taps again and again.

| Member | Description |
|---|---|
| `readonly rateHz: number` | Presses a second: from the profile, or the rate setRate() gave. |
| `readonly tapMs: number` | How long each tap holds the finger down, in milliseconds; 0 means two frames. |
| `readonly firing: boolean` | Whether start() is pressing again and again now. |
| `start(rateHz?: number): void` | Presses again and again until stop(), at rateHz or the given rate (1 to 30 a second). |
| `stop(): void` | Stops start(). |
| `burst(count: number): Action<void>` | Presses count times (1 to 100) at its rate. |
| `setRate(rateHz: number): void` | Changes the presses a second (1 to 30) for its key, start() and burst(), until the logic stops or the profile changes. Throws GS-API-017 outside that range. |
| `resetRate(): void` | Puts back the rate from the profile. |

### Swipe extends Component

A swipe component: each key press swipes along its path.

| Member | Description |
|---|---|
| `readonly durationMs: number` | How long the swipe takes, in milliseconds. |
| `readonly swiping: boolean` | Whether it is swiping now. |
| `swipe(): Action<void>` | Swipes along its path once. |
| `setDuration(durationMs: number): void` | Changes how long the swipe takes (10 to 10000 milliseconds) until the logic stops or the profile changes. Throws GS-API-017 outside that range. |
| `resetDuration(): void` | Puts back the swipe length from the profile. |
| `onStart(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time it puts a finger on the screen. |
| `onEnd(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time it lifts its last finger. |

### Pinch extends Component

A pinch component: two keys zoom in and out with two fingers.

| Member | Description |
|---|---|
| `readonly pinching: boolean` | Whether it is pinching now. |
| `zoomIn(): void` | Spreads two fingers apart once. |
| `zoomOut(): void` | Pinches two fingers together once. |

### Stick extends Component

A stick: WASD wheel or gamepad stick.

| Member | Description |
|---|---|
| `readonly radius: number` | The stick's radius, as a ratio of the picture height. |
| `readonly moving: boolean` | Whether the stick is pushed now. |
| `readonly direction: number \| null` | Where the stick points in degrees (0 up, 90 right, 180 down, 270 left); null when it is in the middle. |
| `readonly amount: number` | How far the stick is pushed, from 0 to 1. |
| `push(direction: number, amount?: number): void` | Pushes the stick toward a direction in degrees (0 up, 90 right, 180 down, 270 left), amount from 0 to 1 (1 by default), until release(). |
| `release(): void` | Lets the stick go back to the middle after push(). |
| `walkFor(direction: number, durationMs: number, amount?: number): Action<void>` | Pushes the stick toward a direction in degrees (0 up, 90 right, 180 down, 270 left) for a while, then lets it go. |
| `setRadius(radius: number): void` | Changes the stick's radius (0.01 to 1, a ratio of the picture height) until the logic stops or the profile changes. Throws GS-API-017 outside that range. |
| `resetRadius(): void` | Puts back the radius from the profile. |
| `onMoveStart(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time the stick starts moving. |
| `onStop(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time the stick comes back to the middle. |
| `onDirectionChange(handler: (event: DirectionEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time the stick points another way. |

### Wasd extends Stick

A WASD wheel: four keys push the stick, with walk and sprint keys.

| Member | Description |
|---|---|
| `readonly walkScale: number` | How far the walk key pushes the stick, from the profile. |
| `readonly sprintScale: number` | How far the sprint key pushes the stick, from the profile. |

### PadStick extends Stick

A gamepad stick component.

### RightWalk extends Component

A right-click walk component: a click walks the hero toward the pointer.

| Member | Description |
|---|---|
| `readonly walking: boolean` | Whether it is walking now. |
| `readonly walkMsPerUnit: number` | How long it walks to cross the picture's height, in milliseconds. |
| `readonly stopOnRelease: boolean` | Whether letting go of its key stops the walk. |
| `walkTo(x: number \| PicturePoint, y?: number): void` | Walks toward a position on the picture, as a click there would. |
| `stop(): void` | Stops walking. |
| `setRadius(radius: number): void` | Changes the radius of its stick (0.01 to 1, a ratio of the picture height) until the logic stops or the profile changes. Throws GS-API-017 outside that range. |
| `resetRadius(): void` | Puts back the radius from the profile. |
| `onWalkStart(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time it starts walking. |
| `onStop(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time it stops walking. |

### Look extends Component

A view component: the finger that turns the view.

| Member | Description |
|---|---|
| `readonly turning: boolean` | Whether its finger is turning the view now. |
| `recenter(): void` | Lifts its finger so the next turn starts from its anchor again. |
| `turn(dx: number, dy: number): void` | Turns the view as if the mouse moved dx and dy counts. |
| `setSensitivity(x: number, y?: number): void` | Changes how far the view turns for each mouse or stick count (0.01 to 100; y is x when left out) until the logic stops or the profile changes. Throws GS-API-017 outside that range. |
| `resetSensitivity(): void` | Puts back the sensitivity from the profile. |
| `onTurnStart(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time it puts a finger on the screen. |
| `onTurnEnd(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time it lifts its last finger. |

### Camera extends Look

A camera component: the mouse turns the view while it is locked.

| Member | Description |
|---|---|
| `readonly locked: boolean` | Whether the view is locked to the mouse now. |
| `lock(): void` | Locks the view to the mouse, as the lock key does. |
| `unlock(): void` | Gives the mouse back. |
| `toggleLock(): void` | Locks or unlocks the view. |
| `onLock(handler: (event: TaskContext) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time the view is locked. |
| `onUnlock(handler: (event: TaskContext) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time the view is unlocked. |

### PadCamera extends Look

A gamepad camera component: a gamepad stick turns the view.

### FreeLook extends Component

A free-look component: holding the key looks around without turning.

| Member | Description |
|---|---|
| `readonly looking: boolean` | Whether it is looking around now. |
| `begin(): void` | Starts looking around, as holding its key does. |
| `end(): void` | Stops looking around. |
| `turn(dx: number, dy: number): void` | Turns the view as if the mouse moved dx and dy counts; it only turns while it is looking around (begin() or its key). |
| `setSensitivity(x: number, y?: number): void` | Changes how far looking around turns for each mouse count (0.01 to 100; y is x when left out) until the logic stops or the profile changes. Throws GS-API-017 outside that range. |
| `resetSensitivity(): void` | Puts back the sensitivity from the profile. |
| `onStart(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time it puts a finger on the screen. |
| `onEnd(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time it lifts its last finger. |

### Skill extends Component

A skill component: press to aim, release or click to cast.

| Member | Description |
|---|---|
| `readonly aiming: boolean` | Whether it is aiming now. |
| `aim(): void` | Starts aiming, as pressing its key does. |
| `cast(): void` | Casts the skill where it is aiming. |
| `cancel(): void` | Cancels the skill. |
| `turn(dx: number, dy: number): void` | Turns the view as if the mouse moved dx and dy counts. |
| `setRadius(radius: number): void` | Changes how far the aiming finger can move from the skill's place (0.01 to 1, a ratio of the picture height) until the logic stops or the profile changes. Throws GS-API-017 outside that range. |
| `resetRadius(): void` | Puts back the radius from the profile. |
| `setSensitivity(x: number, y?: number): void` | Changes how far aiming moves for each mouse count (0.01 to 100; y is x when left out) until the logic stops or the profile changes. Throws GS-API-017 outside that range. |
| `resetSensitivity(): void` | Puts back the sensitivity from the profile. |
| `onAimStart(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time it starts aiming. |
| `onCast(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time the skill is cast. |
| `onCancel(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time the skill is cancelled. |

### Dial extends Component

A direction wheel: holding its key presses the middle of the game's wheel and the mouse picks a cell.

| Member | Description |
|---|---|
| `readonly open: boolean` | Whether the finger is down on the wheel now. |
| `readonly cell: number` | The cell the finger is on now, counted from 0 at the first cell going clockwise; -1 while it is in the middle or the key is up. |
| `readonly cells: number` | How many cells the wheel has, from the profile. |
| `choose(cell: number): Action<void>` | Presses the middle of the wheel, slides to the cell (0 is the first, going clockwise) and lets go, as if its key were pressed and the mouse moved that way. Throws GS-API-017 when the cell is not a whole number from 0 to cells - 1. |
| `onOpen(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time it puts a finger on the screen. |
| `onChoose(handler: (event: CellEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time the key is let go with the finger on a cell; the event says which. |

### MultiPress extends Component

A multi-press component: the key holds several places of the screen at once.

| Member | Description |
|---|---|
| `readonly places: number` | How many places it presses together, from 2 to 10. |
| `readonly holding: boolean` | Whether it is pressing the screen now. |
| `holdFor(durationMs: number): Action<void>` | Presses every place for a while, then lets go. |
| `toggleHold(): void` | Presses if it is up, lets go if it is pressing. |
| `onPressed(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time it puts a finger on the screen. |
| `onReleased(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time it lifts its last finger. |

### TempCursor extends Component

A temporary cursor: holding its key brings the cursor out while the view is locked.

| Member | Description |
|---|---|
| `readonly showing: boolean` | Whether it has the cursor out now: its key is held while the view is locked. |
| `onShow(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time it brings the cursor out. |
| `onHide(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time the cursor goes back and the view locks again. |

### ActionWheel extends Component

An action wheel: holding its key opens a wheel in the middle of the picture; letting go runs the cell the mouse points at.

| Member | Description |
|---|---|
| `readonly open: boolean` | Whether the wheel is open in the middle of the picture now. |
| `readonly cell: number` | The cell the mouse points at now, counted from 0 at the top going clockwise; -1 while it points at the middle or the wheel is closed. |
| `readonly cells: number` | How many cells the wheel has, from the profile. |
| `readonly names: readonly string[]` | The names of the cells, from the profile; empty for a cell without a name. |
| `choose(cell: number): Action<void>` | Opens the wheel, points at the cell (0 is the top, going clockwise) and lets go, so the cell runs as if its key were used. Throws GS-API-017 when the cell is not a whole number from 0 to cells - 1. |
| `onOpen(handler: (event: ControlEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time its key opens the wheel. |
| `onChoose(handler: (event: WheelChoice) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time the key is let go while pointing at a cell; the event says which cell and its name. |

### Variable

A variable of the profile in use, such as "on a boat" or "aiming".

| Member | Description |
|---|---|
| `readonly id: string` | The variable id from the profile. |
| `readonly name: string` | The name shown in the editor. |
| `value: VarValue` | The current value: true or false, a number or a string. Setting it changes what controls and logic see; throws GS-API-010 for other values or when the profile no longer has the variable. |
| `toggle(): void` | Flips a true or false value. |
| `onChange(handler: (event: VarChange) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time the value changes, whoever changed it. |

### Key

A key, mouse button or gamepad button. Listening to it in a profile's logic takes it for the script: it no longer goes to the phone as a key, and mouse buttons count only while the view is locked. A task listening to it hears it without taking it, so the profile's keys and logic still get it.

| Member | Description |
|---|---|
| `readonly name: InputName` | The key's name, such as "KeyF". |
| `readonly isDown: boolean` | Whether the key is held down now. |
| `readonly pressedAt: number \| null` | When the key went down (time.now()), or null when it is up. |
| `readonly heldMs: number` | How long the key has been held, in milliseconds; 0 when it is up. |
| `onPress(handler: (event: KeyEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time the key goes down. |
| `onRelease(handler: (event: KeyEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time the key comes up. |
| `whileHeld(handler: (event: KeyEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task when the key goes down and cancels it when the key comes up. |
| `onTap(handler: (event: KeyEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task when the key is let go within 250 ms of pressing it. |
| `onDoubleTap(handler: (event: KeyEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task when the key is pressed again within 300 ms of a tap. |
| `onLongPress(durationMs: number, handler: (event: KeyEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task when the key has been held for durationMs (500 by default). |

### Globals

Values shared by every profile and task on this phone until the program closes.

| Member | Description |
|---|---|
| `get(name: string): StoreValue \| undefined` | Reads a global value, shared by every profile and task on this phone. |
| `set(name: string, value: StoreValue): void` | Sets a global value; it lasts until the program closes. |
| `onChange(name: string, handler: (event: GlobalChange) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time a global value changes. |

### Gate

A named gate from gate().

| Member | Description |
|---|---|
| `readonly isOpen: boolean` | Whether the gate is open. |
| `open(): void` | Opens the gate. |
| `close(): void` | Closes the gate. |
| `toggle(): void` | Opens or closes the gate. |
| `run<T>(action: () => T): T \| undefined` | Runs the action when the gate is open. |

### Stopwatch

A stopwatch from stopwatch().

| Member | Description |
|---|---|
| `readonly elapsedMs: number` | Milliseconds since it started or was reset. |
| `reset(): void` | Starts again from 0. |
| `lap(): number` | Milliseconds since the last lap, or since it started. |

### Finger

A finger the script put down with touch.down().

| Member | Description |
|---|---|
| `readonly down: boolean` | Whether the finger is still down. |
| `move(x: number \| PicturePoint, y?: number, durationMs?: number): Action<void>` | Moves the finger to a position, at once or over durationMs. |
| `up(): void` | Lifts the finger. |

### CapturedImage

Part of the picture taken by vision.capture().

| Member | Description |
|---|---|
| `readonly region: Region` | Where it was taken, as ratios of the picture. |
| `readonly width: number` | Its width in pixels. |
| `readonly height: number` | Its height in pixels. |
| `save(name: string): Promise<void>` | Keeps it as a template of the script's folder: res/<name>.png and an entry in res/resources.json, replacing the picture of a template with that name; resolves once both are written. Logic keeps running. Throws GS-API-038 when the name is not allowed; rejects with GS-API-038 when the files cannot be written and GS-API-037 when the image is no longer kept. |

### Task

A task: logic that runs on its own on a phone, started from the Tasks page or the mirror window's task menu. It keeps running when the profile changes.

| Member | Description |
|---|---|
| `readonly params: TaskParams` | The settings chosen for this task on the Tasks page, or the values it declares in static params. |
| `progress(text: string): void` | Shows how far the task has come on its card and in the mirror window's task menu. |
| `run(context: TaskContext): Promise<void>` | What the task does once it starts. When it returns the task is done; an error ends it as failed; Stop cancels it. |

### ComponentEvent

An event a component made in logic emits; others listen with on().

| Member | Description |
|---|---|
| `emit(value?: unknown): void` | Starts a task in each listener with { value }. |
| `on(handler: (event: { readonly value: unknown }) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time the event is emitted. |

### ComponentFinger

The own finger of a component made in logic.

| Member | Description |
|---|---|
| `readonly isDown: boolean` | Whether the finger is down. |
| `down(x: number \| PicturePoint, y?: number): Finger` | Puts the finger down at a position, lifting it first when it is already down; it stays down after the task ends. |
| `move(x: number \| PicturePoint, y?: number, durationMs?: number): Action<void>` | Moves the finger, at once or over durationMs. |
| `up(): void` | Lifts the finger. |

### ComponentClass

The Component base class that components made in logic extend.

| Member | Description |
|---|---|
| `new (options?: ComponentOptions): Component` | Makes a component of a class that extends Component, with values for its static props; then calls its setup(). |
| `readonly prototype: Component` | The members every component has. |
| `readonly implements?: readonly Interface<unknown>[]` | The interfaces the components of a class implement, written static implements = [Fire]; the classes that extend it implement them too. |

### ComponentOverrides

The methods a class that extends a built-in component can write; each event one runs when that event comes.

| Member | Description |
|---|---|
| `setup(): void` | Runs once after the component is created; set up its listeners here. |
| `onPress(event?: ControlEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |
| `onRelease(event?: ControlEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |
| `whileHeld(event?: ControlEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |
| `onEnabled(event?: ControlEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |
| `onDisabled(event?: ControlEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |
| `onTouchStart(event?: ControlEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |
| `onTouchEnd(event?: ControlEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |

### HoldOverrides extends ComponentOverrides

The events a class that extends Hold can take on as well.

| Member | Description |
|---|---|
| `onPressed(event?: ControlEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |
| `onReleased(event?: ControlEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |

### StartEndOverrides extends ComponentOverrides

The events a class that extends LongPress, Swipe or FreeLook can take on as well.

| Member | Description |
|---|---|
| `onStart(event?: ControlEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |
| `onEnd(event?: ControlEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |

### StickOverrides extends ComponentOverrides

The events a class that extends Wasd or PadStick can take on as well.

| Member | Description |
|---|---|
| `onMoveStart(event?: ControlEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |
| `onStop(event?: ControlEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |
| `onDirectionChange(event?: DirectionEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |

### RightWalkOverrides extends ComponentOverrides

The events a class that extends RightWalk can take on as well.

| Member | Description |
|---|---|
| `onWalkStart(event?: ControlEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |
| `onStop(event?: ControlEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |

### LookOverrides extends ComponentOverrides

The events a class that extends PadCamera can take on as well.

| Member | Description |
|---|---|
| `onTurnStart(event?: ControlEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |
| `onTurnEnd(event?: ControlEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |

### CameraOverrides extends LookOverrides

The events a class that extends Camera can take on as well.

| Member | Description |
|---|---|
| `onLock(event?: TaskContext): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |
| `onUnlock(event?: TaskContext): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |

### SkillOverrides extends ComponentOverrides

The events a class that extends Skill can take on as well.

| Member | Description |
|---|---|
| `onAimStart(event?: ControlEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |
| `onCast(event?: ControlEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |
| `onCancel(event?: ControlEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |

### DialOverrides extends ComponentOverrides

The events a class that extends Dial can take on as well.

| Member | Description |
|---|---|
| `onOpen(event?: ControlEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |
| `onChoose(event?: CellEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |

### TempCursorOverrides extends ComponentOverrides

The events a class that extends TempCursor can take on as well.

| Member | Description |
|---|---|
| `onShow(event?: ControlEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |
| `onHide(event?: ControlEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |

### ActionWheelOverrides extends ComponentOverrides

The events a class that extends ActionWheel can take on as well.

| Member | Description |
|---|---|
| `onOpen(event?: ControlEvent): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |
| `onChoose(event?: WheelChoice): void \| Promise<void>` | Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing. |

### TaskClass

The Task base class that a task's default export extends.

| Member | Description |
|---|---|
| `new (): Task` | GameScrcpy makes the task when it starts; scripts do not create tasks themselves. |
| `readonly prototype: Task` | The members every task has. |

### UiPanel

A panel of an extension from ui.panel(), drawn with GameScrcpy's own controls.

| Member | Description |
|---|---|
| `render(tree: UiNode \| null): void` | Shows the tree in the panel in place of what it showed; null empties it. GameScrcpy draws at most 30 trees a second, so of quicker ones the last shows. Throws GS-API-075 for a tree that is not right, naming the node, and GS-API-076 when the extension does not declare the panel for this logic. |
| `on(type: UiEventType, handler: (event: UiEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time the user does something in the panel: click on a button; change of a field, number, toggle, choice or slider; submit with Enter in a field; select of a list item; open and close of the panel. Throws GS-API-017 for another event. |
| `close(): void` | Closes the mirror window's side panel when it shows this panel. |

### UiStatus

A status widget of an extension from ui.status().

| Member | Description |
|---|---|
| `render(tree: UiStatusNode \| null): void` | Shows the widget in place of what it showed; null takes it away. Throws GS-API-075 for a tree that is not right and GS-API-076 when the extension does not declare the widget for this logic. |

### UiToolbar

A toolbar button of an extension from ui.toolbar().

| Member | Description |
|---|---|
| `set(state: UiToolbarState \| null): void` | Changes what the button shows; null goes back to what the manifest declares. Throws GS-API-075 for a state that is not right and GS-API-076 when the extension does not declare the button for this logic. |
| `on(type: "click", handler: (event: UiEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time the button is clicked. Throws GS-API-017 for another event. |

### UiContextMenu

A menu of an extension on cards, from ui.contextMenu().

| Member | Description |
|---|---|
| `set(items: readonly UiMenuItem[] \| null): void` | Puts up to 16 items into the More menu of the cards in place of the last ones; null or an empty list takes them away. Throws GS-API-075 for items that are not right and GS-API-076 when the extension does not declare the menu for this logic. |
| `on(type: "choose", handler: (event: UiChooseEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time one of the items is picked; event.target says on which card. Throws GS-API-017 for another event. |

### UiTray

A group of an extension in the tray menu, from ui.tray().

| Member | Description |
|---|---|
| `set(items: readonly UiMenuItem[] \| null): void` | Puts up to 16 items into the tray menu in place of the last ones; null or an empty list takes them away. Throws GS-API-075 for items that are not right and GS-API-076 when the extension does not declare the group for this logic. |
| `on(type: "choose", handler: (event: UiEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task each time one of the items is picked. Throws GS-API-017 for another event. |

### Controls

The components of the profile in use, by id. GameScrcpy writes the members for each profile into types/profile.d.ts; use mapping for ids that are only known at run time.

### Vars

The variables of the profile in use, by id; the members come from types/profile.d.ts.

### TemplateNames

The template names of the profile's res/resources.json; GameScrcpy writes the members into types/profile.d.ts.

### RegionNames

The region names of the profile's res/resources.json; the members come from types/profile.d.ts.

### PointNames

The point names of the profile's res/resources.json; the members come from types/profile.d.ts.

### PathNames

The path names of the profile's res/resources.json; the members come from types/profile.d.ts.

### ColorNames

The color names of the profile's res/resources.json; the members come from types/profile.d.ts.

### Extensions

The system nodes of the installed native extensions, by extension; GameScrcpy writes the members into types/extensions.d.ts.

| Member | Description |
|---|---|
| `call<T = StoreValue>(service: string, args?: StoreValue): Promise<T>` | Calls a service of an extension, named "<extension id>/<service>" as its manifest declares it in contributes.services, with arguments that its input schema accepts, and gives the result, which its output schema checks. The logic of an extension may call the services of the extensions it lists in dependencies, optional ones too, and its own; your own logic may call any service of an extension that is on. Cancelling the task cancels the call. Throws GS-API-146 when no extension that is on offers the service or its app script has not provided it within 10 seconds, GS-API-147 when this extension does not depend on the one that offers it, GS-API-148 when the arguments or the result do not match the schemas or are not JSON of at most 252 KiB, and GS-API-149 with the service's own message when it fails, or when it takes longer than 10 seconds. |
| `provide<A = any>(name: string, handler: (args: A, context: ServiceContext) => StoreValue \| void \| Promise<StoreValue \| void>): Subscription` | Answers the calls of a service that the extension declares in contributes.services with this app script as its script. Each call runs as a task of its own with the permissions that both the calling extension and this one have; what the handler returns is the result (nothing gives null), and when it throws the call fails with its message. Providing the same name again replaces the handler; dispose() stops answering. Throws GS-API-145 when the extension does not declare the service for this script, and GS-API-008 when the handler is not a function. |

### HardwareDevice

A device opened with hardware.open(). Every kind below has these members too.

| Member | Description |
|---|---|
| `readonly id: string` | The id of the device. |
| `readonly kind: HardwareKind` | The kind of device. |
| `readonly name: string` | The name of the device, as in the device list. |
| `readonly isOpen: boolean` | Whether it is still open. |
| `close(): Promise<void>` | Closes the device, so that it can be opened again. |
| `onClose(handler: (event: DeviceCloseEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task when the device closes by itself: it was unplugged, failed or the permission was taken back. |

### SerialDevice extends HardwareDevice

An open serial port.

| Member | Description |
|---|---|
| `readonly kind: "serial"` | Always "serial". |
| `write(data: HardwareBytes \| string): Promise<void>` | Sends bytes, or text as UTF-8, up to 65536 bytes at a time; resolves once they are sent. |
| `setSignals(signals: SerialSignals): Promise<void>` | Turns the DTR and RTS lines on or off. |
| `onData(handler: (event: DeviceDataEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task for each piece of data that comes in; by default the events queue, so none is lost. |

### HidDevice extends HardwareDevice

An open USB HID device. Keyboards, mice and keypads are never offered.

| Member | Description |
|---|---|
| `readonly kind: "hid"` | Always "hid". |
| `sendReport(report: number, data: HardwareBytes): Promise<void>` | Sends an output report: its id, 0 when the device numbers none, and the bytes after it. |
| `sendFeature(report: number, data: HardwareBytes): Promise<void>` | Sends a feature report: its id and the bytes after it. |
| `getFeature(report: number): Promise<Uint8Array>` | Reads a feature report; resolves with the bytes after its id. |
| `onReport(handler: (event: DeviceReportEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task for each input report; by default the events queue, so none is lost. |

### BleDevice extends HardwareDevice

An open Bluetooth LE device; it stays connected while it is open.

| Member | Description |
|---|---|
| `readonly kind: "ble"` | Always "ble". |
| `services(): Promise<readonly BleService[]>` | The services and characteristics the device offers. |
| `read(service: string, characteristic: string): Promise<Uint8Array>` | Reads a characteristic. A UUID may be written short, such as "180f". |
| `write(service: string, characteristic: string, data: HardwareBytes, options?: BleWriteOptions): Promise<void>` | Writes up to 512 bytes to a characteristic. |
| `subscribe(service: string, characteristic: string, handler: (event: DeviceNotifyEvent) => void \| Promise<void>, options?: ListenOptions): Promise<Subscription>` | Turns on the notifications of a characteristic and starts a task for each new value; resolves once they are on. Disposing the last subscription of the characteristic turns them off; by default the events queue, so none is lost. |

### InputSource

Keys and axes that logic sends into the phone's key mapping, made by input.source().

| Member | Description |
|---|---|
| `readonly name: string` | The name of the input source. |
| `press(key: string): void` | Presses a key of the source, named with 1 to 32 letters, digits, _ or -; it stays down until release() or until the logic stops. |
| `release(key: string): void` | Lets go of a key of the source. |
| `axis(key: string, x: number, y?: number): void` | Moves an axis of the source to x and y, each from -1 to 1 (y is 0 when left out); bound to a stick it pushes the stick, and 0, 0 lets it go. |

### NetServer

An HTTP server on 127.0.0.1 made by net.listen().

| Member | Description |
|---|---|
| `readonly port: number` | The port it listens on. |
| `readonly isOpen: boolean` | Whether it still takes requests. |
| `close(): void` | Stops taking requests and frees the port. |

### SpeechListener

Voice commands listened for with speech.listen().

| Member | Description |
|---|---|
| `readonly isOpen: boolean` | Whether it still listens. |
| `close(): void` | Stops listening; the microphone is let go once nothing else listens. |

### VisionEngine

A recognition engine from vision.engine().

| Member | Description |
|---|---|
| `readonly id: string` | The id of the engine. |
| `read(region?: Region \| RegionName, params?: VisionParams): Promise<readonly VisionText[] \| null>` | Reads text, as vision.recognize(id, "ocr", region, params) does. |
| `detect(region?: Region \| RegionName, params?: VisionParams): Promise<readonly VisionObject[] \| null>` | Finds things, as vision.recognize(id, "detect", region, params) does. |
| `classify(region?: Region \| RegionName, params?: VisionParams): Promise<readonly VisionClass[] \| null>` | Tells what the picture shows, as vision.recognize(id, "classify", region, params) does. |

### NetSocket

A WebSocket connection made by net.socket().

| Member | Description |
|---|---|
| `readonly url: string` | The address it is connected to. |
| `readonly protocol: string` | The subprotocol the server took, or an empty text. |
| `readonly isOpen: boolean` | Whether messages can still be sent. |
| `send(data: string \| HardwareBytes): void` | Sends a text message, or a binary one for bytes, of at most 1 MiB. Throws GS-API-159 when the socket is closed and GS-API-157 when 16 MiB are waiting to be sent already. |
| `close(code?: number, reason?: string): void` | Closes the socket with code 1000, or one from 3000 to 4999, and a reason of at most 123 bytes; onClose does not run for it. |
| `onMessage(handler: (event: NetMessageEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task for each message; by default the messages queue, so none is lost. |
| `onClose(handler: (event: NetCloseEvent) => void \| Promise<void>, options?: ListenOptions): Subscription` | Starts a task when the server or the network closes the socket. |

### FileHandle

A file the user picked with files.pick().

| Member | Description |
|---|---|
| `readonly name: string` | The name of the file, without its folder. |
| `readonly isOpen: boolean` | Whether the handle can still be used. |
| `read(): Promise<Uint8Array>` | Reads the whole file, up to 64 MiB. Throws GS-API-162 when Windows cannot read it or it changes while it is read, GS-API-163 when it is larger and GS-API-164 when the handle is closed. |
| `text(): Promise<string>` | Reads the whole file as UTF-8 text, leaving out a byte order mark. |
| `write(data: string \| HardwareBytes): Promise<void>` | Replaces the whole file with the text, as UTF-8, or the bytes, up to 64 MiB; a file the user named in the save dialog is made at the first write. |
| `close(): void` | Gives up the file; the handle can no longer be used. |

### FolderHandle

A folder chosen for files.folder(). Paths inside it use / between names; .., drives and links that lead out of it are refused with GS-API-161, and what Windows cannot do with GS-API-162.

| Member | Description |
|---|---|
| `readonly name: string` | The name the manifest gives the folder. |
| `readonly isOpen: boolean` | Whether the handle can still be used. |
| `list(path?: string): Promise<readonly FolderEntry[]>` | Lists up to 4096 files and folders, sorted by name, at the top of the folder or in a folder inside it. |
| `read(path: string): Promise<Uint8Array>` | Reads a file inside the folder, up to 64 MiB. |
| `text(path: string): Promise<string>` | Reads a file inside the folder as UTF-8 text, leaving out a byte order mark. |
| `write(path: string, data: string \| HardwareBytes): Promise<void>` | Replaces or makes a file inside the folder, with the folders on its path, up to 64 MiB. |
| `remove(path: string): Promise<boolean>` | Removes a file, or a folder with nothing in it; resolves with false when there was nothing to remove. |
| `close(): void` | Gives up the folder; the handle can no longer be used. |

## Types

### AbortSignal

Tells a task that it was cancelled: when its key is released, the script stops or the profile changes.

```ts
type AbortSignal = { readonly aborted: boolean; readonly reason: unknown; onabort: (() => void) | null; throwIfAborted(): void; addEventListener(type: "abort", listener: () => void): void; removeEventListener(type: "abort", listener: () => void): void };
```

### TaskContext

What every exported handler receives when a key, a blueprint or the console starts it.

```ts
type TaskContext = { readonly signal: AbortSignal };
```

### TaskParams

The settings of a running task, as chosen on the Tasks page; the task declares them in static params.

```ts
type TaskParams = { readonly [name: string]: any };
```

### TaskParam

A task setting made by param.number, param.text or param.toggle.

```ts
type TaskParam = { readonly kind: "number" | "text" | "toggle"; readonly value: number | string | boolean; readonly label?: string; readonly min?: number; readonly max?: number };
```

### ComponentOptions

The values for a new component's static props, with an optional id and whether it starts on.

```ts
type ComponentOptions = { readonly id?: string; readonly enabled?: boolean; readonly [prop: string]: unknown };
```

### ComponentProp

A property declared in static props with prop.key, prop.position, prop.number, prop.text, prop.toggle or prop.component.

```ts
type ComponentProp = { readonly kind: "key" | "position" | "number" | "text" | "toggle" | "component"; readonly value: unknown; readonly label?: string };
```

### PropOptions

How a component property is labelled.

```ts
type PropOptions = { label?: string };
```

### McpToolSpec

What an MCP tool of an extension does, in a sentence or two for the AI, and the JSON Schema of its input: an object of type object of at most 16 KiB; without one the tool takes an empty object.

```ts
type McpToolSpec = { readonly description: string; readonly input?: { readonly [key: string]: unknown } };
```

### NumberPropOptions

How a number property is labelled, and the smallest and largest value its box in the property panel takes.

```ts
type NumberPropOptions = { label?: string; min?: number; max?: number };
```

### ParamOptions

How a task setting shows on the Tasks page: its label, and for numbers the smallest and largest value.

```ts
type ParamOptions = { label?: string; min?: number; max?: number };
```

### AndroidKey

The Android keys a script can press.

```ts
type AndroidKey = "back" | "home" | "appSwitch" | "menu" | "power" | "volumeUp" | "volumeDown" | "volumeMute" | "enter" | "delete" | "tab" | "space" | "escape" | "up" | "down" | "left" | "right" | "pageUp" | "pageDown" | "search" | "camera" | "mediaPlayPause" | "notifications" | "settings";
```

### DeviceInfo

The phone: model name, Android version, screen size in pixels and rotation in quarter turns.

```ts
type DeviceInfo = { readonly model: string; readonly android: number; readonly width: number; readonly height: number; readonly rotation: 0 | 1 | 2 | 3 };
```

### ComponentType

The 13 kinds of component.

```ts
type ComponentType = "tap" | "hold" | "longPress" | "turbo" | "swipe" | "pinch" | "wasd" | "rightWalk" | "padStick" | "camera" | "padCamera" | "freeLook" | "skill";
```

### MachineStep

A move to another state, made by to.<state>(reason) and returned from a state of machine().

```ts
type MachineStep = { readonly state: string; readonly reason: string };
```

### MachineTo<S>

What a state of machine() receives: one function per state that makes the move to it, with an optional reason the editor shows on the transition.

```ts
type MachineTo<S> = { readonly [K in S & string]: (reason?: string) => MachineStep };
```

### MachineOptions<S>

The name the editor shows for a state machine, and the state it starts in (the first one when left out).

```ts
type MachineOptions<S> = { name?: string; start?: NoInfer<S & string> };
```

### Interface<T>

An interface made with defineInterface(): its name, and all(), every component of the profile whose class lists it in static implements, so logic can tell each of them at once.

```ts
type Interface<T> = { readonly name: string; all(): (Component & T)[] };
```

### BuiltInClass<T, O>

A built-in component class. A component of yours that extends it is placed as that built-in component and presses the screen just as it does; the event methods of O that the class writes run each time the event comes, as well. T is the type its components have in controls.

```ts
type BuiltInClass<T, O> = { new (): Omit<T, keyof O> & O; readonly component?: T; readonly implements?: readonly Interface<unknown>[] };
```

### ControlInfo

A control of the profile in use.

```ts
type ControlInfo = { readonly id: string; readonly name: string; readonly type: ComponentType; readonly enabled: boolean };
```

### VarValue

What a variable holds.

```ts
type VarValue = boolean | number | string;
```

### StoreValue

A value the store can keep.

```ts
type StoreValue = null | boolean | number | string | StoreValue[] | { [key: string]: StoreValue };
```

### ExtensionJob<T>

A job that a native extension runs, such as writing firmware. result settles when it ends and throws GS-API-065 when it fails and GS-API-066 when it is cancelled; onProgress hears how far it got, fraction from 0 to 1 or -1 when it cannot tell, at most 20 times a second; onLog hears its lines, level 0 information, 1 warning, 2 error; onOutput hears the parts of its result that it hands over while it runs, such as each device a scan finds; follow hears the progress, waits for the job and then runs done with the result or failed with the error, and without failed a failure throws as result does; cancel asks it to stop. It is cancelled when the script stops.

```ts
type ExtensionJob<T> = { readonly result: Promise<T>; onProgress(listener: (fraction: number, text: string) => void): ExtensionJob<T>; onLog(listener: (level: number, text: string) => void): ExtensionJob<T>; onOutput(listener: (value: StoreValue) => void): ExtensionJob<T>; follow(progress?: (fraction: number, text: string) => void | Promise<void>, done?: (result: T) => void | Promise<void>, failed?: (error: Error) => void | Promise<void>): Promise<void>; cancel(): void };
```

### ServiceContext

What the handler of a service gets besides the arguments: the id of the extension that calls it ("" for the user's own logic), the permissions the call runs with, which are those that both extensions have, and the signal that aborts when the caller cancels or the call takes longer than 10 seconds.

```ts
type ServiceContext = TaskContext & { readonly caller: string; readonly permissions: readonly string[] };
```

### ListenOptions

What happens when the event comes again while the last task it started is still running: ignore it (default), restart (cancel the running task first), stop (only cancel the running task), queue (run after it) or parallel (run both).

```ts
type ListenOptions = { readonly again?: "ignore" | "restart" | "stop" | "queue" | "parallel" };
```

### KeyListenOptions

The options of a control's key events: again as for other events, and consume. With consume true (the default), the profile's logic takes the key of a tap, hold, long press, turbo, swipe or multi-press control it listens to: pressing the key no longer touches the phone, and the control touches only when the logic calls it, such as with press() or tap(), a few milliseconds later than its own touch would. consume: false keeps the control's own touch, which comes before the handler runs. Only the profile's own logic takes keys: tasks, extension scripts and scripts an AI client runs only listen, the event methods a component class overrides keep the built-in touch, and other controls always keep working.

```ts
type KeyListenOptions = ListenOptions & { readonly consume?: boolean };
```

### Subscription

A listener; dispose() stops it. Listeners made inside a task stop when that task ends, those made at module top level last as long as the script.

```ts
type Subscription = { dispose(): void };
```

### UntilOptions

How long until() waits for its condition after each try of the action, in milliseconds (5000 by default, from 1 to 600000), and how many more times it does the action when the condition does not come true in time (0 by default, at most 10).

```ts
type UntilOptions = { readonly timeoutMs?: number; readonly retry?: number };
```

### Action<T>

What an action returns: a Promise that resolves when the action is done, with until(), which then checks the condition once per frame and resolves with its first truthy value, doing the action again, as often as retry allows, when it does not come true in time. The condition may return a Promise, as vision.find() does. until() rejects with a TimeoutError (GS-API-030) when the tries run out and with an AbortError when the task is cancelled; it throws GS-API-008 for a condition that is not a function, GS-API-011 for options that are not an object, GS-API-003 for a timeoutMs out of range and GS-API-017 for a retry out of range.

```ts
type Action<T> = Promise<T> & { until<R>(condition: () => R | Promise<R>, options?: UntilOptions): Promise<NonNullable<Awaited<R>>> };
```

### Binding

A binding made with bind(); dispose() stops it and puts the property back to its value from before the binding. Bindings made inside a task stop when that task ends, those made at module top level last as long as the script.

```ts
type Binding = { dispose(): void };
```

### ControlEvent

What a control listener receives: the cancel signal and the id of the control.

```ts
type ControlEvent = TaskContext & { readonly control: string };
```

### DirectionEvent

What onDirectionChange receives: the new direction in degrees.

```ts
type DirectionEvent = ControlEvent & { readonly direction: number };
```

### CellEvent

What a direction wheel's onChoose receives: the cell picked, counted from 0 at the first cell going clockwise.

```ts
type CellEvent = ControlEvent & { readonly cell: number };
```

### WheelChoice

What an action wheel's onChoose receives: the cell run, counted from 0 at the top going clockwise, and its name.

```ts
type WheelChoice = ControlEvent & { readonly cell: number; readonly name: string };
```

### VarChange

What a variable listener receives: the cancel signal, the variable id and its new value.

```ts
type VarChange = TaskContext & { readonly id: string; readonly value: VarValue };
```

### KeyEvent

What a key listener receives: the key's name, and on release how long it was held.

```ts
type KeyEvent = TaskContext & { readonly key: InputName; readonly heldMs?: number };
```

### Keys

Keys by name, as in profiles ("KeyF", "Space", "Mouse.Left") or short (F, Esc, MouseLeft, WheelUp, GamepadA).

```ts
type Keys = { readonly [name: string]: Key };
```

### GlobalChange

What a global listener receives: the name and the new value.

```ts
type GlobalChange = TaskContext & { readonly name: string; readonly value: StoreValue };
```

### ForegroundApp

The app in front on the phone: its package and whether Android calls it a game.

```ts
type ForegroundApp = { readonly package: string; readonly game: boolean };
```

### InputBox

An input box on the phone: whether it is open, its kind as Android's InputType number (1 text, 2 number, 3 phone, 4 date and time, with the variation and flag bits added), what its Enter key does and whether it takes several lines. When it closes the other fields are 0, "unspecified" and false.

```ts
type InputBox = { readonly shown: boolean; readonly inputType: number; readonly action: "unspecified" | "none" | "go" | "search" | "send" | "next" | "done" | "previous"; readonly multiLine: boolean };
```

### LaunchOptions

Whether to stop the app before starting it.

```ts
type LaunchOptions = { readonly restart?: boolean };
```

### ProfileInfo

A profile's id and name.

```ts
type ProfileInfo = { readonly id: string; readonly name: string };
```

### Work

Something to run: a Promise, or a function to start.

```ts
type Work = Promise<unknown> | (() => unknown);
```

### ConfirmOptions

The words on the two buttons of a yes or no question (up to 60 characters each).

```ts
type ConfirmOptions = { readonly ok?: string; readonly cancel?: string };
```

### AskOptions

The text already in the box when the question opens.

```ts
type AskOptions = { readonly initial?: string };
```

### PicturePoint

A position on the phone picture as ratios: (0, 0) top left, (1, 1) bottom right.

```ts
type PicturePoint = { readonly x: number; readonly y: number };
```

### InputName

A key or mouse button, written as in profiles, such as "KeyW", "ShiftLeft" or "Mouse.Right".

```ts
type InputName = string;
```

### DrawColor

The colors a drawing can use; they follow the program's theme.

```ts
type DrawColor = "accent" | "good" | "warn" | "bad" | "white";
```

### BoxOptions

A box's color (accent by default), a label of up to 60 characters and how long it stays, in milliseconds.

```ts
type BoxOptions = { readonly color?: DrawColor; readonly label?: string; readonly durationMs?: number };
```

### TextOptions

The text's color (accent by default) and how long it stays, in milliseconds.

```ts
type TextOptions = { readonly color?: DrawColor; readonly durationMs?: number };
```

### MarkOptions

A mark's color (accent by default), a label of up to 60 characters shown to the right of the ring and how long it stays, in milliseconds.

```ts
type MarkOptions = { readonly color?: DrawColor; readonly label?: string; readonly durationMs?: number };
```

### Drawing

Something drawn on the picture; dispose() removes it. At most 64 are shown at a time.

```ts
type Drawing = { dispose(): void };
```

### Region

Part of the picture, as ratios: left, top, width and height.

```ts
type Region = { readonly x: number; readonly y: number; readonly width: number; readonly height: number };
```

### FindOptions

Where to look (a region or a region name; the whole picture by default), how alike it must be from 0 to 1 (0.8 by default), how to compare (grey by default), how many to return for findAll (16 by default, at most 64) and how long to wait while there is no picture yet (2000 ms by default, at most 10000). A template name brings its own threshold, match mode and region; options given here win.

```ts
type FindOptions = { readonly region?: Region | RegionName; readonly threshold?: number; readonly match?: MatchMode; readonly max?: number; readonly timeoutMs?: number };
```

### Found

Where a picture was found: x and y are its center and width and height its size, as ratios of the picture; score is how alike it is, from 0 to 1.

```ts
type Found = { readonly x: number; readonly y: number; readonly width: number; readonly height: number; readonly score: number };
```

### WaitOptions

Where to look and how alike it must be, as for find(); timeoutMs is how long to keep looking in all (10000 ms by default, at most 600000) and intervalMs how long to wait between looks (250 ms by default, from 50 to 60000).

```ts
type WaitOptions = FindOptions & { readonly intervalMs?: number };
```

### WatchOptions

Where to look and how alike it must be, as for find(); intervalMs is how long to wait between looks (500 ms by default, from 50 to 60000), and again what happens when the picture comes or goes again while the last task it started is still running.

```ts
type WatchOptions = FindOptions & ListenOptions & { readonly intervalMs?: number };
```

### MatchMode

How a template is compared. "grey" compares brightness only (zero-mean normalized cross-correlation). "color" also compares colors: the score is multiplied by 1 - d / 128 (at least 0), where d is the mean absolute difference of red, green and blue (0 to 255) over the template's opaque pixels, so the same shape in another color scores low.

```ts
type MatchMode = "color" | "grey";
```

### ColorOptions

Where to look (the whole picture by default), how far each of red, green and blue may be from the color as a ratio of 255 (0.1 by default) and how long to wait while there is no picture yet (2000 ms by default, at most 10000). A color name brings its own tolerance and region; options given here win.

```ts
type ColorOptions = { readonly region?: Region | RegionName; readonly tolerance?: number; readonly timeoutMs?: number };
```

### PixelOptions

How long to wait while there is no picture yet (2000 ms by default, at most 10000).

```ts
type PixelOptions = { readonly timeoutMs?: number };
```

### ChangeOptions

How much must change to count, as the share of the region from 0 to 1 (0.05 by default; a spot counts when its brightness moves by about a tenth or more), and how long to wait while there is no picture yet (2000 ms by default, at most 10000).

```ts
type ChangeOptions = { readonly threshold?: number; readonly timeoutMs?: number };
```

### OnChangeOptions

How much must change, as for changed(); intervalMs is how long to wait between looks (500 ms by default, from 50 to 60000), and again what happens when it changes again while the last task it started is still running.

```ts
type OnChangeOptions = ListenOptions & { readonly threshold?: number; readonly intervalMs?: number };
```

### ReadTextOptions

Where to read (a region or a region name; the whole picture by default), the language as a tag such as "zh-Hans" or "en" (by default the first Windows display language that has text recognition, otherwise the first one this computer has; findText looks for text of English letters, digits and signs with English recognition when the computer has it) and how long to wait while there is no picture yet (2000 ms by default, at most 10000).

```ts
type ReadTextOptions = { readonly region?: Region | RegionName; readonly language?: string; readonly timeoutMs?: number };
```

### TextResult

Text read from the picture: text is every line joined with line breaks, lines each line with its place from top to bottom, and language the tag of the language it was read in, such as "zh-Hans-CN".

```ts
type TextResult = { readonly text: string; readonly lines: readonly TextLine[]; readonly language: string };
```

### TextLine

One line of text: x and y are its center and width and height its size, as ratios of the picture.

```ts
type TextLine = { readonly text: string; readonly x: number; readonly y: number; readonly width: number; readonly height: number };
```

### PixelMatchOptions

How far each of red, green and blue may be from the color as a ratio of 255 (0.1 by default; a color name brings its own) and how long to wait while there is no picture yet (2000 ms by default, at most 10000).

```ts
type PixelMatchOptions = { readonly tolerance?: number; readonly timeoutMs?: number };
```

### PicturePath

A path on the picture: its points as ratios and how long a swipe along it takes, in milliseconds.

```ts
type PicturePath = { readonly points: readonly PicturePoint[]; readonly durationMs: number };
```

### ImagePath

A picture in the profile folder, written as a relative path such as "res/chest.png".

```ts
type ImagePath = `${string}.png` | `${string}.jpg` | `${string}.jpeg` | `${string}.bmp` | `${string}.PNG` | `${string}.JPG` | `${string}.JPEG` | `${string}.BMP`;
```

### HexColor

A color written "#RRGGBB", such as "#F5C542".

```ts
type HexColor = `#${string}`;
```

### TemplateName

The name of a template in res/resources.json.

```ts
type TemplateName = keyof TemplateNames;
```

### RegionName

The name of a region in res/resources.json.

```ts
type RegionName = keyof RegionNames;
```

### PointName

The name of a point in res/resources.json.

```ts
type PointName = keyof PointNames;
```

### PathName

The name of a path in res/resources.json.

```ts
type PathName = keyof PathNames;
```

### ColorName

The name of a color in res/resources.json.

```ts
type ColorName = keyof ColorNames;
```

### UiEventType

What the user does in a panel.

```ts
type UiEventType = "click" | "change" | "submit" | "select" | "open" | "close";
```

### UiEvent

What a panel or menu listener receives: the id of the node or item, and its new value or null.

```ts
type UiEvent = TaskContext & { readonly id: string; readonly value: unknown };
```

### UiTone

The colors of text and icons in a panel; they follow the program's theme.

```ts
type UiTone = "normal" | "muted" | "accent" | "good" | "warn" | "bad";
```

### UiColor

A color in a chart or canvas: "#RRGGBB", or the name of a theme color such as "accent", "textPrimary" or "success".

```ts
type UiColor = string;
```

### UiNode

A node of a panel tree. A tree is at most 256 KiB as JSON, 2000 nodes and 16 levels deep.

```ts
type UiNode = UiColumn | UiRow | UiGroup | UiText | UiButton | UiField | UiNumber | UiToggle | UiChoice | UiSlider | UiList | UiTable | UiChart | UiCanvas | UiImage | UiProgress | UiSeparator | UiDeviceItem | UiSteps | UiQr | UiKeyValue;
```

### UiColumn

Nodes one under another.

```ts
type UiColumn = { readonly type: "column"; readonly id?: string; readonly children?: readonly UiNode[] };
```

### UiRow

Nodes side by side, going on in the next line when the panel is too narrow.

```ts
type UiRow = { readonly type: "row"; readonly id?: string; readonly children?: readonly UiNode[] };
```

### UiGroup

Nodes in a card with a title.

```ts
type UiGroup = { readonly type: "group"; readonly id?: string; readonly title?: string; readonly children?: readonly UiNode[] };
```

### UiText

Text of up to 4000 characters. With markdown it shows **bold**, *italic*, `code` and lines that start with - or 1.; it never opens links or shows pictures.

```ts
type UiText = { readonly type: "text"; readonly id?: string; readonly text: string; readonly format?: "plain" | "markdown"; readonly style?: "body" | "heading" | "caption"; readonly tone?: UiTone };
```

### UiButton

A button; a click sends click. icon is the name of a GameScrcpy icon.

```ts
type UiButton = { readonly type: "button"; readonly id: string; readonly title: string; readonly icon?: string; readonly style?: "normal" | "primary" | "danger"; readonly enabled?: boolean };
```

### UiField

A text box; leaving it sends change with the text, and Enter sends submit as well. The phone's keys pause while it has the keyboard.

```ts
type UiField = { readonly type: "field"; readonly id: string; readonly title?: string; readonly value?: string; readonly placeholder?: string; readonly enabled?: boolean };
```

### UiNumber

A number box; a new number sends change.

```ts
type UiNumber = { readonly type: "number"; readonly id: string; readonly title?: string; readonly value?: number; readonly min?: number; readonly max?: number; readonly step?: number; readonly unit?: string; readonly enabled?: boolean };
```

### UiToggle

A switch; flipping it sends change with true or false.

```ts
type UiToggle = { readonly type: "toggle"; readonly id: string; readonly title: string; readonly value?: boolean; readonly enabled?: boolean };
```

### UiChoice

Up to 64 options to pick one from; picking sends change with the option's id.

```ts
type UiChoice = { readonly type: "choice"; readonly id: string; readonly title?: string; readonly options: readonly UiOption[]; readonly value?: string; readonly enabled?: boolean };
```

### UiOption

An option of a choice: text that is both its id and its title, or an id and a title.

```ts
type UiOption = string | { readonly id: string; readonly title: string };
```

### UiSlider

A slider from min (0 if left out) to max (1 if left out); letting it go sends change with the value.

```ts
type UiSlider = { readonly type: "slider"; readonly id: string; readonly title?: string; readonly value?: number; readonly min?: number; readonly max?: number; readonly step?: number; readonly enabled?: boolean };
```

### UiList

A list of up to 500 items; clicking one sends select with its id, unless selectable is false.

```ts
type UiList = { readonly type: "list"; readonly id: string; readonly items: readonly UiListItem[]; readonly selected?: string; readonly selectable?: boolean };
```

### UiListItem

An item of a list.

```ts
type UiListItem = { readonly id: string; readonly title: string; readonly subtitle?: string; readonly icon?: string };
```

### UiTable

A table of up to 16 columns and 200 rows.

```ts
type UiTable = { readonly type: "table"; readonly id?: string; readonly columns: readonly string[]; readonly rows?: readonly UiTableRow[] };
```

### UiTableRow

A row of a table, one cell for each column.

```ts
type UiTableRow = readonly UiCell[];
```

### UiCell

A cell of a table.

```ts
type UiCell = string | number | boolean | null;
```

### UiChart

A line or bar chart of up to 4 series; height is from 60 to 480, and min and max fix the scale.

```ts
type UiChart = { readonly type: "chart"; readonly id?: string; readonly title?: string; readonly kind?: "line" | "bar"; readonly series: readonly UiSeries[]; readonly height?: number; readonly min?: number; readonly max?: number };
```

### UiSeries

A series of a chart: up to 500 values from left to right.

```ts
type UiSeries = { readonly title?: string; readonly color?: UiColor; readonly points: readonly number[] };
```

### UiCanvas

A drawing of up to 500 shapes. width and height, from 1 to 2000, are in the panel's units; a wider drawing is made smaller to fit.

```ts
type UiCanvas = { readonly type: "canvas"; readonly id?: string; readonly title?: string; readonly width: number; readonly height: number; readonly shapes?: readonly UiShape[] };
```

### UiShape

A shape of a canvas: rect (x, y, width, height, radius), circle (x, y, radius), line (x1, y1, x2, y2), polyline (points as x, y, x, y… and closed) or text (x, y, text, size, color); fill, stroke and strokeWidth paint it.

```ts
type UiShape = { readonly kind: "rect" | "circle" | "line" | "polyline" | "text"; readonly x?: number; readonly y?: number; readonly width?: number; readonly height?: number; readonly radius?: number; readonly x1?: number; readonly y1?: number; readonly x2?: number; readonly y2?: number; readonly points?: readonly number[]; readonly closed?: boolean; readonly text?: string; readonly size?: "body" | "caption" | "heading"; readonly color?: UiColor; readonly fill?: UiColor; readonly stroke?: UiColor; readonly strokeWidth?: number };
```

### UiImage

A PNG or SVG picture of up to 2 MiB in the extension's folder, written as a relative path; nothing outside the folder is shown.

```ts
type UiImage = { readonly type: "image"; readonly id?: string; readonly path: string; readonly title?: string; readonly width?: number; readonly height?: number };
```

### UiProgress

A progress bar from 0 to 1; while value is null or left out it moves on its own.

```ts
type UiProgress = { readonly type: "progress"; readonly id?: string; readonly title?: string; readonly value?: number | null };
```

### UiSeparator

A thin line between nodes.

```ts
type UiSeparator = { readonly type: "separator"; readonly id?: string };
```

### UiStatusNode

A status widget: a row or a single part, at most 64 nodes.

```ts
type UiStatusNode = UiStatusRow | UiStatusPart;
```

### UiStatusRow

Parts of a status widget side by side.

```ts
type UiStatusRow = { readonly type: "row"; readonly id?: string; readonly children?: readonly UiStatusPart[] };
```

### UiStatusPart

A part of a status widget; its text is at most 100 characters.

```ts
type UiStatusPart = UiText | UiStatusIcon | UiProgress;
```

### UiStatusIcon

A GameScrcpy icon by its name.

```ts
type UiStatusIcon = { readonly type: "icon"; readonly id?: string; readonly name: string; readonly tone?: UiTone };
```

### UiMenuItem

An item of the right-click menu; with checked it shows a check mark, and picking it sends the opposite.

```ts
type UiMenuItem = { readonly id: string; readonly title: string; readonly icon?: string; readonly checked?: boolean; readonly enabled?: boolean };
```

### UiDeviceItem

A device in a list: its icon, name and a line under it, with a pill for its state; stateText words the pill. icon is the name of a GameScrcpy icon or "svg:" and the path of an SVG in the extension's folder. With an id, clicking it sends click.

```ts
type UiDeviceItem = { readonly type: "deviceItem"; readonly id?: string; readonly title: string; readonly subtitle?: string; readonly icon?: string; readonly state?: "ready" | "busy" | "problem" | "offline"; readonly stateText?: string };
```

### UiSteps

The steps of something done in turn, 2 to 8 titles of up to 40 characters: those before current are done, current is going on (or went wrong with failed), and current equal to the number of steps means all are done.

```ts
type UiSteps = { readonly type: "steps"; readonly id?: string; readonly steps: readonly string[]; readonly current?: number; readonly failed?: boolean };
```

### UiQr

A QR code of up to 1000 bytes of text for a phone to scan.

```ts
type UiQr = { readonly type: "qr"; readonly id?: string; readonly text: string; readonly title?: string };
```

### UiKeyValue

Up to 100 rows of a name and its value.

```ts
type UiKeyValue = { readonly type: "keyValue"; readonly id?: string; readonly rows: readonly UiKeyValueRow[] };
```

### UiKeyValueRow

A row of a keyValue: a name of up to 60 characters and its value.

```ts
type UiKeyValueRow = { readonly key: string; readonly value?: string | number | boolean | null };
```

### UiToolbarState

What a toolbar button shows in place of what the manifest declares: its tooltip, its icon (a GameScrcpy icon or "svg:" and a path), whether it shows as on, and whether it can be clicked.

```ts
type UiToolbarState = { readonly title?: string; readonly icon?: string; readonly checked?: boolean; readonly enabled?: boolean };
```

### UiTarget

The card a menu was opened on: a device by its serial or a profile by its id, with its name.

```ts
type UiTarget = { readonly kind: "device" | "profile"; readonly id: string; readonly name: string };
```

### UiChooseEvent

What a card menu listener receives: the item's id, its new checked state or null, and the card.

```ts
type UiChooseEvent = UiEvent & { readonly target: UiTarget };
```

### HardwareKind

The kinds of device logic can use: serial ports, USB HID devices and Bluetooth LE devices.

```ts
type HardwareKind = "serial" | "hid" | "ble";
```

### HardwareFilter

What a listed device must have: USB vendor and product ids and HID usage page and usage (0 to 65535), one of the Bluetooth service UUIDs, and how its name starts (up to 64 characters). A device is listed when it matches any one filter.

```ts
type HardwareFilter = { readonly vendorId?: number; readonly productId?: number; readonly usagePage?: number; readonly usage?: number; readonly services?: readonly string[]; readonly namePrefix?: string };
```

### HardwareRequest

Which devices hardware.request() lists: one kind and up to 16 filters.

```ts
type HardwareRequest = { readonly kind: HardwareKind; readonly filters?: readonly HardwareFilter[] };
```

### HardwareInfo

An allowed device: the id to open it with, its kind and name and, in hardware.list(), whether it is connected now.

```ts
type HardwareInfo = { readonly id: string; readonly kind: HardwareKind; readonly name: string; readonly present?: boolean };
```

### HardwareClaimOptions

wait asks whoever holds the hardware to let it go and waits up to 3 seconds for it. onWanted is called with the name of whoever wants the hardware this logic holds; return true to let it go, and it is released for them.

```ts
type HardwareClaimOptions = { readonly wait?: boolean; readonly onWanted?: (by: string) => boolean | Promise<boolean> };
```

### NetListenOptions

Up to 16 sites, such as "https://example.com", whose pages may call the server from a browser; pages from the server itself and local files always may.

```ts
type NetListenOptions = { readonly origins?: readonly string[] };
```

### NetRequest

A request to a server of net.listen(): the path without the query, the query and headers (names in lowercase), and the body as text, bytes or parsed JSON. Bodies are up to 1 MiB.

```ts
type NetRequest = { readonly method: string; readonly path: string; readonly query: Readonly<Record<string, string>>; readonly headers: Readonly<Record<string, string>>; readonly text: string; readonly bytes: Uint8Array; json(): unknown };
```

### NetAnswer

The answer to a request: a status (200 by default), headers, and text, JSON or bytes as the body; a text alone answers 200 with it, and nothing answers 204. The answer must come within 10 seconds.

```ts
type NetAnswer = { readonly status?: number; readonly headers?: Readonly<Record<string, string>>; readonly text?: string; readonly json?: unknown; readonly bytes?: HardwareBytes } | string | void;
```

### SpeechSayOptions

engine is the id of an engine from speech.engines(); voice is the id or name of one of its voices; rate goes from 0.5 to 2, 1 being the voice's usual pace, and volume from 0 to 1. interrupt cuts off what the engine is reading and drops what waits, so that this text comes at once.

```ts
type SpeechSayOptions = { readonly engine?: string; readonly voice?: string; readonly rate?: number; readonly volume?: number; readonly interrupt?: boolean };
```

### SpeechVoicesOptions

The id of an engine from speech.engines().

```ts
type SpeechVoicesOptions = { readonly engine?: string };
```

### SpeechListenOptions

engine is the id of an engine from speech.engines() that listens; language is one of its languages, such as "zh-CN" or "en-US"; confidence, from 0 to 1, is how sure the engine must be before the handler runs.

```ts
type SpeechListenOptions = { readonly engine?: string; readonly language?: string; readonly confidence?: number };
```

### SpeechHeard

A command that was heard: the phrase of the list it matched (null when the engine took any words), the words as the engine wrote them, and how sure it is, from 0 to 1.

```ts
type SpeechHeard = { readonly phrase: string | null; readonly text: string; readonly confidence: number };
```

### SpeechVoice

A voice: the id or the name to pass as voice, and its language, such as "zh-CN".

```ts
type SpeechVoice = { readonly id: string; readonly name: string; readonly language: string };
```

### SpeechEngineInfo

A speech engine: "system" is Windows, the others are "extension id/name". say and listen tell what it can do, dictation whether it takes any words rather than only a list of commands, and languages the languages it listens in.

```ts
type SpeechEngineInfo = { readonly id: string; readonly title: string; readonly say: boolean; readonly listen: boolean; readonly dictation: boolean; readonly languages: readonly string[] };
```

### HardwareBytes

Bytes to send: a Uint8Array, another typed array or a list of numbers from 0 to 255.

```ts
type HardwareBytes = Uint8Array | ArrayBuffer | ArrayBufferView | readonly number[];
```

### SerialOptions

How a serial port talks: 115200 baud (50 to 4000000), 8 data bits, no parity, 1 stop bit and no flow control unless given, with the DTR and RTS lines on.

```ts
type SerialOptions = { readonly baudRate?: number; readonly dataBits?: 5 | 6 | 7 | 8; readonly parity?: "none" | "even" | "odd" | "mark" | "space"; readonly stopBits?: 1 | 1.5 | 2; readonly flowControl?: "none" | "hardware" | "software"; readonly dtr?: boolean; readonly rts?: boolean };
```

### SerialSignals

The DTR and RTS lines of a serial port to turn on or off; the lines left out stay as they are.

```ts
type SerialSignals = { readonly dtr?: boolean; readonly rts?: boolean };
```

### BleService

A service of a Bluetooth LE device and its characteristics.

```ts
type BleService = { readonly uuid: string; readonly characteristics: readonly BleCharacteristic[] };
```

### BleCharacteristic

A characteristic and what it allows: read, write, writeWithoutResponse, notify and indicate.

```ts
type BleCharacteristic = { readonly uuid: string; readonly properties: readonly string[] };
```

### BleWriteOptions

Whether a write waits for the device to confirm it (true unless given).

```ts
type BleWriteOptions = { readonly response?: boolean };
```

### DeviceDataEvent

What onData receives: the bytes that came in.

```ts
type DeviceDataEvent = TaskContext & { readonly data: Uint8Array };
```

### DeviceReportEvent

What onReport receives: the report id and the bytes after it.

```ts
type DeviceReportEvent = TaskContext & { readonly report: number; readonly data: Uint8Array };
```

### DeviceNotifyEvent

What a subscription receives: the service and characteristic UUIDs, written in full, and the new value.

```ts
type DeviceNotifyEvent = TaskContext & { readonly service: string; readonly characteristic: string; readonly data: Uint8Array };
```

### DeviceCloseEvent

Why a device closed by itself: it went away, Windows reported an error, or the permission to use it was taken back.

```ts
type DeviceCloseEvent = TaskContext & { readonly reason: "removed" | "failed" | "forgotten"; readonly message: string };
```

### AiAskOptions

What ai.ask() sends along and how long it waits: picture true for the whole phone picture, a region or the name of one for that part of it, false or nothing for no picture; timeoutMs from 1000 to 120000 milliseconds, 60000 by default.

```ts
type AiAskOptions = { readonly picture?: boolean | Region | RegionName; readonly timeoutMs?: number };
```

### MediaFrameOptions

How media.frames() takes frames: at most fps a second, from 0.1 to 10 (2 by default); the long side at most size pixels, a whole number from 64 to 1920 (640 by default); only region, a part of the picture in ratios, when it is given.

```ts
type MediaFrameOptions = { readonly fps?: number; readonly size?: number; readonly region?: Region };
```

### ConvertInput

What a converter of an extension receives: the name of the file, the formats it converts from and to, and the contents as bytes, as text read as UTF-8, or parsed by json(). A converter is the export named convert, or the default export, of the script that contributes.converters names; it runs in the logic sandbox without a phone and returns a ConvertOutput. A profile comes as its JSON file, a task as its task.ts and a recording as its session.json. At most 32 MiB go in and at most 32 MiB come out, and a conversion may take 30 seconds; the message of what it throws is what the user sees. The conversion fails with GS-API-165 when the script exports no such function, GS-API-166 when the result is not something GameScrcpy can write, and GS-API-167 when the result is larger than 32 MiB.

```ts
type ConvertInput = { readonly name: string; readonly from: string; readonly to: string; readonly bytes: Uint8Array; readonly text: string; json<T = unknown>(): T; readonly signal: AbortSignal };
```

### ConvertOutput

What a converter returns: the new file as bytes, as a text written as UTF-8 or as a JSON value written as JSON text, and for an import the name of the file, which names the new profile. A converter may also return the text, the bytes or the JSON value itself, such as the object of a profile; an object counts as a ConvertOutput when it has bytes, text or json. GameScrcpy checks what it imports as it checks any file it imports.

```ts
type ConvertOutput = { readonly name?: string; readonly bytes?: Uint8Array | ArrayBuffer | readonly number[]; readonly text?: string; readonly json?: unknown };
```

### VisionKind

What a recognition engine recognizes: text, things, or what the picture shows.

```ts
type VisionKind = "ocr" | "detect" | "classify";
```

### VisionParams

What a recognition engine takes besides the picture. "system" takes language, a tag such as "en" or "zh-Hans" (the languages of the user's profile by default), and words, true for each word instead of each line.

```ts
type VisionParams = Readonly<Record<string, StoreValue>>;
```

### VisionEngineInfo

A recognition engine: "system" is Windows, the others are "extension id/name". kinds tells what it recognizes and languages the languages it reads, when it says.

```ts
type VisionEngineInfo = { readonly id: string; readonly title: string; readonly kinds: readonly VisionKind[]; readonly languages: readonly string[] };
```

### VisionText

Text an engine read: x and y are its middle and width and height its size, as ratios of the picture; confidence goes from 0 to 1.

```ts
type VisionText = { readonly text: string; readonly x: number; readonly y: number; readonly width: number; readonly height: number; readonly confidence: number };
```

### VisionObject

A thing an engine found: what it is, where it is as for VisionText, and how sure the engine is, from 0 to 1.

```ts
type VisionObject = { readonly label: string; readonly x: number; readonly y: number; readonly width: number; readonly height: number; readonly confidence: number };
```

### VisionClass

What an engine says the picture shows, and how sure it is, from 0 to 1.

```ts
type VisionClass = { readonly label: string; readonly confidence: number };
```

### VisionResult

One thing recognize() found: a VisionText for "ocr", a VisionObject for "detect" and a VisionClass for "classify".

```ts
type VisionResult = VisionText | VisionObject | VisionClass;
```

### NetFetchOptions

How net.fetch() asks: the method (GET unless given), up to 64 headers besides those GameScrcpy sets itself, such as host and content-length, a body of text, sent as UTF-8 with content-type text/plain unless a header says otherwise, or of bytes, and timeoutMs, from 1000 to 60000 milliseconds (15000 by default), for the whole request.

```ts
type NetFetchOptions = { readonly method?: "GET" | "HEAD" | "POST" | "PUT" | "PATCH" | "DELETE" | "OPTIONS"; readonly headers?: Readonly<Record<string, string>>; readonly body?: string | HardwareBytes; readonly timeoutMs?: number };
```

### NetResponse

The answer to net.fetch(): its status, 404 and 500 too, the address it came from after redirects, its headers with lowercase names, and its body as text, bytes or parsed JSON.

```ts
type NetResponse = { readonly status: number; readonly url: string; readonly headers: Readonly<Record<string, string>>; readonly text: string; readonly bytes: Uint8Array; json(): unknown };
```

### NetSocketOptions

Up to 8 subprotocols to offer the server, such as "graphql-ws"; the protocol of the socket says which one it took.

```ts
type NetSocketOptions = { readonly protocols?: readonly string[] };
```

### NetMessageEvent

What onMessage receives: a text message as a string, or a binary one as bytes when binary is true.

```ts
type NetMessageEvent = TaskContext & { readonly data: string | Uint8Array; readonly binary: boolean };
```

### NetCloseEvent

Why a socket closed by itself: the code and reason the server gave, 1006 when the connection was lost, and whether both sides closed it cleanly.

```ts
type NetCloseEvent = TaskContext & { readonly code: number; readonly reason: string; readonly clean: boolean };
```

### FilePickOptions

save shows the dialog to save a file instead of the one to open a file; filters are up to 8 kinds of file the dialog offers; suggestedName is the file name the dialog starts with, without a folder.

```ts
type FilePickOptions = { readonly save?: boolean; readonly filters?: readonly FileFilter[]; readonly suggestedName?: string };
```

### FileFilter

A kind of file in the dialog: its name, up to 60 characters, and 1 to 16 extensions without the dot, such as ["png", "jpg"].

```ts
type FileFilter = { readonly name: string; readonly extensions: readonly string[] };
```

### FolderEntry

A file or folder in a FolderHandle: its name, its path from the top of the folder, whether it is a folder, its size in bytes and when it last changed, in milliseconds since 1970.

```ts
type FolderEntry = { readonly name: string; readonly path: string; readonly folder: boolean; readonly size: number; readonly modified: number };
```
