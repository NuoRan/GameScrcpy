/** Tells a task that it was cancelled: when its key is released, the script stops or the profile changes. */
type AbortSignal = { readonly aborted: boolean; readonly reason: unknown; onabort: (() => void) | null; throwIfAborted(): void; addEventListener(type: "abort", listener: () => void): void; removeEventListener(type: "abort", listener: () => void): void };

/** What every exported handler receives when a key, a blueprint or the console starts it. */
type TaskContext = { readonly signal: AbortSignal };

/** The settings of a running task, as chosen on the Tasks page; the task declares them in static params. */
type TaskParams = { readonly [name: string]: any };

/** A task setting made by param.number, param.text or param.toggle. */
type TaskParam = { readonly kind: "number" | "text" | "toggle"; readonly value: number | string | boolean; readonly label?: string; readonly min?: number; readonly max?: number };

/** The values for a new component's static props, with an optional id and whether it starts on. */
type ComponentOptions = { readonly id?: string; readonly enabled?: boolean; readonly [prop: string]: unknown };

/** A property declared in static props with prop.key, prop.position, prop.number, prop.text, prop.toggle or prop.component. */
type ComponentProp = { readonly kind: "key" | "position" | "number" | "text" | "toggle" | "component"; readonly value: unknown; readonly label?: string };

/** How a component property is labelled. */
type PropOptions = { label?: string };

/** What an MCP tool of an extension does, in a sentence or two for the AI, and the JSON Schema of its input: an object of type object of at most 16 KiB; without one the tool takes an empty object. */
type McpToolSpec = { readonly description: string; readonly input?: { readonly [key: string]: unknown } };

/** How a number property is labelled, and the smallest and largest value its box in the property panel takes. */
type NumberPropOptions = { label?: string; min?: number; max?: number };

/** How a task setting shows on the Tasks page: its label, and for numbers the smallest and largest value. */
type ParamOptions = { label?: string; min?: number; max?: number };

/** The Android keys a script can press. */
type AndroidKey = "back" | "home" | "appSwitch" | "menu" | "power" | "volumeUp" | "volumeDown" | "volumeMute" | "enter" | "delete" | "tab" | "space" | "escape" | "up" | "down" | "left" | "right" | "pageUp" | "pageDown" | "search" | "camera" | "mediaPlayPause" | "notifications" | "settings";

/** The phone: model name, Android version, screen size in pixels and rotation in quarter turns. */
type DeviceInfo = { readonly model: string; readonly android: number; readonly width: number; readonly height: number; readonly rotation: 0 | 1 | 2 | 3 };

/** The 13 kinds of component. */
type ComponentType = "tap" | "hold" | "longPress" | "turbo" | "swipe" | "pinch" | "wasd" | "rightWalk" | "padStick" | "camera" | "padCamera" | "freeLook" | "skill";

/** A move to another state, made by to.<state>(reason) and returned from a state of machine(). */
type MachineStep = { readonly state: string; readonly reason: string };

/** What a state of machine() receives: one function per state that makes the move to it, with an optional reason the editor shows on the transition. */
type MachineTo<S> = { readonly [K in S & string]: (reason?: string) => MachineStep };

/** The name the editor shows for a state machine, and the state it starts in (the first one when left out). */
type MachineOptions<S> = { name?: string; start?: NoInfer<S & string> };

/** An interface made with defineInterface(): its name, and all(), every component of the profile whose class lists it in static implements, so logic can tell each of them at once. */
type Interface<T> = { readonly name: string; all(): (Component & T)[] };

/** A built-in component class. A component of yours that extends it is placed as that built-in component and presses the screen just as it does; the event methods of O that the class writes run each time the event comes, as well. T is the type its components have in controls. */
type BuiltInClass<T, O> = { new (): Omit<T, keyof O> & O; readonly component?: T; readonly implements?: readonly Interface<unknown>[] };

/** A control of the profile in use. */
type ControlInfo = { readonly id: string; readonly name: string; readonly type: ComponentType; readonly enabled: boolean };

/** What a variable holds. */
type VarValue = boolean | number | string;

/** A value the store can keep. */
type StoreValue = null | boolean | number | string | StoreValue[] | { [key: string]: StoreValue };

/** A job that a native extension runs, such as writing firmware. result settles when it ends and throws GS-API-065 when it fails and GS-API-066 when it is cancelled; onProgress hears how far it got, fraction from 0 to 1 or -1 when it cannot tell, at most 20 times a second; onLog hears its lines, level 0 information, 1 warning, 2 error; onOutput hears the parts of its result that it hands over while it runs, such as each device a scan finds; follow hears the progress, waits for the job and then runs done with the result or failed with the error, and without failed a failure throws as result does; cancel asks it to stop. It is cancelled when the script stops. */
type ExtensionJob<T> = { readonly result: Promise<T>; onProgress(listener: (fraction: number, text: string) => void): ExtensionJob<T>; onLog(listener: (level: number, text: string) => void): ExtensionJob<T>; onOutput(listener: (value: StoreValue) => void): ExtensionJob<T>; follow(progress?: (fraction: number, text: string) => void | Promise<void>, done?: (result: T) => void | Promise<void>, failed?: (error: Error) => void | Promise<void>): Promise<void>; cancel(): void };

/** What the handler of a service gets besides the arguments: the id of the extension that calls it ("" for the user's own logic), the permissions the call runs with, which are those that both extensions have, and the signal that aborts when the caller cancels or the call takes longer than 10 seconds. */
type ServiceContext = TaskContext & { readonly caller: string; readonly permissions: readonly string[] };

/** What happens when the event comes again while the last task it started is still running: ignore it (default), restart (cancel the running task first), stop (only cancel the running task), queue (run after it) or parallel (run both). */
type ListenOptions = { readonly again?: "ignore" | "restart" | "stop" | "queue" | "parallel" };

/** The options of a control's key events: again as for other events, and consume. With consume true (the default), the profile's logic takes the key of a tap, hold, long press, turbo, swipe or multi-press control it listens to: pressing the key no longer touches the phone, and the control touches only when the logic calls it, such as with press() or tap(), a few milliseconds later than its own touch would. consume: false keeps the control's own touch, which comes before the handler runs. Only the profile's own logic takes keys: tasks, extension scripts and scripts an AI client runs only listen, the event methods a component class overrides keep the built-in touch, and other controls always keep working. */
type KeyListenOptions = ListenOptions & { readonly consume?: boolean };

/** A listener; dispose() stops it. Listeners made inside a task stop when that task ends, those made at module top level last as long as the script. */
type Subscription = { dispose(): void };

/** How long until() waits for its condition after each try of the action, in milliseconds (5000 by default, from 1 to 600000), and how many more times it does the action when the condition does not come true in time (0 by default, at most 10). */
type UntilOptions = { readonly timeoutMs?: number; readonly retry?: number };

/** What an action returns: a Promise that resolves when the action is done, with until(), which then checks the condition once per frame and resolves with its first truthy value, doing the action again, as often as retry allows, when it does not come true in time. The condition may return a Promise, as vision.find() does. until() rejects with a TimeoutError (GS-API-030) when the tries run out and with an AbortError when the task is cancelled; it throws GS-API-008 for a condition that is not a function, GS-API-011 for options that are not an object, GS-API-003 for a timeoutMs out of range and GS-API-017 for a retry out of range. */
type Action<T> = Promise<T> & { until<R>(condition: () => R | Promise<R>, options?: UntilOptions): Promise<NonNullable<Awaited<R>>> };

/** A binding made with bind(); dispose() stops it and puts the property back to its value from before the binding. Bindings made inside a task stop when that task ends, those made at module top level last as long as the script. */
type Binding = { dispose(): void };

/** What a control listener receives: the cancel signal and the id of the control. */
type ControlEvent = TaskContext & { readonly control: string };

/** What onDirectionChange receives: the new direction in degrees. */
type DirectionEvent = ControlEvent & { readonly direction: number };

/** What a direction wheel's onChoose receives: the cell picked, counted from 0 at the first cell going clockwise. */
type CellEvent = ControlEvent & { readonly cell: number };

/** What an action wheel's onChoose receives: the cell run, counted from 0 at the top going clockwise, and its name. */
type WheelChoice = ControlEvent & { readonly cell: number; readonly name: string };

/** What a variable listener receives: the cancel signal, the variable id and its new value. */
type VarChange = TaskContext & { readonly id: string; readonly value: VarValue };

/** What a key listener receives: the key's name, and on release how long it was held. */
type KeyEvent = TaskContext & { readonly key: InputName; readonly heldMs?: number };

/** Keys by name, as in profiles ("KeyF", "Space", "Mouse.Left") or short (F, Esc, MouseLeft, WheelUp, GamepadA). */
type Keys = { readonly [name: string]: Key };

/** What a global listener receives: the name and the new value. */
type GlobalChange = TaskContext & { readonly name: string; readonly value: StoreValue };

/** The app in front on the phone: its package and whether Android calls it a game. */
type ForegroundApp = { readonly package: string; readonly game: boolean };

/** An input box on the phone: whether it is open, its kind as Android's InputType number (1 text, 2 number, 3 phone, 4 date and time, with the variation and flag bits added), what its Enter key does and whether it takes several lines. When it closes the other fields are 0, "unspecified" and false. */
type InputBox = { readonly shown: boolean; readonly inputType: number; readonly action: "unspecified" | "none" | "go" | "search" | "send" | "next" | "done" | "previous"; readonly multiLine: boolean };

/** Whether to stop the app before starting it. */
type LaunchOptions = { readonly restart?: boolean };

/** A profile's id and name. */
type ProfileInfo = { readonly id: string; readonly name: string };

/** Something to run: a Promise, or a function to start. */
type Work = Promise<unknown> | (() => unknown);

/** The words on the two buttons of a yes or no question (up to 60 characters each). */
type ConfirmOptions = { readonly ok?: string; readonly cancel?: string };

/** The text already in the box when the question opens. */
type AskOptions = { readonly initial?: string };

/** A position on the phone picture as ratios: (0, 0) top left, (1, 1) bottom right. */
type PicturePoint = { readonly x: number; readonly y: number };

/** A key or mouse button, written as in profiles, such as "KeyW", "ShiftLeft" or "Mouse.Right". */
type InputName = string;

/** The colors a drawing can use; they follow the program's theme. */
type DrawColor = "accent" | "good" | "warn" | "bad" | "white";

/** A box's color (accent by default), a label of up to 60 characters and how long it stays, in milliseconds. */
type BoxOptions = { readonly color?: DrawColor; readonly label?: string; readonly durationMs?: number };

/** The text's color (accent by default) and how long it stays, in milliseconds. */
type TextOptions = { readonly color?: DrawColor; readonly durationMs?: number };

/** A mark's color (accent by default), a label of up to 60 characters shown to the right of the ring and how long it stays, in milliseconds. */
type MarkOptions = { readonly color?: DrawColor; readonly label?: string; readonly durationMs?: number };

/** Something drawn on the picture; dispose() removes it. At most 64 are shown at a time. */
type Drawing = { dispose(): void };

/** Part of the picture, as ratios: left, top, width and height. */
type Region = { readonly x: number; readonly y: number; readonly width: number; readonly height: number };

/** Where to look (a region or a region name; the whole picture by default), how alike it must be from 0 to 1 (0.8 by default), how to compare (grey by default), how many to return for findAll (16 by default, at most 64) and how long to wait while there is no picture yet (2000 ms by default, at most 10000). A template name brings its own threshold, match mode and region; options given here win. */
type FindOptions = { readonly region?: Region | RegionName; readonly threshold?: number; readonly match?: MatchMode; readonly max?: number; readonly timeoutMs?: number };

/** Where a picture was found: x and y are its center and width and height its size, as ratios of the picture; score is how alike it is, from 0 to 1. */
type Found = { readonly x: number; readonly y: number; readonly width: number; readonly height: number; readonly score: number };

/** Where to look and how alike it must be, as for find(); timeoutMs is how long to keep looking in all (10000 ms by default, at most 600000) and intervalMs how long to wait between looks (250 ms by default, from 50 to 60000). */
type WaitOptions = FindOptions & { readonly intervalMs?: number };

/** Where to look and how alike it must be, as for find(); intervalMs is how long to wait between looks (500 ms by default, from 50 to 60000), and again what happens when the picture comes or goes again while the last task it started is still running. */
type WatchOptions = FindOptions & ListenOptions & { readonly intervalMs?: number };

/** How a template is compared. "grey" compares brightness only (zero-mean normalized cross-correlation). "color" also compares colors: the score is multiplied by 1 - d / 128 (at least 0), where d is the mean absolute difference of red, green and blue (0 to 255) over the template's opaque pixels, so the same shape in another color scores low. */
type MatchMode = "color" | "grey";

/** Where to look (the whole picture by default), how far each of red, green and blue may be from the color as a ratio of 255 (0.1 by default) and how long to wait while there is no picture yet (2000 ms by default, at most 10000). A color name brings its own tolerance and region; options given here win. */
type ColorOptions = { readonly region?: Region | RegionName; readonly tolerance?: number; readonly timeoutMs?: number };

/** How long to wait while there is no picture yet (2000 ms by default, at most 10000). */
type PixelOptions = { readonly timeoutMs?: number };

/** How much must change to count, as the share of the region from 0 to 1 (0.05 by default; a spot counts when its brightness moves by about a tenth or more), and how long to wait while there is no picture yet (2000 ms by default, at most 10000). */
type ChangeOptions = { readonly threshold?: number; readonly timeoutMs?: number };

/** How much must change, as for changed(); intervalMs is how long to wait between looks (500 ms by default, from 50 to 60000), and again what happens when it changes again while the last task it started is still running. */
type OnChangeOptions = ListenOptions & { readonly threshold?: number; readonly intervalMs?: number };

/** Where to read (a region or a region name; the whole picture by default), the language as a tag such as "zh-Hans" or "en" (by default the first Windows display language that has text recognition, otherwise the first one this computer has; findText looks for text of English letters, digits and signs with English recognition when the computer has it) and how long to wait while there is no picture yet (2000 ms by default, at most 10000). */
type ReadTextOptions = { readonly region?: Region | RegionName; readonly language?: string; readonly timeoutMs?: number };

/** Text read from the picture: text is every line joined with line breaks, lines each line with its place from top to bottom, and language the tag of the language it was read in, such as "zh-Hans-CN". */
type TextResult = { readonly text: string; readonly lines: readonly TextLine[]; readonly language: string };

/** One line of text: x and y are its center and width and height its size, as ratios of the picture. */
type TextLine = { readonly text: string; readonly x: number; readonly y: number; readonly width: number; readonly height: number };

/** How far each of red, green and blue may be from the color as a ratio of 255 (0.1 by default; a color name brings its own) and how long to wait while there is no picture yet (2000 ms by default, at most 10000). */
type PixelMatchOptions = { readonly tolerance?: number; readonly timeoutMs?: number };

/** A path on the picture: its points as ratios and how long a swipe along it takes, in milliseconds. */
type PicturePath = { readonly points: readonly PicturePoint[]; readonly durationMs: number };

/** A picture in the profile folder, written as a relative path such as "res/chest.png". */
type ImagePath = `${string}.png` | `${string}.jpg` | `${string}.jpeg` | `${string}.bmp` | `${string}.PNG` | `${string}.JPG` | `${string}.JPEG` | `${string}.BMP`;

/** A color written "#RRGGBB", such as "#F5C542". */
type HexColor = `#${string}`;

/** The name of a template in res/resources.json. */
type TemplateName = keyof TemplateNames;

/** The name of a region in res/resources.json. */
type RegionName = keyof RegionNames;

/** The name of a point in res/resources.json. */
type PointName = keyof PointNames;

/** The name of a path in res/resources.json. */
type PathName = keyof PathNames;

/** The name of a color in res/resources.json. */
type ColorName = keyof ColorNames;

/** What the user does in a panel. */
type UiEventType = "click" | "change" | "submit" | "select" | "open" | "close";

/** What a panel or menu listener receives: the id of the node or item, and its new value or null. */
type UiEvent = TaskContext & { readonly id: string; readonly value: unknown };

/** The colors of text and icons in a panel; they follow the program's theme. */
type UiTone = "normal" | "muted" | "accent" | "good" | "warn" | "bad";

/** A color in a chart or canvas: "#RRGGBB", or the name of a theme color such as "accent", "textPrimary" or "success". */
type UiColor = string;

/** A node of a panel tree. A tree is at most 256 KiB as JSON, 2000 nodes and 16 levels deep. */
type UiNode = UiColumn | UiRow | UiGroup | UiText | UiButton | UiField | UiNumber | UiToggle | UiChoice | UiSlider | UiList | UiTable | UiChart | UiCanvas | UiImage | UiProgress | UiSeparator | UiDeviceItem | UiSteps | UiQr | UiKeyValue;

/** Nodes one under another. */
type UiColumn = { readonly type: "column"; readonly id?: string; readonly children?: readonly UiNode[] };

/** Nodes side by side, going on in the next line when the panel is too narrow. */
type UiRow = { readonly type: "row"; readonly id?: string; readonly children?: readonly UiNode[] };

/** Nodes in a card with a title. */
type UiGroup = { readonly type: "group"; readonly id?: string; readonly title?: string; readonly children?: readonly UiNode[] };

/** Text of up to 4000 characters. With markdown it shows **bold**, *italic*, `code` and lines that start with - or 1.; it never opens links or shows pictures. */
type UiText = { readonly type: "text"; readonly id?: string; readonly text: string; readonly format?: "plain" | "markdown"; readonly style?: "body" | "heading" | "caption"; readonly tone?: UiTone };

/** A button; a click sends click. icon is the name of a GameScrcpy icon. */
type UiButton = { readonly type: "button"; readonly id: string; readonly title: string; readonly icon?: string; readonly style?: "normal" | "primary" | "danger"; readonly enabled?: boolean };

/** A text box; leaving it sends change with the text, and Enter sends submit as well. The phone's keys pause while it has the keyboard. */
type UiField = { readonly type: "field"; readonly id: string; readonly title?: string; readonly value?: string; readonly placeholder?: string; readonly enabled?: boolean };

/** A number box; a new number sends change. */
type UiNumber = { readonly type: "number"; readonly id: string; readonly title?: string; readonly value?: number; readonly min?: number; readonly max?: number; readonly step?: number; readonly unit?: string; readonly enabled?: boolean };

/** A switch; flipping it sends change with true or false. */
type UiToggle = { readonly type: "toggle"; readonly id: string; readonly title: string; readonly value?: boolean; readonly enabled?: boolean };

/** Up to 64 options to pick one from; picking sends change with the option's id. */
type UiChoice = { readonly type: "choice"; readonly id: string; readonly title?: string; readonly options: readonly UiOption[]; readonly value?: string; readonly enabled?: boolean };

/** An option of a choice: text that is both its id and its title, or an id and a title. */
type UiOption = string | { readonly id: string; readonly title: string };

/** A slider from min (0 if left out) to max (1 if left out); letting it go sends change with the value. */
type UiSlider = { readonly type: "slider"; readonly id: string; readonly title?: string; readonly value?: number; readonly min?: number; readonly max?: number; readonly step?: number; readonly enabled?: boolean };

/** A list of up to 500 items; clicking one sends select with its id, unless selectable is false. */
type UiList = { readonly type: "list"; readonly id: string; readonly items: readonly UiListItem[]; readonly selected?: string; readonly selectable?: boolean };

/** An item of a list. */
type UiListItem = { readonly id: string; readonly title: string; readonly subtitle?: string; readonly icon?: string };

/** A table of up to 16 columns and 200 rows. */
type UiTable = { readonly type: "table"; readonly id?: string; readonly columns: readonly string[]; readonly rows?: readonly UiTableRow[] };

/** A row of a table, one cell for each column. */
type UiTableRow = readonly UiCell[];

/** A cell of a table. */
type UiCell = string | number | boolean | null;

/** A line or bar chart of up to 4 series; height is from 60 to 480, and min and max fix the scale. */
type UiChart = { readonly type: "chart"; readonly id?: string; readonly title?: string; readonly kind?: "line" | "bar"; readonly series: readonly UiSeries[]; readonly height?: number; readonly min?: number; readonly max?: number };

/** A series of a chart: up to 500 values from left to right. */
type UiSeries = { readonly title?: string; readonly color?: UiColor; readonly points: readonly number[] };

/** A drawing of up to 500 shapes. width and height, from 1 to 2000, are in the panel's units; a wider drawing is made smaller to fit. */
type UiCanvas = { readonly type: "canvas"; readonly id?: string; readonly title?: string; readonly width: number; readonly height: number; readonly shapes?: readonly UiShape[] };

/** A shape of a canvas: rect (x, y, width, height, radius), circle (x, y, radius), line (x1, y1, x2, y2), polyline (points as x, y, x, y… and closed) or text (x, y, text, size, color); fill, stroke and strokeWidth paint it. */
type UiShape = { readonly kind: "rect" | "circle" | "line" | "polyline" | "text"; readonly x?: number; readonly y?: number; readonly width?: number; readonly height?: number; readonly radius?: number; readonly x1?: number; readonly y1?: number; readonly x2?: number; readonly y2?: number; readonly points?: readonly number[]; readonly closed?: boolean; readonly text?: string; readonly size?: "body" | "caption" | "heading"; readonly color?: UiColor; readonly fill?: UiColor; readonly stroke?: UiColor; readonly strokeWidth?: number };

/** A PNG or SVG picture of up to 2 MiB in the extension's folder, written as a relative path; nothing outside the folder is shown. */
type UiImage = { readonly type: "image"; readonly id?: string; readonly path: string; readonly title?: string; readonly width?: number; readonly height?: number };

/** A progress bar from 0 to 1; while value is null or left out it moves on its own. */
type UiProgress = { readonly type: "progress"; readonly id?: string; readonly title?: string; readonly value?: number | null };

/** A thin line between nodes. */
type UiSeparator = { readonly type: "separator"; readonly id?: string };

/** A status widget: a row or a single part, at most 64 nodes. */
type UiStatusNode = UiStatusRow | UiStatusPart;

/** Parts of a status widget side by side. */
type UiStatusRow = { readonly type: "row"; readonly id?: string; readonly children?: readonly UiStatusPart[] };

/** A part of a status widget; its text is at most 100 characters. */
type UiStatusPart = UiText | UiStatusIcon | UiProgress;

/** A GameScrcpy icon by its name. */
type UiStatusIcon = { readonly type: "icon"; readonly id?: string; readonly name: string; readonly tone?: UiTone };

/** An item of the right-click menu; with checked it shows a check mark, and picking it sends the opposite. */
type UiMenuItem = { readonly id: string; readonly title: string; readonly icon?: string; readonly checked?: boolean; readonly enabled?: boolean };

/** A device in a list: its icon, name and a line under it, with a pill for its state; stateText words the pill. icon is the name of a GameScrcpy icon or "svg:" and the path of an SVG in the extension's folder. With an id, clicking it sends click. */
type UiDeviceItem = { readonly type: "deviceItem"; readonly id?: string; readonly title: string; readonly subtitle?: string; readonly icon?: string; readonly state?: "ready" | "busy" | "problem" | "offline"; readonly stateText?: string };

/** The steps of something done in turn, 2 to 8 titles of up to 40 characters: those before current are done, current is going on (or went wrong with failed), and current equal to the number of steps means all are done. */
type UiSteps = { readonly type: "steps"; readonly id?: string; readonly steps: readonly string[]; readonly current?: number; readonly failed?: boolean };

/** A QR code of up to 1000 bytes of text for a phone to scan. */
type UiQr = { readonly type: "qr"; readonly id?: string; readonly text: string; readonly title?: string };

/** Up to 100 rows of a name and its value. */
type UiKeyValue = { readonly type: "keyValue"; readonly id?: string; readonly rows: readonly UiKeyValueRow[] };

/** A row of a keyValue: a name of up to 60 characters and its value. */
type UiKeyValueRow = { readonly key: string; readonly value?: string | number | boolean | null };

/** What a toolbar button shows in place of what the manifest declares: its tooltip, its icon (a GameScrcpy icon or "svg:" and a path), whether it shows as on, and whether it can be clicked. */
type UiToolbarState = { readonly title?: string; readonly icon?: string; readonly checked?: boolean; readonly enabled?: boolean };

/** The card a menu was opened on: a device by its serial or a profile by its id, with its name. */
type UiTarget = { readonly kind: "device" | "profile"; readonly id: string; readonly name: string };

/** What a card menu listener receives: the item's id, its new checked state or null, and the card. */
type UiChooseEvent = UiEvent & { readonly target: UiTarget };

/** The kinds of device logic can use: serial ports, USB HID devices and Bluetooth LE devices. */
type HardwareKind = "serial" | "hid" | "ble";

/** What a listed device must have: USB vendor and product ids and HID usage page and usage (0 to 65535), one of the Bluetooth service UUIDs, and how its name starts (up to 64 characters). A device is listed when it matches any one filter. */
type HardwareFilter = { readonly vendorId?: number; readonly productId?: number; readonly usagePage?: number; readonly usage?: number; readonly services?: readonly string[]; readonly namePrefix?: string };

/** Which devices hardware.request() lists: one kind and up to 16 filters. */
type HardwareRequest = { readonly kind: HardwareKind; readonly filters?: readonly HardwareFilter[] };

/** An allowed device: the id to open it with, its kind and name and, in hardware.list(), whether it is connected now. */
type HardwareInfo = { readonly id: string; readonly kind: HardwareKind; readonly name: string; readonly present?: boolean };

/** wait asks whoever holds the hardware to let it go and waits up to 3 seconds for it. onWanted is called with the name of whoever wants the hardware this logic holds; return true to let it go, and it is released for them. */
type HardwareClaimOptions = { readonly wait?: boolean; readonly onWanted?: (by: string) => boolean | Promise<boolean> };

/** Up to 16 sites, such as "https://example.com", whose pages may call the server from a browser; pages from the server itself always may, and pages whose origin is null, such as local files, may not. */
type NetListenOptions = { readonly origins?: readonly string[] };

/** A request to a server of net.listen(): the path without the query, the query and headers (names in lowercase), and the body as text, bytes or parsed JSON. Bodies are up to 1 MiB. */
type NetRequest = { readonly method: string; readonly path: string; readonly query: Readonly<Record<string, string>>; readonly headers: Readonly<Record<string, string>>; readonly text: string; readonly bytes: Uint8Array; json(): unknown };

/** The answer to a request: a status (200 by default), headers, and text, JSON or bytes as the body; a text alone answers 200 with it, and nothing answers 204. The answer must come within 10 seconds. */
type NetAnswer = { readonly status?: number; readonly headers?: Readonly<Record<string, string>>; readonly text?: string; readonly json?: unknown; readonly bytes?: HardwareBytes } | string | void;

/** engine is the id of an engine from speech.engines(); voice is the id or name of one of its voices; rate goes from 0.5 to 2, 1 being the voice's usual pace, and volume from 0 to 1. interrupt cuts off what the engine is reading and drops what waits, so that this text comes at once. */
type SpeechSayOptions = { readonly engine?: string; readonly voice?: string; readonly rate?: number; readonly volume?: number; readonly interrupt?: boolean };

/** The id of an engine from speech.engines(). */
type SpeechVoicesOptions = { readonly engine?: string };

/** engine is the id of an engine from speech.engines() that listens; language is one of its languages, such as "zh-CN" or "en-US"; confidence, from 0 to 1, is how sure the engine must be before the handler runs. */
type SpeechListenOptions = { readonly engine?: string; readonly language?: string; readonly confidence?: number };

/** A command that was heard: the phrase of the list it matched (null when the engine took any words), the words as the engine wrote them, and how sure it is, from 0 to 1. */
type SpeechHeard = { readonly phrase: string | null; readonly text: string; readonly confidence: number };

/** A voice: the id or the name to pass as voice, and its language, such as "zh-CN". */
type SpeechVoice = { readonly id: string; readonly name: string; readonly language: string };

/** A speech engine: "system" is Windows, the others are "extension id/name". say and listen tell what it can do, dictation whether it takes any words rather than only a list of commands, and languages the languages it listens in. */
type SpeechEngineInfo = { readonly id: string; readonly title: string; readonly say: boolean; readonly listen: boolean; readonly dictation: boolean; readonly languages: readonly string[] };

/** Bytes to send: a Uint8Array, another typed array or a list of numbers from 0 to 255. */
type HardwareBytes = Uint8Array | ArrayBuffer | ArrayBufferView | readonly number[];

/** How a serial port talks: 115200 baud (50 to 4000000), 8 data bits, no parity, 1 stop bit and no flow control unless given, with the DTR and RTS lines on. */
type SerialOptions = { readonly baudRate?: number; readonly dataBits?: 5 | 6 | 7 | 8; readonly parity?: "none" | "even" | "odd" | "mark" | "space"; readonly stopBits?: 1 | 1.5 | 2; readonly flowControl?: "none" | "hardware" | "software"; readonly dtr?: boolean; readonly rts?: boolean };

/** The DTR and RTS lines of a serial port to turn on or off; the lines left out stay as they are. */
type SerialSignals = { readonly dtr?: boolean; readonly rts?: boolean };

/** A service of a Bluetooth LE device and its characteristics. */
type BleService = { readonly uuid: string; readonly characteristics: readonly BleCharacteristic[] };

/** A characteristic and what it allows: read, write, writeWithoutResponse, notify and indicate. */
type BleCharacteristic = { readonly uuid: string; readonly properties: readonly string[] };

/** Whether a write waits for the device to confirm it (true unless given). */
type BleWriteOptions = { readonly response?: boolean };

/** What onData receives: the bytes that came in. */
type DeviceDataEvent = TaskContext & { readonly data: Uint8Array };

/** What onReport receives: the report id and the bytes after it. */
type DeviceReportEvent = TaskContext & { readonly report: number; readonly data: Uint8Array };

/** What a subscription receives: the service and characteristic UUIDs, written in full, and the new value. */
type DeviceNotifyEvent = TaskContext & { readonly service: string; readonly characteristic: string; readonly data: Uint8Array };

/** Why a device closed by itself: it went away, Windows reported an error, or the permission to use it was taken back. */
type DeviceCloseEvent = TaskContext & { readonly reason: "removed" | "failed" | "forgotten"; readonly message: string };

/** What ai.ask() sends along and how long it waits: picture true for the whole phone picture, a region or the name of one for that part of it, false or nothing for no picture; timeoutMs from 1000 to 120000 milliseconds, 60000 by default. */
type AiAskOptions = { readonly picture?: boolean | Region | RegionName; readonly timeoutMs?: number };

/** How media.frames() takes frames: at most fps a second, from 0.1 to 10 (2 by default); the long side at most size pixels, a whole number from 64 to 1920 (640 by default); only region, a part of the picture in ratios, when it is given. */
type MediaFrameOptions = { readonly fps?: number; readonly size?: number; readonly region?: Region };

/** What a converter of an extension receives: the name of the file, the formats it converts from and to, and the contents as bytes, as text read as UTF-8, or parsed by json(). A converter is the export named convert, or the default export, of the script that contributes.converters names; it runs in the logic sandbox without a phone and returns a ConvertOutput. A profile comes as its JSON file, a task as its task.ts and a recording as its session.json. At most 32 MiB go in and at most 32 MiB come out, and a conversion may take 30 seconds; the message of what it throws is what the user sees. The conversion fails with GS-API-165 when the script exports no such function, GS-API-166 when the result is not something GameScrcpy can write, and GS-API-167 when the result is larger than 32 MiB. */
type ConvertInput = { readonly name: string; readonly from: string; readonly to: string; readonly bytes: Uint8Array; readonly text: string; json<T = unknown>(): T; readonly signal: AbortSignal };

/** What a converter returns: the new file as bytes, as a text written as UTF-8 or as a JSON value written as JSON text, and for an import the name of the file, which names the new profile. A converter may also return the text, the bytes or the JSON value itself, such as the object of a profile; an object counts as a ConvertOutput when it has bytes, text or json. GameScrcpy checks what it imports as it checks any file it imports. */
type ConvertOutput = { readonly name?: string; readonly bytes?: Uint8Array | ArrayBuffer | readonly number[]; readonly text?: string; readonly json?: unknown };

/** What a recognition engine recognizes: text, things, or what the picture shows. */
type VisionKind = "ocr" | "detect" | "classify";

/** What a recognition engine takes besides the picture. "system" takes language, a tag such as "en" or "zh-Hans" (the languages of the user's profile by default), and words, true for each word instead of each line. */
type VisionParams = Readonly<Record<string, StoreValue>>;

/** A recognition engine: "system" is Windows, the others are "extension id/name". kinds tells what it recognizes and languages the languages it reads, when it says. */
type VisionEngineInfo = { readonly id: string; readonly title: string; readonly kinds: readonly VisionKind[]; readonly languages: readonly string[] };

/** Text an engine read: x and y are its middle and width and height its size, as ratios of the picture; confidence goes from 0 to 1. */
type VisionText = { readonly text: string; readonly x: number; readonly y: number; readonly width: number; readonly height: number; readonly confidence: number };

/** A thing an engine found: what it is, where it is as for VisionText, and how sure the engine is, from 0 to 1. */
type VisionObject = { readonly label: string; readonly x: number; readonly y: number; readonly width: number; readonly height: number; readonly confidence: number };

/** What an engine says the picture shows, and how sure it is, from 0 to 1. */
type VisionClass = { readonly label: string; readonly confidence: number };

/** One thing recognize() found: a VisionText for "ocr", a VisionObject for "detect" and a VisionClass for "classify". */
type VisionResult = VisionText | VisionObject | VisionClass;

/** How net.fetch() asks: the method (GET unless given), up to 64 headers besides those GameScrcpy sets itself, such as host and content-length, a body of text, sent as UTF-8 with content-type text/plain unless a header says otherwise, or of bytes, and timeoutMs, from 1000 to 60000 milliseconds (15000 by default), for the whole request. */
type NetFetchOptions = { readonly method?: "GET" | "HEAD" | "POST" | "PUT" | "PATCH" | "DELETE" | "OPTIONS"; readonly headers?: Readonly<Record<string, string>>; readonly body?: string | HardwareBytes; readonly timeoutMs?: number };

/** The answer to net.fetch(): its status, 404 and 500 too, the address it came from after redirects, its headers with lowercase names, and its body as text, bytes or parsed JSON. */
type NetResponse = { readonly status: number; readonly url: string; readonly headers: Readonly<Record<string, string>>; readonly text: string; readonly bytes: Uint8Array; json(): unknown };

/** Up to 8 subprotocols to offer the server, such as "graphql-ws"; the protocol of the socket says which one it took. */
type NetSocketOptions = { readonly protocols?: readonly string[] };

/** What onMessage receives: a text message as a string, or a binary one as bytes when binary is true. */
type NetMessageEvent = TaskContext & { readonly data: string | Uint8Array; readonly binary: boolean };

/** Why a socket closed by itself: the code and reason the server gave, 1006 when the connection was lost, and whether both sides closed it cleanly. */
type NetCloseEvent = TaskContext & { readonly code: number; readonly reason: string; readonly clean: boolean };

/** save shows the dialog to save a file instead of the one to open a file; filters are up to 8 kinds of file the dialog offers; suggestedName is the file name the dialog starts with, without a folder. */
type FilePickOptions = { readonly save?: boolean; readonly filters?: readonly FileFilter[]; readonly suggestedName?: string };

/** A kind of file in the dialog: its name, up to 60 characters, and 1 to 16 extensions without the dot, such as ["png", "jpg"]. */
type FileFilter = { readonly name: string; readonly extensions: readonly string[] };

/** A file or folder in a FolderHandle: its name, its path from the top of the folder, whether it is a folder, its size in bytes and when it last changed, in milliseconds since 1970. */
type FolderEntry = { readonly name: string; readonly path: string; readonly folder: boolean; readonly size: number; readonly modified: number };

/** A component of the profile in use. Every type below has these members too. */
interface Component {
    /**
     * The control id from the profile; stays the same when the control is renamed.
     * @example log.info(controls.jump.id);
     */
    readonly id: string;

    /**
     * The name shown in the editor.
     * @example log.info(controls.jump.name);
     */
    readonly name: string;

    /**
     * Where the component sits on the picture, as ratios, for the phone's picture when the logic started; it follows moveTo(). null for a component made in logic with new.
     * @example await touch.tap(controls.jump.position);
     */
    readonly position: PicturePoint | null;

    /**
     * The kind of component, such as "tap", "wasd" or "camera".
     * @example if (controls.jump.type === "tap") log.info("tap");
     */
    readonly type: ComponentType;

    /**
     * The component's settings: from profile.json, or for a component made in logic its static props with the values given to it (keys become Key objects, positions points and component ids components).
     * @example log.info(controls.fire.props.rateHz);
     */
    readonly props: Readonly<Record<string, any>>;

    /**
     * Its main key, such as "KeyF" or "Mouse.Right"; null when it has none.
     * @example log.info(controls.fire.key);
     */
    readonly key: InputName | null;

    /**
     * Every key of the component by slot, such as { up: "KeyW" }.
     * @example log.info(controls.move.inputs.up);
     */
    readonly inputs: Readonly<Record<string, InputName>>;

    /**
     * Its key hint on the picture.
     * @example controls.fire.hint.hide();
     */
    readonly hint: Hint;

    /**
     * Whether the control reacts to its keys. Setting it lasts until the script stops or the profile changes, and ends a binding made with bind().
     * @example controls.fire.enabled = false;
     */
    enabled: boolean;

    /**
     * Turns the component on (the same as enabled = true).
     * @example controls.fire.enable();
     */
    enable(): void;

    /**
     * Turns the component off.
     * @example controls.fire.disable();
     */
    disable(): void;

    /**
     * Turns the component on or off.
     * @example controls.fire.toggle();
     */
    toggle(): void;

    /**
     * Whether its key is held down now.
     * @example if (controls.aim.active) log.info("aiming");
     */
    readonly active: boolean;

    /**
     * When its key went down (time.now()), or null when it is up.
     * @example log.info(controls.aim.pressedAt);
     */
    readonly pressedAt: number | null;

    /**
     * How long its key has been held, in milliseconds; 0 when it is up.
     * @example if (controls.aim.heldMs > 1000) log.info("long");
     */
    readonly heldMs: number;

    /**
     * Whether it has a finger on the phone's screen now.
     * @example if (controls.fire.touching) log.info("firing");
     */
    readonly touching: boolean;

    /**
     * Presses and releases the control with its own behavior; the script's own output never triggers scripts.
     * @example await controls.jump.trigger();
     */
    trigger(): Action<void>;

    /**
     * Holds the control down for a while, then releases it; released early if the task is cancelled. Throws GS-API-003 when the duration is not more than 0 and at most 60000.
     * @example await controls.sprint.hold(1500);
     */
    hold(durationMs: number): Action<void>;

    /**
     * Puts its finger down and keeps it there until release().
     * @example controls.fire.press();
     */
    press(): void;

    /**
     * Lifts its finger.
     * @example controls.fire.release();
     */
    release(): void;

    /**
     * Lifts all its fingers and puts it back at rest: a stick back to the middle, a view back to its anchor.
     * @example controls.move.reset();
     */
    reset(): void;

    /**
     * Moves the component and its key hint to another place on the picture until the logic stops or the profile changes; a finger already down stays where it is, and profile.save() can keep the new place. Throws GS-API-002 for a place outside the picture and GS-API-036 for a component made in logic with new.
     * @example controls.fire.moveTo(0.85, 0.7);
     */
    moveTo(x: number | PicturePoint, y?: number): void;

    /**
     * Moves the component by picture ratios, as moveTo() does.
     * @example controls.fire.moveBy(-0.02, 0);
     */
    moveBy(dx: number, dy: number): void;

    /**
     * Puts the component back where the profile has it.
     * @example controls.fire.resetPosition();
     */
    resetPosition(): void;

    /**
     * Puts back everything the logic changed on this component: its place, rate, duration, radius and sensitivity.
     * @example controls.fire.resetProps();
     */
    resetProps(): void;

    /**
     * Keeps a property computed from states: compute runs at once and again whenever a state it read last time changes, also when the script changes it. Only "enabled" can be bound. Disposing the binding, or the end of the task that made it, puts the property back to its value from before the binding; binding it again takes over without putting it back, and setting it directly, or with mapping.setEnabled, ends the binding. Throws GS-API-031 for another property, GS-API-008 for a compute that is not a function and GS-API-032 when compute returns a Promise. Later, a compute that throws is reported and keeps the last value, and one that returns a Promise is reported with GS-API-032 and ends the binding as dispose() does.
     * @example controls.fire.bind("enabled", () => !vars.aiming.value);
     */
    bind(property: "enabled", compute: () => unknown): Binding;

    /**
     * Starts a task each time the control's key goes down. In the profile's logic it takes the key of a tap, hold, long press, turbo, swipe or multi-press control unless consume is false (KeyListenOptions). Throws GS-API-008 for a handler that is not a function, GS-API-009 for an unknown again rule and GS-API-011 for a consume that is not true or false.
     * @example controls.jump.onPress(async () => { await controls.crouch.trigger(); });
     */
    onPress(handler: (event: ControlEvent) => void | Promise<void>, options?: KeyListenOptions): Subscription;

    /**
     * Starts a task each time the control's key comes up. It takes the key as onPress does.
     * @example controls.aim.onRelease(() => log.info("aim off"));
     */
    onRelease(handler: (event: ControlEvent) => void | Promise<void>, options?: KeyListenOptions): Subscription;

    /**
     * Starts a task when the control's key goes down and cancels it when the key comes up. It takes the key as onPress does.
     * @example controls.fire.whileHeld(async ({ signal }) => { while (!signal.aborted) await controls.shoot.trigger(); });
     */
    whileHeld(handler: (event: ControlEvent) => void | Promise<void>, options?: KeyListenOptions): Subscription;

    /**
     * Starts a task each time it is turned on.
     * @example controls.fire.onEnabled(() => log.info("on"));
     */
    onEnabled(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task each time it is turned off.
     * @example controls.fire.onDisabled(() => log.info("off"));
     */
    onDisabled(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task each time it puts a finger on the screen.
     * @example controls.fire.onTouchStart(() => log.info("down"));
     */
    onTouchStart(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task each time it lifts its last finger.
     * @example controls.fire.onTouchEnd(() => log.info("up"));
     */
    onTouchEnd(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * For a component made in logic: runs once after it is created; set up its listeners here. Create components at the top level of the logic so these listeners last.
     * @example setup() { this.props.key.onPress(() => this.finger.down(this.props.at)); }
     */
    setup(): void;

    /**
     * Makes an event the component can emit and others can listen to, kept in a field: pressed = this.event().
     * @example pressed = this.event();
     */
    event(): ComponentEvent;

    /**
     * In a field of a component class: one of its child components, a built-in component the editor places with it and moves with it, such as cancel = this.child(Tap) or skills = [this.child(Skill), this.child(Skill)]. The calls take the children in the order the class declares them; null when the profile has no such child. Throws GS-API-008 when kind is not a class.
     * @example cancel = this.child(Tap);
     */
    child<T extends Component>(kind: { readonly component?: T }): T;

    /**
     * For a component made in logic: its own finger, which stays down until up() or until the logic stops.
     * @example this.finger.down(this.props.at);
     */
    readonly finger: ComponentFinger;
}

/** The key hint of a component on the picture; changes last until the script stops. */
interface Hint {
    /**
     * Shows this hint again after hide().
     * @example controls.fire.hint.show();
     */
    show(): void;

    /**
     * Hides this hint on the picture; the component still works.
     * @example controls.fire.hint.hide();
     */
    hide(): void;

    /**
     * Writes other words in the hint, up to 16 characters.
     * @example controls.fire.hint.setText("Auto");
     */
    setText(text: string): void;

    /**
     * Shows the hint at another place on the picture; the component itself still touches where the profile has it.
     * @example controls.fire.hint.moveTo(0.5, 0.9);
     */
    moveTo(x: number | PicturePoint, y?: number): void;

    /**
     * Makes the hint flash twice to draw the eye to it; nothing else changes.
     * @example controls.fire.hint.flash();
     */
    flash(): void;

    /**
     * Puts the hint back as the profile has it.
     * @example controls.fire.hint.reset();
     */
    reset(): void;
}

/** A tap component: each key press taps the screen once. */
interface Tap extends Component {
    /**
     * How long each tap holds the finger down, in milliseconds; 0 means two frames.
     * @example log.info(controls.jump.tapMs);
     */
    readonly tapMs: number;

    /**
     * Taps once, holding the finger down for tapMs (60 milliseconds when tapMs is 0).
     * @example await controls.jump.tap();
     */
    tap(): Action<void>;

    /**
     * Taps count times (1 to 100), gapMs apart (100 by default), each as long as tap().
     * @example await controls.jump.tapTimes(3, 80);
     */
    tapTimes(count: number, gapMs?: number): Action<void>;

    /**
     * Changes how long each tap holds the finger down (0 to 1000 milliseconds; 0 means two frames) until the logic stops or the profile changes. Throws GS-API-017 outside that range.
     * @example controls.jump.setDuration(60);
     */
    setDuration(durationMs: number): void;

    /**
     * Puts back the tap length from the profile.
     * @example controls.jump.resetDuration();
     */
    resetDuration(): void;
}

/** A hold component: the finger stays down while the key is held. */
interface Hold extends Component {
    /**
     * Whether it is pressing the screen now.
     * @example if (controls.scope.holding) controls.fire.press();
     */
    readonly holding: boolean;

    /**
     * Presses for a while, then lets go.
     * @example await controls.scope.holdFor(800);
     */
    holdFor(durationMs: number): Action<void>;

    /**
     * Presses if it is up, lets go if it is pressing.
     * @example controls.scope.toggleHold();
     */
    toggleHold(): void;

    /**
     * Starts a task each time it puts a finger on the screen.
     * @example controls.scope.onPressed(() => log.info("scoped"));
     */
    onPressed(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task each time it lifts its last finger.
     * @example controls.scope.onReleased(() => log.info("unscoped"));
     */
    onReleased(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** A long-press component: each key press holds the screen for holdMs. */
interface LongPress extends Component {
    /**
     * How long a long press lasts, in milliseconds.
     * @example log.info(controls.bag.holdMs);
     */
    readonly holdMs: number;

    /**
     * Whether it is pressing the screen now.
     * @example log.info(controls.bag.holding);
     */
    readonly holding: boolean;

    /**
     * Long-presses for holdMs, or for durationMs when given.
     * @example await controls.bag.longPress();
     */
    longPress(durationMs?: number): Action<void>;

    /**
     * Changes how long a long press lasts (100 to 10000 milliseconds) until the logic stops or the profile changes. Throws GS-API-017 outside that range.
     * @example controls.bag.setDuration(1500);
     */
    setDuration(durationMs: number): void;

    /**
     * Puts back the long press length from the profile.
     * @example controls.bag.resetDuration();
     */
    resetDuration(): void;

    /**
     * Starts a task each time it puts a finger on the screen.
     * @example controls.bag.onStart(() => log.info("start"));
     */
    onStart(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task each time it lifts its last finger.
     * @example controls.bag.onEnd(() => log.info("end"));
     */
    onEnd(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** A turbo component: holding the key taps again and again. */
interface Turbo extends Component {
    /**
     * Presses a second: from the profile, or the rate setRate() gave.
     * @example log.info(controls.burst.rateHz);
     */
    readonly rateHz: number;

    /**
     * How long each tap holds the finger down, in milliseconds; 0 means two frames.
     * @example log.info(controls.burst.tapMs);
     */
    readonly tapMs: number;

    /**
     * Whether start() is pressing again and again now.
     * @example if (!controls.burst.firing) controls.burst.start();
     */
    readonly firing: boolean;

    /**
     * Presses again and again until stop(), at rateHz or the given rate (1 to 30 a second).
     * @example controls.burst.start(15);
     */
    start(rateHz?: number): void;

    /**
     * Stops start().
     * @example controls.burst.stop();
     */
    stop(): void;

    /**
     * Presses count times (1 to 100) at its rate.
     * @example await controls.burst.burst(5);
     */
    burst(count: number): Action<void>;

    /**
     * Changes the presses a second (1 to 30) for its key, start() and burst(), until the logic stops or the profile changes. Throws GS-API-017 outside that range.
     * @example controls.burst.setRate(12);
     */
    setRate(rateHz: number): void;

    /**
     * Puts back the rate from the profile.
     * @example controls.burst.resetRate();
     */
    resetRate(): void;
}

/** A swipe component: each key press swipes along its path. */
interface Swipe extends Component {
    /**
     * How long the swipe takes, in milliseconds.
     * @example log.info(controls.slide.durationMs);
     */
    readonly durationMs: number;

    /**
     * Whether it is swiping now.
     * @example log.info(controls.slide.swiping);
     */
    readonly swiping: boolean;

    /**
     * Swipes along its path once.
     * @example await controls.slide.swipe();
     */
    swipe(): Action<void>;

    /**
     * Changes how long the swipe takes (10 to 10000 milliseconds) until the logic stops or the profile changes. Throws GS-API-017 outside that range.
     * @example controls.slide.setDuration(350);
     */
    setDuration(durationMs: number): void;

    /**
     * Puts back the swipe length from the profile.
     * @example controls.slide.resetDuration();
     */
    resetDuration(): void;

    /**
     * Starts a task each time it puts a finger on the screen.
     * @example controls.slide.onStart(() => log.info("start"));
     */
    onStart(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task each time it lifts its last finger.
     * @example controls.slide.onEnd(() => log.info("end"));
     */
    onEnd(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** A pinch component: two keys zoom in and out with two fingers. */
interface Pinch extends Component {
    /**
     * Whether it is pinching now.
     * @example log.info(controls.zoom.pinching);
     */
    readonly pinching: boolean;

    /**
     * Spreads two fingers apart once.
     * @example controls.zoom.zoomIn();
     */
    zoomIn(): void;

    /**
     * Pinches two fingers together once.
     * @example controls.zoom.zoomOut();
     */
    zoomOut(): void;
}

/** A stick: WASD wheel or gamepad stick. */
interface Stick extends Component {
    /**
     * The stick's radius, as a ratio of the picture height.
     * @example log.info(controls.move.radius);
     */
    readonly radius: number;

    /**
     * Whether the stick is pushed now.
     * @example if (controls.move.moving) log.info("moving");
     */
    readonly moving: boolean;

    /**
     * Where the stick points in degrees (0 up, 90 right, 180 down, 270 left); null when it is in the middle.
     * @example log.info(controls.move.direction);
     */
    readonly direction: number | null;

    /**
     * How far the stick is pushed, from 0 to 1.
     * @example log.info(controls.move.amount);
     */
    readonly amount: number;

    /**
     * Pushes the stick toward a direction in degrees (0 up, 90 right, 180 down, 270 left), amount from 0 to 1 (1 by default), until release().
     * @example controls.move.push(90);
     */
    push(direction: number, amount?: number): void;

    /**
     * Lets the stick go back to the middle after push().
     * @example controls.move.release();
     */
    release(): void;

    /**
     * Pushes the stick toward a direction in degrees (0 up, 90 right, 180 down, 270 left) for a while, then lets it go.
     * @example await controls.move.walkFor(0, 1500);
     */
    walkFor(direction: number, durationMs: number, amount?: number): Action<void>;

    /**
     * Changes the stick's radius (0.01 to 1, a ratio of the picture height) until the logic stops or the profile changes. Throws GS-API-017 outside that range.
     * @example controls.move.setRadius(0.15);
     */
    setRadius(radius: number): void;

    /**
     * Puts back the radius from the profile.
     * @example controls.move.resetRadius();
     */
    resetRadius(): void;

    /**
     * Starts a task each time the stick starts moving.
     * @example controls.move.onMoveStart(() => log.info("go"));
     */
    onMoveStart(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task each time the stick comes back to the middle.
     * @example controls.move.onStop(() => log.info("stop"));
     */
    onStop(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task each time the stick points another way.
     * @example controls.move.onDirectionChange(({ direction }) => log.info(direction));
     */
    onDirectionChange(handler: (event: DirectionEvent) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** A WASD wheel: four keys push the stick, with walk and sprint keys. */
interface Wasd extends Stick {
    /**
     * How far the walk key pushes the stick, from the profile.
     * @example log.info(controls.move.walkScale);
     */
    readonly walkScale: number;

    /**
     * How far the sprint key pushes the stick, from the profile.
     * @example log.info(controls.move.sprintScale);
     */
    readonly sprintScale: number;
}

/** A gamepad stick component. */
interface PadStick extends Stick {}

/** A right-click walk component: a click walks the hero toward the pointer. */
interface RightWalk extends Component {
    /**
     * Whether it is walking now.
     * @example log.info(controls.walk.walking);
     */
    readonly walking: boolean;

    /**
     * How long it walks to cross the picture's height, in milliseconds.
     * @example log.info(controls.walk.walkMsPerUnit);
     */
    readonly walkMsPerUnit: number;

    /**
     * Whether letting go of its key stops the walk.
     * @example log.info(controls.walk.stopOnRelease);
     */
    readonly stopOnRelease: boolean;

    /**
     * Walks toward a position on the picture, as a click there would.
     * @example controls.walk.walkTo(0.7, 0.4);
     */
    walkTo(x: number | PicturePoint, y?: number): void;

    /**
     * Stops walking.
     * @example controls.walk.stop();
     */
    stop(): void;

    /**
     * Changes the radius of its stick (0.01 to 1, a ratio of the picture height) until the logic stops or the profile changes. Throws GS-API-017 outside that range.
     * @example controls.walk.setRadius(0.15);
     */
    setRadius(radius: number): void;

    /**
     * Puts back the radius from the profile.
     * @example controls.walk.resetRadius();
     */
    resetRadius(): void;

    /**
     * Starts a task each time it starts walking.
     * @example controls.walk.onWalkStart(() => log.info("walk"));
     */
    onWalkStart(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task each time it stops walking.
     * @example controls.walk.onStop(() => log.info("arrived"));
     */
    onStop(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** A view component: the finger that turns the view. */
interface Look extends Component {
    /**
     * Whether its finger is turning the view now.
     * @example log.info(controls.view.turning);
     */
    readonly turning: boolean;

    /**
     * Lifts its finger so the next turn starts from its anchor again.
     * @example controls.view.recenter();
     */
    recenter(): void;

    /**
     * Turns the view as if the mouse moved dx and dy counts.
     * @example controls.view.turn(120, 0);
     */
    turn(dx: number, dy: number): void;

    /**
     * Changes how far the view turns for each mouse or stick count (0.01 to 100; y is x when left out) until the logic stops or the profile changes. Throws GS-API-017 outside that range.
     * @example controls.view.setSensitivity(1.5);
     */
    setSensitivity(x: number, y?: number): void;

    /**
     * Puts back the sensitivity from the profile.
     * @example controls.view.resetSensitivity();
     */
    resetSensitivity(): void;

    /**
     * Starts a task each time it puts a finger on the screen.
     * @example controls.view.onTurnStart(() => log.info("turn"));
     */
    onTurnStart(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task each time it lifts its last finger.
     * @example controls.view.onTurnEnd(() => log.info("still"));
     */
    onTurnEnd(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** A camera component: the mouse turns the view while it is locked. */
interface Camera extends Look {
    /**
     * Whether the view is locked to the mouse now.
     * @example if (!controls.view.locked) controls.view.lock();
     */
    readonly locked: boolean;

    /**
     * Locks the view to the mouse, as the lock key does.
     * @example controls.view.lock();
     */
    lock(): void;

    /**
     * Gives the mouse back.
     * @example controls.view.unlock();
     */
    unlock(): void;

    /**
     * Locks or unlocks the view.
     * @example controls.view.toggleLock();
     */
    toggleLock(): void;

    /**
     * Starts a task each time the view is locked.
     * @example controls.view.onLock(() => hints.hide());
     */
    onLock(handler: (event: TaskContext) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task each time the view is unlocked.
     * @example controls.view.onUnlock(() => hints.show());
     */
    onUnlock(handler: (event: TaskContext) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** A gamepad camera component: a gamepad stick turns the view. */
interface PadCamera extends Look {}

/** A free-look component: holding the key looks around without turning. */
interface FreeLook extends Component {
    /**
     * Whether it is looking around now.
     * @example log.info(controls.peek.looking);
     */
    readonly looking: boolean;

    /**
     * Starts looking around, as holding its key does.
     * @example controls.peek.begin();
     */
    begin(): void;

    /**
     * Stops looking around.
     * @example controls.peek.end();
     */
    end(): void;

    /**
     * Turns the view as if the mouse moved dx and dy counts; it only turns while it is looking around (begin() or its key).
     * @example controls.peek.turn(-200, 0);
     */
    turn(dx: number, dy: number): void;

    /**
     * Changes how far looking around turns for each mouse count (0.01 to 100; y is x when left out) until the logic stops or the profile changes. Throws GS-API-017 outside that range.
     * @example controls.peek.setSensitivity(0.8);
     */
    setSensitivity(x: number, y?: number): void;

    /**
     * Puts back the sensitivity from the profile.
     * @example controls.peek.resetSensitivity();
     */
    resetSensitivity(): void;

    /**
     * Starts a task each time it puts a finger on the screen.
     * @example controls.peek.onStart(() => log.info("peek"));
     */
    onStart(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task each time it lifts its last finger.
     * @example controls.peek.onEnd(() => log.info("back"));
     */
    onEnd(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** A skill component: press to aim, release or click to cast. */
interface Skill extends Component {
    /**
     * Whether it is aiming now.
     * @example log.info(controls.skill1.aiming);
     */
    readonly aiming: boolean;

    /**
     * Starts aiming, as pressing its key does.
     * @example controls.skill1.aim();
     */
    aim(): void;

    /**
     * Casts the skill where it is aiming.
     * @example controls.skill1.cast();
     */
    cast(): void;

    /**
     * Cancels the skill.
     * @example controls.skill1.cancel();
     */
    cancel(): void;

    /**
     * Turns the view as if the mouse moved dx and dy counts.
     * @example controls.skill1.turn(50, 0);
     */
    turn(dx: number, dy: number): void;

    /**
     * Changes how far the aiming finger can move from the skill's place (0.01 to 1, a ratio of the picture height) until the logic stops or the profile changes. Throws GS-API-017 outside that range.
     * @example controls.skill1.setRadius(0.2);
     */
    setRadius(radius: number): void;

    /**
     * Puts back the radius from the profile.
     * @example controls.skill1.resetRadius();
     */
    resetRadius(): void;

    /**
     * Changes how far aiming moves for each mouse count (0.01 to 100; y is x when left out) until the logic stops or the profile changes. Throws GS-API-017 outside that range.
     * @example controls.skill1.setSensitivity(1.2);
     */
    setSensitivity(x: number, y?: number): void;

    /**
     * Puts back the sensitivity from the profile.
     * @example controls.skill1.resetSensitivity();
     */
    resetSensitivity(): void;

    /**
     * Starts a task each time it starts aiming.
     * @example controls.skill1.onAimStart(() => { controls.move.enabled = false; });
     */
    onAimStart(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task each time the skill is cast.
     * @example controls.skill1.onCast(() => { controls.move.enabled = true; });
     */
    onCast(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task each time the skill is cancelled.
     * @example controls.skill1.onCancel(() => log.info("cancelled"));
     */
    onCancel(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** A direction wheel: holding its key presses the middle of the game's wheel and the mouse picks a cell. */
interface Dial extends Component {
    /**
     * Whether the finger is down on the wheel now.
     * @example if (controls.wheel.open) log.info(controls.wheel.cell);
     */
    readonly open: boolean;

    /**
     * The cell the finger is on now, counted from 0 at the first cell going clockwise; -1 while it is in the middle or the key is up.
     * @example log.info(controls.wheel.cell);
     */
    readonly cell: number;

    /**
     * How many cells the wheel has, from the profile.
     * @example log.info(controls.wheel.cells);
     */
    readonly cells: number;

    /**
     * Presses the middle of the wheel, slides to the cell (0 is the first, going clockwise) and lets go, as if its key were pressed and the mouse moved that way. Throws GS-API-017 when the cell is not a whole number from 0 to cells - 1.
     * @example await controls.wheel.choose(2);
     */
    choose(cell: number): Action<void>;

    /**
     * Starts a task each time it puts a finger on the screen.
     * @example controls.wheel.onOpen(() => log.info("open"));
     */
    onOpen(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task each time the key is let go with the finger on a cell; the event says which.
     * @example controls.wheel.onChoose(({ cell }) => log.info("picked", cell));
     */
    onChoose(handler: (event: CellEvent) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** A multi-press component: the key holds several places of the screen at once. */
interface MultiPress extends Component {
    /**
     * How many places it presses together, from 2 to 10.
     * @example log.info(controls.peekFire.places);
     */
    readonly places: number;

    /**
     * Whether it is pressing the screen now.
     * @example if (controls.peekFire.holding) log.info("peeking and firing");
     */
    readonly holding: boolean;

    /**
     * Presses every place for a while, then lets go.
     * @example await controls.peekFire.holdFor(800);
     */
    holdFor(durationMs: number): Action<void>;

    /**
     * Presses if it is up, lets go if it is pressing.
     * @example controls.peekFire.toggleHold();
     */
    toggleHold(): void;

    /**
     * Starts a task each time it puts a finger on the screen.
     * @example controls.peekFire.onPressed(() => log.info("down"));
     */
    onPressed(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task each time it lifts its last finger.
     * @example controls.peekFire.onReleased(() => log.info("up"));
     */
    onReleased(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** A temporary cursor: holding its key brings the cursor out while the view is locked. */
interface TempCursor extends Component {
    /**
     * Whether it has the cursor out now: its key is held while the view is locked.
     * @example if (controls.cursor.showing) log.info("cursor out");
     */
    readonly showing: boolean;

    /**
     * Starts a task each time it brings the cursor out.
     * @example controls.cursor.onShow(() => hints.hide());
     */
    onShow(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task each time the cursor goes back and the view locks again.
     * @example controls.cursor.onHide(() => hints.show());
     */
    onHide(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** An action wheel: holding its key opens a wheel in the middle of the picture; letting go runs the cell the mouse points at. */
interface ActionWheel extends Component {
    /**
     * Whether the wheel is open in the middle of the picture now.
     * @example if (controls.items.open) log.info(controls.items.cell);
     */
    readonly open: boolean;

    /**
     * The cell the mouse points at now, counted from 0 at the top going clockwise; -1 while it points at the middle or the wheel is closed.
     * @example log.info(controls.items.cell);
     */
    readonly cell: number;

    /**
     * How many cells the wheel has, from the profile.
     * @example log.info(controls.items.cells);
     */
    readonly cells: number;

    /**
     * The names of the cells, from the profile; empty for a cell without a name.
     * @example log.info(controls.items.names.join(", "));
     */
    readonly names: readonly string[];

    /**
     * Opens the wheel, points at the cell (0 is the top, going clockwise) and lets go, so the cell runs as if its key were used. Throws GS-API-017 when the cell is not a whole number from 0 to cells - 1.
     * @example await controls.items.choose(1);
     */
    choose(cell: number): Action<void>;

    /**
     * Starts a task each time its key opens the wheel.
     * @example controls.items.onOpen(() => log.info("open"));
     */
    onOpen(handler: (event: ControlEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task each time the key is let go while pointing at a cell; the event says which cell and its name.
     * @example controls.items.onChoose(({ cell, name }) => log.info("ran", cell, name));
     */
    onChoose(handler: (event: WheelChoice) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** A variable of the profile in use, such as "on a boat" or "aiming". */
interface Variable {
    /**
     * The variable id from the profile.
     * @example log.info(vars.onBoat.id);
     */
    readonly id: string;

    /**
     * The name shown in the editor.
     * @example log.info(vars.onBoat.name);
     */
    readonly name: string;

    /**
     * The current value: true or false, a number or a string. Setting it changes what controls and logic see; throws GS-API-010 for other values or when the profile no longer has the variable.
     * @example vars.onBoat.value = !vars.onBoat.value;
     */
    value: VarValue;

    /**
     * Flips a true or false value.
     * @example vars.onBoat.toggle();
     */
    toggle(): void;

    /**
     * Starts a task each time the value changes, whoever changed it.
     * @example vars.onBoat.onChange(({ value }) => { controls.drive.enabled = Boolean(value); });
     */
    onChange(handler: (event: VarChange) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** A key, mouse button or gamepad button. Listening to it in a profile's logic takes it for the script: it no longer goes to the phone as a key, and mouse buttons count only while the view is locked. A task listening to it hears it without taking it, so the profile's keys and logic still get it. */
interface Key {
    /**
     * The key's name, such as "KeyF".
     * @example log.info(keys.F.name);
     */
    readonly name: InputName;

    /**
     * Whether the key is held down now.
     * @example if (keys.Shift.isDown) log.info("walk");
     */
    readonly isDown: boolean;

    /**
     * When the key went down (time.now()), or null when it is up.
     * @example log.info(keys.F.pressedAt);
     */
    readonly pressedAt: number | null;

    /**
     * How long the key has been held, in milliseconds; 0 when it is up.
     * @example log.info(keys.F.heldMs);
     */
    readonly heldMs: number;

    /**
     * Starts a task each time the key goes down.
     * @example keys.MouseLeft.onPress(() => controls.fire.press());
     */
    onPress(handler: (event: KeyEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task each time the key comes up.
     * @example keys.MouseLeft.onRelease(() => controls.fire.release());
     */
    onRelease(handler: (event: KeyEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task when the key goes down and cancels it when the key comes up.
     * @example keys.E.whileHeld(async ({ signal }) => { while (!signal.aborted) await controls.loot.tap(); });
     */
    whileHeld(handler: (event: KeyEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task when the key is let go within 250 ms of pressing it.
     * @example keys.F.onTap(() => controls.pick.tap());
     */
    onTap(handler: (event: KeyEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task when the key is pressed again within 300 ms of a tap.
     * @example keys.Q.onDoubleTap(() => controls.roll.tap());
     */
    onDoubleTap(handler: (event: KeyEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task when the key has been held for durationMs (500 by default).
     * @example keys.F.onLongPress(500, () => controls.bag.tap());
     */
    onLongPress(durationMs: number, handler: (event: KeyEvent) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** Values shared by every profile and task on this phone until the program closes. */
interface Globals {
    /**
     * Reads a global value, shared by every profile and task on this phone.
     * @example if (globals.get("afk")) return;
     */
    get(name: string): StoreValue | undefined;

    /**
     * Sets a global value; it lasts until the program closes.
     * @example globals.set("afk", true);
     */
    set(name: string, value: StoreValue): void;

    /**
     * Starts a task each time a global value changes.
     * @example globals.onChange("afk", ({ value }) => log.info(value));
     */
    onChange(name: string, handler: (event: GlobalChange) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** A named gate from gate(). */
interface Gate {
    /**
     * Whether the gate is open.
     * @example log.info(gate("loot").isOpen);
     */
    readonly isOpen: boolean;

    /**
     * Opens the gate.
     * @example gate("loot").open();
     */
    open(): void;

    /**
     * Closes the gate.
     * @example gate("loot").close();
     */
    close(): void;

    /**
     * Opens or closes the gate.
     * @example gate("loot").toggle();
     */
    toggle(): void;

    /**
     * Runs the action when the gate is open.
     * @example gate("loot").run(() => controls.pick.tap());
     */
    run<T>(action: () => T): T | undefined;
}

/** A stopwatch from stopwatch(). */
interface Stopwatch {
    /**
     * Milliseconds since it started or was reset.
     * @example log.info(watch.elapsedMs);
     */
    readonly elapsedMs: number;

    /**
     * Starts again from 0.
     * @example watch.reset();
     */
    reset(): void;

    /**
     * Milliseconds since the last lap, or since it started.
     * @example log.info(watch.lap());
     */
    lap(): number;
}

/** A finger the script put down with touch.down(). */
interface Finger {
    /**
     * Whether the finger is still down.
     * @example log.info(finger.down);
     */
    readonly down: boolean;

    /**
     * Moves the finger to a position, at once or over durationMs.
     * @example await finger.move(0.6, 0.7, 300);
     */
    move(x: number | PicturePoint, y?: number, durationMs?: number): Action<void>;

    /**
     * Lifts the finger.
     * @example finger.up();
     */
    up(): void;
}

/** Part of the picture taken by vision.capture(). */
interface CapturedImage {
    /**
     * Where it was taken, as ratios of the picture.
     * @example log.info(shot.region);
     */
    readonly region: Region;

    /**
     * Its width in pixels.
     * @example log.info(shot.width);
     */
    readonly width: number;

    /**
     * Its height in pixels.
     * @example log.info(shot.height);
     */
    readonly height: number;

    /**
     * Keeps it as a template of the script's folder: res/<name>.png and an entry in res/resources.json, replacing the picture of a template with that name; resolves once both are written. Logic keeps running. Throws GS-API-038 when the name is not allowed; rejects with GS-API-038 when the files cannot be written and GS-API-037 when the image is no longer kept.
     * @example await shot.save("Boss icon");
     */
    save(name: string): Promise<void>;
}

/** A task: logic that runs on its own on a phone, started from the Tasks page or the mirror window's task menu. It keeps running when the profile changes. */
interface Task {
    /**
     * The settings chosen for this task on the Tasks page, or the values it declares in static params.
     * @example log.info(this.params.rounds);
     */
    readonly params: TaskParams;

    /**
     * Shows how far the task has come on its card and in the mirror window's task menu.
     * @example this.progress("Round 3 of 10");
     */
    progress(text: string): void;

    /**
     * What the task does once it starts. When it returns the task is done; an error ends it as failed; Stop cancels it.
     * @example async run({ signal }) { while (!signal.aborted) await sleep(1000); }
     */
    run(context: TaskContext): Promise<void>;
}

/** An event a component made in logic emits; others listen with on(). */
interface ComponentEvent {
    /**
     * Starts a task in each listener with { value }.
     * @example this.pressed.emit(1);
     */
    emit(value?: unknown): void;

    /**
     * Starts a task each time the event is emitted.
     * @example toggle.pressed.on(({ value }) => log.info(value));
     */
    on(handler: (event: { readonly value: unknown }) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** The own finger of a component made in logic. */
interface ComponentFinger {
    /**
     * Whether the finger is down.
     * @example log.info(this.finger.isDown);
     */
    readonly isDown: boolean;

    /**
     * Puts the finger down at a position, lifting it first when it is already down; it stays down after the task ends.
     * @example this.finger.down(this.props.at);
     */
    down(x: number | PicturePoint, y?: number): Finger;

    /**
     * Moves the finger, at once or over durationMs.
     * @example await this.finger.move(0.6, 0.7, 200);
     */
    move(x: number | PicturePoint, y?: number, durationMs?: number): Action<void>;

    /**
     * Lifts the finger.
     * @example this.finger.up();
     */
    up(): void;
}

/** The Component base class that components made in logic extend. */
interface ComponentClass {
    /**
     * Makes a component of a class that extends Component, with values for its static props; then calls its setup().
     */
    new (options?: ComponentOptions): Component;

    /**
     * The members every component has.
     */
    readonly prototype: Component;

    /**
     * The interfaces the components of a class implement, written static implements = [Fire]; the classes that extend it implement them too.
     * @example static implements = [Fire];
     */
    readonly implements?: readonly Interface<unknown>[];
}

/** The methods a class that extends a built-in component can write; each event one runs when that event comes. */
interface ComponentOverrides {
    /**
     * Runs once after the component is created; set up its listeners here.
     * @example setup() { log.info(this.name); }
     */
    setup(): void;

    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onPress() { log.info("down"); }
     */
    onPress(event?: ControlEvent): void | Promise<void>;

    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onRelease() { log.info("up"); }
     */
    onRelease(event?: ControlEvent): void | Promise<void>;

    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example async whileHeld({ signal }: ControlEvent) { while (!signal.aborted) await sleep(100); }
     */
    whileHeld(event?: ControlEvent): void | Promise<void>;

    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onEnabled() { log.info("on"); }
     */
    onEnabled(event?: ControlEvent): void | Promise<void>;

    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onDisabled() { log.info("off"); }
     */
    onDisabled(event?: ControlEvent): void | Promise<void>;

    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onTouchStart() { log.info("touch"); }
     */
    onTouchStart(event?: ControlEvent): void | Promise<void>;

    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onTouchEnd() { log.info("lift"); }
     */
    onTouchEnd(event?: ControlEvent): void | Promise<void>;
}

/** The events a class that extends Hold can take on as well. */
interface HoldOverrides extends ComponentOverrides {
    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onPressed() { log.info(this.name); }
     */
    onPressed(event?: ControlEvent): void | Promise<void>;

    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onReleased() { log.info(this.name); }
     */
    onReleased(event?: ControlEvent): void | Promise<void>;
}

/** The events a class that extends LongPress, Swipe or FreeLook can take on as well. */
interface StartEndOverrides extends ComponentOverrides {
    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onStart() { log.info(this.name); }
     */
    onStart(event?: ControlEvent): void | Promise<void>;

    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onEnd() { log.info(this.name); }
     */
    onEnd(event?: ControlEvent): void | Promise<void>;
}

/** The events a class that extends Wasd or PadStick can take on as well. */
interface StickOverrides extends ComponentOverrides {
    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onMoveStart() { log.info("go"); }
     */
    onMoveStart(event?: ControlEvent): void | Promise<void>;

    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onStop() { log.info("stop"); }
     */
    onStop(event?: ControlEvent): void | Promise<void>;

    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onDirectionChange(event?: DirectionEvent) { log.info(event?.direction); }
     */
    onDirectionChange(event?: DirectionEvent): void | Promise<void>;
}

/** The events a class that extends RightWalk can take on as well. */
interface RightWalkOverrides extends ComponentOverrides {
    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onWalkStart() { log.info("walk"); }
     */
    onWalkStart(event?: ControlEvent): void | Promise<void>;

    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onStop() { log.info("arrived"); }
     */
    onStop(event?: ControlEvent): void | Promise<void>;
}

/** The events a class that extends PadCamera can take on as well. */
interface LookOverrides extends ComponentOverrides {
    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onTurnStart() { log.info("turn"); }
     */
    onTurnStart(event?: ControlEvent): void | Promise<void>;

    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onTurnEnd() { log.info("still"); }
     */
    onTurnEnd(event?: ControlEvent): void | Promise<void>;
}

/** The events a class that extends Camera can take on as well. */
interface CameraOverrides extends LookOverrides {
    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onLock() { hints.hide(); }
     */
    onLock(event?: TaskContext): void | Promise<void>;

    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onUnlock() { hints.show(); }
     */
    onUnlock(event?: TaskContext): void | Promise<void>;
}

/** The events a class that extends Skill can take on as well. */
interface SkillOverrides extends ComponentOverrides {
    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onAimStart() { log.info("aim"); }
     */
    onAimStart(event?: ControlEvent): void | Promise<void>;

    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onCast() { log.info("cast"); }
     */
    onCast(event?: ControlEvent): void | Promise<void>;

    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onCancel() { log.info("cancelled"); }
     */
    onCancel(event?: ControlEvent): void | Promise<void>;
}

/** The events a class that extends Dial can take on as well. */
interface DialOverrides extends ComponentOverrides {
    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onOpen() { log.info("open"); }
     */
    onOpen(event?: ControlEvent): void | Promise<void>;

    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onChoose(event?: CellEvent) { log.info(event?.cell); }
     */
    onChoose(event?: CellEvent): void | Promise<void>;
}

/** The events a class that extends TempCursor can take on as well. */
interface TempCursorOverrides extends ComponentOverrides {
    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onShow() { hints.hide(); }
     */
    onShow(event?: ControlEvent): void | Promise<void>;

    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onHide() { hints.show(); }
     */
    onHide(event?: ControlEvent): void | Promise<void>;
}

/** The events a class that extends ActionWheel can take on as well. */
interface ActionWheelOverrides extends ComponentOverrides {
    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onOpen() { log.info("open"); }
     */
    onOpen(event?: ControlEvent): void | Promise<void>;

    /**
     * Write it as a method in a class that extends this built-in component: it runs each time the event comes, as well as the built-in behavior, which cannot be changed. Calling super is not needed and does nothing.
     * @example onChoose(event?: WheelChoice) { log.info(event?.name); }
     */
    onChoose(event?: WheelChoice): void | Promise<void>;
}

/** The Task base class that a task's default export extends. */
interface TaskClass {
    /**
     * GameScrcpy makes the task when it starts; scripts do not create tasks themselves.
     */
    new (): Task;

    /**
     * The members every task has.
     */
    readonly prototype: Task;
}

/** A panel of an extension from ui.panel(), drawn with GameScrcpy's own controls. */
interface UiPanel {
    /**
     * Shows the tree in the panel in place of what it showed; null empties it. GameScrcpy draws at most 30 trees a second, so of quicker ones the last shows. Throws GS-API-075 for a tree that is not right, naming the node, and GS-API-076 when the extension does not declare the panel for this logic.
     * @example stats.render({ type: "column", children: [{ type: "button", id: "go", title: "Go" }] });
     */
    render(tree: UiNode | null): void;

    /**
     * Starts a task each time the user does something in the panel: click on a button; change of a field, number, toggle, choice or slider; submit with Enter in a field; select of a list item; open and close of the panel. Throws GS-API-017 for another event.
     * @example stats.on("click", ({ id }) => log.info("clicked", id));
     */
    on(type: UiEventType, handler: (event: UiEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Closes the mirror window's side panel when it shows this panel.
     * @example stats.close();
     */
    close(): void;
}

/** A status widget of an extension from ui.status(). */
interface UiStatus {
    /**
     * Shows the widget in place of what it showed; null takes it away. Throws GS-API-075 for a tree that is not right and GS-API-076 when the extension does not declare the widget for this logic.
     * @example ui.status("fps").render({ type: "row", children: [{ type: "icon", name: "gauge" }, { type: "text", text: "60" }] });
     */
    render(tree: UiStatusNode | null): void;
}

/** A toolbar button of an extension from ui.toolbar(). */
interface UiToolbar {
    /**
     * Changes what the button shows; null goes back to what the manifest declares. Throws GS-API-075 for a state that is not right and GS-API-076 when the extension does not declare the button for this logic.
     * @example quick.set({ checked: true, title: "Recording" });
     */
    set(state: UiToolbarState | null): void;

    /**
     * Starts a task each time the button is clicked. Throws GS-API-017 for another event.
     * @example quick.on("click", () => log.info("pressed"));
     */
    on(type: "click", handler: (event: UiEvent) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** A menu of an extension on cards, from ui.contextMenu(). */
interface UiContextMenu {
    /**
     * Puts up to 16 items into the More menu of the cards in place of the last ones; null or an empty list takes them away. Throws GS-API-075 for items that are not right and GS-API-076 when the extension does not declare the menu for this logic.
     * @example more.set([{ id: "copy", title: "Copy the id", icon: "copy" }]);
     */
    set(items: readonly UiMenuItem[] | null): void;

    /**
     * Starts a task each time one of the items is picked; event.target says on which card. Throws GS-API-017 for another event.
     * @example more.on("choose", ({ id, target }) => log.info(id, target.kind, target.name));
     */
    on(type: "choose", handler: (event: UiChooseEvent) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** A group of an extension in the tray menu, from ui.tray(). */
interface UiTray {
    /**
     * Puts up to 16 items into the tray menu in place of the last ones; null or an empty list takes them away. Throws GS-API-075 for items that are not right and GS-API-076 when the extension does not declare the group for this logic.
     * @example tray.set([{ id: "pause", title: "Pause", checked: paused }]);
     */
    set(items: readonly UiMenuItem[] | null): void;

    /**
     * Starts a task each time one of the items is picked. Throws GS-API-017 for another event.
     * @example tray.on("choose", ({ id }) => log.info("picked", id));
     */
    on(type: "choose", handler: (event: UiEvent) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** The components of the profile in use, by id. GameScrcpy writes the members for each profile into types/profile.d.ts; use mapping for ids that are only known at run time. */
interface Controls {}

/** The variables of the profile in use, by id; the members come from types/profile.d.ts. */
interface Vars {}

/** The template names of the profile's res/resources.json; GameScrcpy writes the members into types/profile.d.ts. */
interface TemplateNames {}

/** The region names of the profile's res/resources.json; the members come from types/profile.d.ts. */
interface RegionNames {}

/** The point names of the profile's res/resources.json; the members come from types/profile.d.ts. */
interface PointNames {}

/** The path names of the profile's res/resources.json; the members come from types/profile.d.ts. */
interface PathNames {}

/** The color names of the profile's res/resources.json; the members come from types/profile.d.ts. */
interface ColorNames {}

/** The system nodes of the installed native extensions, by extension; GameScrcpy writes the members into types/extensions.d.ts. */
interface Extensions {
    /**
     * Calls a service of an extension, named "<extension id>/<service>" as its manifest declares it in contributes.services, with arguments that its input schema accepts, and gives the result, which its output schema checks. The logic of an extension may call the services of the extensions it lists in dependencies, optional ones too, and its own; your own logic may call any service of an extension that is on. Cancelling the task cancels the call. Throws GS-API-146 when no extension that is on offers the service or its app script has not provided it within 10 seconds, GS-API-147 when this extension does not depend on the one that offers it, GS-API-148 when the arguments or the result do not match the schemas or are not JSON of at most 252 KiB, and GS-API-149 with the service's own message when it fails, or when it takes longer than 10 seconds.
     * @example const sum = await ext.call<number>("alice.tools/add", { a: 1, b: 2 });
     */
    call<T = StoreValue>(service: string, args?: StoreValue): Promise<T>;

    /**
     * Answers the calls of a service that the extension declares in contributes.services with this app script as its script. Each call runs as a task of its own with the permissions that both the calling extension and this one have; what the handler returns is the result (nothing gives null), and when it throws the call fails with its message. Providing the same name again replaces the handler; dispose() stops answering. Throws GS-API-145 when the extension does not declare the service for this script, and GS-API-008 when the handler is not a function.
     * @example ext.provide("add", ({ a, b }) => a + b);
     */
    provide<A = any>(name: string, handler: (args: A, context: ServiceContext) => StoreValue | void | Promise<StoreValue | void>): Subscription;
}

/** A device opened with hardware.open(). Every kind below has these members too. */
interface HardwareDevice {
    /**
     * The id of the device.
     * @example log.info(port.id);
     */
    readonly id: string;

    /**
     * The kind of device.
     * @example log.info(port.kind);
     */
    readonly kind: HardwareKind;

    /**
     * The name of the device, as in the device list.
     * @example log.info(port.name);
     */
    readonly name: string;

    /**
     * Whether it is still open.
     * @example if (!port.isOpen) return;
     */
    readonly isOpen: boolean;

    /**
     * Closes the device, so that it can be opened again.
     * @example await port.close();
     */
    close(): Promise<void>;

    /**
     * Starts a task when the device closes by itself: it was unplugged, failed or the permission was taken back.
     * @example port.onClose(({ reason }) => log.warn("closed", reason));
     */
    onClose(handler: (event: DeviceCloseEvent) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** An open serial port. */
interface SerialDevice extends HardwareDevice {
    /**
     * Always "serial".
     * @example log.info(port.kind);
     */
    readonly kind: "serial";

    /**
     * Sends bytes, or text as UTF-8, up to 65536 bytes at a time; resolves once they are sent.
     * @example await port.write("ping\n");
     */
    write(data: HardwareBytes | string): Promise<void>;

    /**
     * Turns the DTR and RTS lines on or off.
     * @example await port.setSignals({ dtr: false });
     */
    setSignals(signals: SerialSignals): Promise<void>;

    /**
     * Starts a task for each piece of data that comes in; by default the events queue, so none is lost.
     * @example port.onData(({ data }) => log.info(data.length));
     */
    onData(handler: (event: DeviceDataEvent) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** An open USB HID device. Keyboards, mice and keypads are never offered. */
interface HidDevice extends HardwareDevice {
    /**
     * Always "hid".
     * @example log.info(pad.kind);
     */
    readonly kind: "hid";

    /**
     * Sends an output report: its id, 0 when the device numbers none, and the bytes after it.
     * @example await pad.sendReport(0, [5, 255]);
     */
    sendReport(report: number, data: HardwareBytes): Promise<void>;

    /**
     * Sends a feature report: its id and the bytes after it.
     * @example await pad.sendFeature(3, [1]);
     */
    sendFeature(report: number, data: HardwareBytes): Promise<void>;

    /**
     * Reads a feature report; resolves with the bytes after its id.
     * @example const info = await pad.getFeature(1);
     */
    getFeature(report: number): Promise<Uint8Array>;

    /**
     * Starts a task for each input report; by default the events queue, so none is lost.
     * @example pad.onReport(({ report, data }) => log.info(report, data[0]));
     */
    onReport(handler: (event: DeviceReportEvent) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** An open Bluetooth LE device; it stays connected while it is open. */
interface BleDevice extends HardwareDevice {
    /**
     * Always "ble".
     * @example log.info(belt.kind);
     */
    readonly kind: "ble";

    /**
     * The services and characteristics the device offers.
     * @example for (const service of await belt.services()) log.info(service.uuid);
     */
    services(): Promise<readonly BleService[]>;

    /**
     * Reads a characteristic. A UUID may be written short, such as "180f".
     * @example const battery = await belt.read("180f", "2a19");
     */
    read(service: string, characteristic: string): Promise<Uint8Array>;

    /**
     * Writes up to 512 bytes to a characteristic.
     * @example await belt.write("180d", "2a39", [1]);
     */
    write(service: string, characteristic: string, data: HardwareBytes, options?: BleWriteOptions): Promise<void>;

    /**
     * Turns on the notifications of a characteristic and starts a task for each new value; resolves once they are on. Disposing the last subscription of the characteristic turns them off; by default the events queue, so none is lost.
     * @example await belt.subscribe("180d", "2a37", ({ data }) => log.info("heart rate", data[1]));
     */
    subscribe(service: string, characteristic: string, handler: (event: DeviceNotifyEvent) => void | Promise<void>, options?: ListenOptions): Promise<Subscription>;
}

/** Keys and axes that logic sends into the phone's key mapping, made by input.source(). */
interface InputSource {
    /**
     * The name of the input source.
     * @example log.info(deck.name);
     */
    readonly name: string;

    /**
     * Presses a key of the source, named with 1 to 32 letters, digits, _ or -; it stays down until release() or until the logic stops.
     * @example deck.press("k1");
     */
    press(key: string): void;

    /**
     * Lets go of a key of the source.
     * @example deck.release("k1");
     */
    release(key: string): void;

    /**
     * Moves an axis of the source to x and y, each from -1 to 1 (y is 0 when left out); bound to a stick it pushes the stick, and 0, 0 lets it go.
     * @example deck.axis("knob", 0.5, -0.25);
     */
    axis(key: string, x: number, y?: number): void;
}

/** An HTTP server on 127.0.0.1 made by net.listen(). */
interface NetServer {
    /**
     * The port it listens on.
     * @example log.info(server.port);
     */
    readonly port: number;

    /**
     * Whether it still takes requests.
     * @example if (!server.isOpen) return;
     */
    readonly isOpen: boolean;

    /**
     * Stops taking requests and frees the port.
     * @example server.close();
     */
    close(): void;
}

/** Voice commands listened for with speech.listen(). */
interface SpeechListener {
    /**
     * Whether it still listens.
     * @example if (!listener.isOpen) return;
     */
    readonly isOpen: boolean;

    /**
     * Stops listening; the microphone is let go once nothing else listens.
     * @example listener.close();
     */
    close(): void;
}

/** A recognition engine from vision.engine(). */
interface VisionEngine {
    /**
     * The id of the engine.
     * @example log.info(engine.id);
     */
    readonly id: string;

    /**
     * Reads text, as vision.recognize(id, "ocr", region, params) does.
     * @example const lines = await vision.engine("system").read(res.region("Gold"));
     */
    read(region?: Region | RegionName, params?: VisionParams): Promise<readonly VisionText[] | null>;

    /**
     * Finds things, as vision.recognize(id, "detect", region, params) does.
     * @example const enemies = await vision.engine("alice.finder/boss").detect(undefined, { threshold: 0.6 });
     */
    detect(region?: Region | RegionName, params?: VisionParams): Promise<readonly VisionObject[] | null>;

    /**
     * Tells what the picture shows, as vision.recognize(id, "classify", region, params) does.
     * @example const scene = await vision.engine("alice.finder/scene").classify(); log.info(scene?.[0]?.label);
     */
    classify(region?: Region | RegionName, params?: VisionParams): Promise<readonly VisionClass[] | null>;
}

/** A WebSocket connection made by net.socket(). */
interface NetSocket {
    /**
     * The address it is connected to.
     * @example log.info(live.url);
     */
    readonly url: string;

    /**
     * The subprotocol the server took, or an empty text.
     * @example log.info(live.protocol);
     */
    readonly protocol: string;

    /**
     * Whether messages can still be sent.
     * @example if (!live.isOpen) return;
     */
    readonly isOpen: boolean;

    /**
     * Sends a text message, or a binary one for bytes, of at most 1 MiB. Throws GS-API-159 when the socket is closed and GS-API-157 when 16 MiB are waiting to be sent already.
     * @example live.send(JSON.stringify({ subscribe: "scores" }));
     */
    send(data: string | HardwareBytes): void;

    /**
     * Closes the socket with code 1000, or one from 3000 to 4999, and a reason of at most 123 bytes; onClose does not run for it.
     * @example live.close();
     */
    close(code?: number, reason?: string): void;

    /**
     * Starts a task for each message; by default the messages queue, so none is lost.
     * @example live.onMessage(({ data, binary }) => log.info(binary ? "bytes" : data));
     */
    onMessage(handler: (event: NetMessageEvent) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task when the server or the network closes the socket.
     * @example live.onClose(({ code, reason }) => log.warn("closed", code, reason));
     */
    onClose(handler: (event: NetCloseEvent) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** A file the user picked with files.pick(). */
interface FileHandle {
    /**
     * The name of the file, without its folder.
     * @example log.info(file.name);
     */
    readonly name: string;

    /**
     * Whether the handle can still be used.
     * @example if (!file.isOpen) return;
     */
    readonly isOpen: boolean;

    /**
     * Reads the whole file, up to 64 MiB. Throws GS-API-162 when Windows cannot read it or it changes while it is read, GS-API-163 when it is larger and GS-API-164 when the handle is closed.
     * @example const bytes = await file.read();
     */
    read(): Promise<Uint8Array>;

    /**
     * Reads the whole file as UTF-8 text, leaving out a byte order mark.
     * @example const settings = JSON.parse(await file.text());
     */
    text(): Promise<string>;

    /**
     * Replaces the whole file with the text, as UTF-8, or the bytes, up to 64 MiB; a file the user named in the save dialog is made at the first write.
     * @example await file.write(JSON.stringify(settings, null, 2));
     */
    write(data: string | HardwareBytes): Promise<void>;

    /**
     * Gives up the file; the handle can no longer be used.
     * @example file.close();
     */
    close(): void;
}

/** A folder chosen for files.folder(). Paths inside it use / between names; .., drives and links that lead out of it are refused with GS-API-161, and what Windows cannot do with GS-API-162. */
interface FolderHandle {
    /**
     * The name the manifest gives the folder.
     * @example log.info(exports.name);
     */
    readonly name: string;

    /**
     * Whether the handle can still be used.
     * @example if (!exports.isOpen) return;
     */
    readonly isOpen: boolean;

    /**
     * Lists up to 4096 files and folders, sorted by name, at the top of the folder or in a folder inside it.
     * @example for (const entry of await exports.list()) log.info(entry.path, entry.size);
     */
    list(path?: string): Promise<readonly FolderEntry[]>;

    /**
     * Reads a file inside the folder, up to 64 MiB.
     * @example const picture = await exports.read("shots/last.png");
     */
    read(path: string): Promise<Uint8Array>;

    /**
     * Reads a file inside the folder as UTF-8 text, leaving out a byte order mark.
     * @example const notes = await exports.text("notes.txt");
     */
    text(path: string): Promise<string>;

    /**
     * Replaces or makes a file inside the folder, with the folders on its path, up to 64 MiB.
     * @example await exports.write("scores/today.csv", "time,score\n");
     */
    write(path: string, data: string | HardwareBytes): Promise<void>;

    /**
     * Removes a file, or a folder with nothing in it; resolves with false when there was nothing to remove.
     * @example await exports.remove("scores/old.csv");
     */
    remove(path: string): Promise<boolean>;

    /**
     * Gives up the folder; the handle can no longer be used.
     * @example exports.close();
     */
    close(): void;
}

/**
 * Waits without blocking other tasks. Rejects with an AbortError when the task is cancelled.
 * @param ms How long to wait, in milliseconds (0 to 2147483647).
 * @throws RangeError when ms is not a number from 0 to 2147483647. Pass a whole number of milliseconds.
 * @since 1 (stable)
 * @example await sleep(500);
 */
declare function sleep(ms: number): Promise<void>;

/**
 * Waits until the next picture frame is shown.
 * @since 1 (stable)
 * @example await nextFrame();
 */
declare function nextFrame(): Promise<void>;

/**
 * Runs a handler again and again, waiting intervalMs before each run.
 * @param intervalMs Milliseconds between runs, from 1 to 86400000.
 * @param handler What to run; each run is its own task.
 * @param options What happens when the last run is still going.
 * @throws GS-API-003 when the time is out of range. Pass milliseconds in the range given.
 * @throws GS-API-008 when the handler or action is not a function. Pass a function.
 * @since 1 (stable)
 * @example every(500, async () => { if (await vision.find("res/pick.png")) await controls.pick.tap(); });
 */
declare function every(intervalMs: number, handler: (event: TaskContext) => void | Promise<void>, options?: ListenOptions): Subscription;

/**
 * Runs a handler once after a delay, unless it is disposed first.
 * @param delayMs Milliseconds to wait, from 1 to 86400000.
 * @param handler What to run, as its own task.
 * @param options What happens when it is still running from before.
 * @throws GS-API-003 when the time is out of range. Pass milliseconds in the range given.
 * @throws GS-API-008 when the handler or action is not a function. Pass a function.
 * @since 1 (stable)
 * @example after(3000, () => ui.toast("Ready"));
 */
declare function after(delayMs: number, handler: (event: TaskContext) => void | Promise<void>, options?: ListenOptions): Subscription;

/**
 * Waits until a condition gives a truthy value and resolves with it; rejects with a TimeoutError (GS-API-030) when the time is up.
 * @param condition Checked once per frame until it gives a truthy value; may return a Promise.
 * @param options How long to wait, in milliseconds (5000 by default).
 * @throws GS-API-008 when the condition is not a function. Pass a function.
 * @throws GS-API-030 when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
 * @since 1 (stable)
 * @example await waitUntil(() => vars.ready.value, { timeoutMs: 3000 });
 */
declare function waitUntil(condition: () => unknown, options?: UntilOptions): Promise<NonNullable<unknown>>;

/**
 * Runs an action and tries again when it throws, up to times more; a cancelled task is not retried.
 * @param times How many more tries after the first, from 0 to 10.
 * @param action What to try; it gets the number of the try, from 0.
 * @throws GS-API-003 when the time is out of range. Pass milliseconds in the range given.
 * @throws GS-API-008 when the handler or action is not a function. Pass a function.
 * @since 1 (stable)
 * @example await retry(2, () => touch.tap(0.5, 0.9).until(() => vision.find("res/ok.png")));
 */
declare function retry(times: number, action: (attempt: number) => unknown): Promise<unknown>;

/**
 * Runs an action with a time limit: when the time is up its signal aborts and the Promise rejects with a TimeoutError (GS-API-030).
 * @param timeoutMs The time limit in milliseconds, from 1 to 600000.
 * @param action What to run; its signal aborts when the time is up.
 * @throws GS-API-003 when the time is out of range. Pass milliseconds in the range given.
 * @throws GS-API-008 when the handler or action is not a function. Pass a function.
 * @since 1 (stable)
 * @example await withTimeout(5000, async ({ signal }) => { while (!signal.aborted) await controls.fire.trigger(); });
 */
declare function withTimeout(timeoutMs: number, action: (context: TaskContext) => unknown): Promise<unknown>;

/**
 * Runs several things at the same time and waits for all of them.
 * @param work Promises, or functions to start at once.
 * @since 1 (stable)
 * @example await parallel(controls.skill1.trigger(), controls.skill2.trigger());
 */
declare function parallel(...work: Work[]): Promise<unknown[]>;

/**
 * Runs several things one after another, in order, waiting for each before it starts the next.
 * @param work Functions to run one after another, waiting for each before the next.
 * @since 1 (stable)
 * @example await sequence(async () => { await ui.toast("one"); }, async () => { await ui.toast("two"); });
 */
declare function sequence(...work: Work[]): Promise<unknown[]>;

/**
 * Runs the next of several functions each time it is called, in turn or in random order, and one more function after they have all run.
 * @param name The gate's name; the same name keeps the same count.
 * @param times How many outputs it has, from 1 to 1000.
 * @param loop Whether it starts over at the first output after the last one.
 * @param random Whether the outputs come in a random order.
 * @param work One function per output, then one more for after the last.
 * @throws GS-API-011 when the name is not a string or is empty. Pass a name.
 * @throws GS-API-017 when times is not a whole number from 1 to 1000. Pass a whole number of outputs.
 * @since 1 (stable)
 * @example await multiGate("door", 2, false, false, async () => {}, async () => {}, async () => {});
 */
declare function multiGate(name: string, times: number, loop: boolean, random: boolean, ...work: Work[]): Promise<unknown>;

/**
 * Runs several things at the same time and waits for the first to finish.
 * @param work Promises, or functions to start at once.
 * @since 1 (stable)
 * @example await race(sleep(2000), waitUntil(() => keys.Space.isDown));
 */
declare function race(...work: Work[]): Promise<unknown>;

/**
 * Runs an action only the first time it is called with this name.
 * @param name Which one; each name runs once until resetOnce.
 * @param action What to run the first time.
 * @throws GS-API-011 when the name is not a string or is empty. Pass a name.
 * @since 1 (stable)
 * @example doOnce("greet", () => ui.toast("Hello"));
 */
declare function doOnce(name: string, action: () => unknown): unknown;

/**
 * Lets doOnce run the action with this name again.
 * @param name The name you gave it.
 * @throws GS-API-011 when the name is not a string or is empty. Pass a name.
 * @since 1 (stable)
 * @example resetOnce("greet");
 */
declare function resetOnce(name: string): void;

/**
 * Says yes for the first times calls with this name, and no after that, until resetDoN with the same name.
 * @param name Which count; the same name with the same limit keeps counting.
 * @param times How many times it passes before it says no, from 1 to 1000000.
 * @throws GS-API-011 when the name is not a string or is empty. Pass a name.
 * @throws GS-API-017 when times is not a whole number from 1 to 1000000. Pass a whole number of times.
 * @since 1 (stable)
 * @example if (doN("dash", 3)) await controls.dash.tap();
 */
declare function doN(name: string, times: number): boolean;

/**
 * Lets doN with this name pass that many times again.
 * @param name The name you gave it.
 * @throws GS-API-011 when the name is not a string or is empty. Pass a name.
 * @since 1 (stable)
 * @example resetDoN("dash");
 */
declare function resetDoN(name: string): void;

/**
 * A named gate that lets an action through only while it is open.
 * @param name The gate's name; the same name gives the same gate.
 * @param startClosed Whether a new gate starts closed (open by default).
 * @throws GS-API-011 when the name is not a string or is empty. Pass a name.
 * @since 1 (stable)
 * @example gate("loot").close();
 */
declare function gate(name: string, startClosed?: boolean): Gate;

/**
 * Gives "A" and "B" in turn, starting with "A", for each name.
 * @param name The name you gave it.
 * @throws GS-API-011 when the name is not a string or is empty. Pass a name.
 * @since 1 (stable)
 * @example if (flipFlop("light") === "A") controls.light.press(); else controls.light.release();
 */
declare function flipFlop(name: string): "A" | "B";

/**
 * Makes a function that runs the action at most once per interval and ignores calls in between.
 * @param intervalMs The shortest time between two runs, from 1 to 86400000 milliseconds.
 * @param action What to run.
 * @throws GS-API-003 when the time is out of range. Pass milliseconds in the range given.
 * @throws GS-API-008 when the handler or action is not a function. Pass a function.
 * @since 1 (stable)
 * @example const shout = throttle(1000, () => ui.toast("Fire"));
 */
declare function throttle(intervalMs: number, action: (...values: unknown[]) => unknown): (...values: unknown[]) => unknown;

/**
 * Makes a function that runs the action only after the calls have stopped for waitMs.
 * @param waitMs How long it must stay quiet before running, from 1 to 86400000 milliseconds.
 * @param action What to run, with the values of the last call.
 * @throws GS-API-003 when the time is out of range. Pass milliseconds in the range given.
 * @throws GS-API-008 when the handler or action is not a function. Pass a function.
 * @since 1 (stable)
 * @example const save = debounce(500, () => store.set("x", 1));
 */
declare function debounce(waitMs: number, action: (...values: unknown[]) => unknown): (...values: unknown[]) => void;

/**
 * A stopwatch that starts now.
 * @since 1 (stable)
 * @example const watch = stopwatch();
 */
declare function stopwatch(): Stopwatch;

/**
 * A position on the picture, as ratios.
 * @param x Horizontal position as a ratio of the picture width: 0 is the left edge, 1 the right edge.
 * @param y Vertical position as a ratio of the picture height: 0 is the top edge, 1 the bottom edge.
 * @throws GS-API-002 when a position is outside 0 to 1. Positions are picture ratios; divide pixels by the picture size.
 * @since 1 (stable)
 * @example await touch.tap(point(0.5, 0.9));
 */
declare function point(x: number, y: number): PicturePoint;

/**
 * The base class of components made in logic: extend it, declare static props and set them up in setup(); create them with new at the top level of the logic.
 * @example class Toggle extends Component { static props = { key: prop.key("F") }; }
 */
declare const Component: ComponentClass;

/**
 * A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.
 * @example export default class Jump extends Tap { onPress() { log.info("jump"); } }
 */
declare const Tap: BuiltInClass<Tap, ComponentOverrides>;

/**
 * A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.
 * @example export default class Scope extends Hold { onPressed() { log.info("scoped"); } }
 */
declare const Hold: BuiltInClass<Hold, HoldOverrides>;

/**
 * A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.
 * @example export default class Bag extends LongPress { onEnd() { log.info("bag open"); } }
 */
declare const LongPress: BuiltInClass<LongPress, StartEndOverrides>;

/**
 * A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.
 * @example export default class Shots extends Turbo { onRelease() { log.info("stop"); } }
 */
declare const Turbo: BuiltInClass<Turbo, ComponentOverrides>;

/**
 * A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.
 * @example export default class Slide extends Swipe { onEnd() { log.info("slid"); } }
 */
declare const Swipe: BuiltInClass<Swipe, StartEndOverrides>;

/**
 * A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.
 * @example export default class Zoom extends Pinch { onTouchEnd() { log.info("zoomed"); } }
 */
declare const Pinch: BuiltInClass<Pinch, ComponentOverrides>;

/**
 * A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.
 * @example export default class Move extends Wasd { onStop() { log.info("stopped"); } }
 */
declare const Wasd: BuiltInClass<Wasd, StickOverrides>;

/**
 * A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.
 * @example export default class Walk extends RightWalk { onStop() { log.info("arrived"); } }
 */
declare const RightWalk: BuiltInClass<RightWalk, RightWalkOverrides>;

/**
 * A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.
 * @example export default class Stick extends PadStick { onMoveStart() { log.info("go"); } }
 */
declare const PadStick: BuiltInClass<PadStick, StickOverrides>;

/**
 * A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.
 * @example export default class View extends Camera { onLock() { hints.hide(); } }
 */
declare const Camera: BuiltInClass<Camera, CameraOverrides>;

/**
 * A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.
 * @example export default class PadView extends PadCamera { onTurnStart() { log.info("turn"); } }
 */
declare const PadCamera: BuiltInClass<PadCamera, LookOverrides>;

/**
 * A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.
 * @example export default class Peek extends FreeLook { onStart() { log.info("peek"); } }
 */
declare const FreeLook: BuiltInClass<FreeLook, StartEndOverrides>;

/**
 * A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.
 * @example export default class Blink extends Skill { onCast() { log.info("cast"); } }
 */
declare const Skill: BuiltInClass<Skill, SkillOverrides>;

/**
 * A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.
 * @example export default class Weapons extends Dial { onChoose(event?: CellEvent) { log.info(event?.cell); } }
 */
declare const Dial: BuiltInClass<Dial, DialOverrides>;

/**
 * A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.
 * @example export default class PeekFire extends MultiPress { onPressed() { log.info("peek and fire"); } }
 */
declare const MultiPress: BuiltInClass<MultiPress, HoldOverrides>;

/**
 * A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.
 * @example export default class Pointer extends TempCursor { onShow() { hints.hide(); } }
 */
declare const TempCursor: BuiltInClass<TempCursor, TempCursorOverrides>;

/**
 * A built-in component class. A class of yours that extends it is placed in the editor as this component, presses the screen just as it does and runs the event methods it writes as well; children are made with this.child(). Components of these classes only come from the profile: new on them throws GS-API-036.
 * @example export default class Items extends ActionWheel { onChoose(event?: WheelChoice) { log.info(event?.name); } }
 */
declare const ActionWheel: BuiltInClass<ActionWheel, ActionWheelOverrides>;

/**
 * Makes an interface, or gives back the one with this name, so components of different kinds can answer the same call: list it in static implements of their classes, then mapping.all(it) or it.all() gives every component that implements it. Throws GS-API-011 for a name that is not a string or is empty.
 * @example const Fire = defineInterface<{ fire(): void }>("fire");
 */
declare const defineInterface: <T>(name: string) => Interface<T>;

/**
 * The base class of a task. The task.ts of a task exports a class that extends it: static params lists its settings and run() does the work.
 * @example export default class Daily extends Task { async run() { log.info(this.params.rounds); } }
 */
declare const Task: TaskClass;

/**
 * The components of the profile in use, by id; empty without a phone. In a task they are the components of the profile the phone uses now, and are made again from the new profile when the phone switches.
 * @example controls.jump.onPress(() => log.info("jump"));
 */
declare const controls: Controls;

/**
 * The variables of the profile in use, by id; empty without a phone. In a task they follow the phone's profile, as controls do.
 * @example if (vars.onBoat.value) await controls.drive.hold(500);
 */
declare const vars: Vars;

/**
 * Keys, mouse buttons and gamepad buttons by name: keys.F, keys.Space, keys.MouseLeft, keys.GamepadA.
 * @example keys.F.onTap(() => controls.pick.tap());
 */
declare const keys: Keys;

/**
 * Values shared by every profile and task on this phone.
 * @example globals.set("afk", true);
 */
declare const globals: Globals;

/**
 * Runs a state machine: each member of states is a state, an async function that receives to and returns to.<next state>(reason) to move on, or nothing to end the machine. The first member is the start unless options.start names another. The editor shows the state it is in and every transition with its reason. Throws GS-API-008 when states is not an object of functions and GS-API-002 for an unknown start.
 * @example await machine({ async lobby(to) { await vision.findAndTap("start"); return to.queue("tapped start"); }, async queue(to) { return (await vision.waitFor("inGame", { timeoutMs: 120000 })) ? to.game() : to.lobby("timed out"); }, async game() {} }, { name: "farm" });
 */
declare const machine: <S extends string>(states: { readonly [K in S]: (to: MachineTo<S>) => MachineStep | void | Promise<MachineStep | void> }, options?: MachineOptions<S>) => Promise<void>;

/**
 * The system nodes that native extensions add: ext.<extension>.<node>(...) runs a node at once and gives its result. <extension> is the extension id with each part in lowerCamelCase, so the nodes of gamescrcpy.pc-clock are ext.gamescrcpy.pcClock. Throws GS-API-060 when the extension is not on, GS-API-061 for values of the wrong type, GS-API-062 when the node fails, GS-API-063 naming a permission the extension lacks and GS-API-064 when the node takes longer than 100 ms. A job the extension offers, ext.<extension>.<job>({ ... }), starts at once with its arguments by name and gives an ExtensionJob; starting throws GS-API-060, GS-API-061 and GS-API-063 as a node does, and GS-API-067 when the extension runs as many of its jobs as it may. ext.call() and ext.provide() call and answer the services that extensions offer each other.
 * @example log.info(ext.gamescrcpy.pcClock.now());
 */
declare const ext: Extensions;

/** Lines in the script console, with the task that wrote them. */
declare namespace log {
    /**
     * Writes an information line to the script console.
     * @param values Values to write, joined with spaces.
     * @since 1 (stable)
     * @example log.info("started", 3);
     */
    function info(...values: unknown[]): void;

    /**
     * Writes a warning line to the script console.
     * @param values Values to write, joined with spaces.
     * @since 1 (stable)
     * @example log.warn("slow");
     */
    function warn(...values: unknown[]): void;

    /**
     * Writes an error line to the script console.
     * @param values Values to write, joined with spaces.
     * @since 1 (stable)
     * @example log.error("missing");
     */
    function error(...values: unknown[]): void;

    /**
     * Starts a timer with this name; log.timeEnd() with the same name writes how long it has been running.
     * @param name The timer's name.
     * @throws GS-API-011 when the name is not a string or is empty. Pass a name.
     * @since 1 (stable)
     * @example log.time("search");
     */
    function time(name: string): void;

    /**
     * Writes an information line with the milliseconds since log.time() with this name and gives that number; with no such timer it writes a warning line and gives null.
     * @param name The timer's name.
     * @throws GS-API-011 when the name is not a string or is empty. Pass a name.
     * @since 1 (stable)
     * @example log.time("search"); await vision.find("res/chest.png"); log.timeEnd("search");
     */
    function timeEnd(name: string): number | null;
}

/** The script clock. */
declare namespace time {
    /**
     * Milliseconds since the scripts started; virtual time when replaying a recording.
     * @since 1 (stable)
     * @example const started = time.now();
     */
    function now(): number;
}

/** Touches on the phone picture. Positions are ratios of the picture: (0, 0) top left, (1, 1) bottom right. */
declare namespace touch {
    /**
     * Taps the picture at a position; resolves when the finger is lifted, which is at once if the task is cancelled.
     * @param x Where to tap: a ratio of the picture width, or a point.
     * @param y A ratio of the picture height; with a point, the duration instead.
     * @param durationMs How long the finger stays down, in milliseconds (default 60).
     * @throws GS-API-002 when a position is outside 0 to 1. Positions are picture ratios; divide pixels by the picture size.
     * @throws GS-API-003 when the duration is not more than 0 and at most 60000. Pass a duration in milliseconds.
     * @throws GS-API-030 when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example await touch.tap(0.8, 0.9).until(() => vision.find("res/playing.png"), { timeoutMs: 10000, retry: 1 });
     */
    function tap(x: number, y: number, durationMs?: number): Action<void>;

    function tap(x: PicturePoint, durationMs?: number): Action<void>;

    function tap(x: PicturePoint, y?: undefined, durationMs?: number): Action<void>;

    /**
     * Presses and holds the picture at a position, then lifts the finger.
     * @param x Where to press: a ratio of the picture width, or a point.
     * @param y A ratio of the picture height; with a point, the duration instead.
     * @param durationMs How long the finger stays down, in milliseconds (default 800).
     * @throws GS-API-002 when a position is outside 0 to 1. Positions are picture ratios; divide pixels by the picture size.
     * @throws GS-API-003 when the duration is not more than 0 and at most 60000. Pass a duration in milliseconds.
     * @throws GS-API-030 when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example await touch.longPress(point(0.5, 0.5), 1200);
     */
    function longPress(x: number, y: number, durationMs?: number): Action<void>;

    function longPress(x: PicturePoint, durationMs?: number): Action<void>;

    function longPress(x: PicturePoint, y?: undefined, durationMs?: number): Action<void>;

    /**
     * Swipes from one position to another; resolves when it ends. If the task is cancelled, the finger is lifted where it is.
     * @param fromX Start: a ratio of the picture width, or a point.
     * @param fromY Start height as a ratio; with points, the end point.
     * @param toX End, as a ratio of the picture width; with points, the duration.
     * @param toY End, as a ratio of the picture height.
     * @param durationMs How long the swipe takes, in milliseconds (default 300).
     * @throws GS-API-002 when a position is outside 0 to 1. Positions are picture ratios; divide pixels by the picture size.
     * @throws GS-API-003 when the duration is not more than 0 and at most 60000. Pass a duration in milliseconds.
     * @throws GS-API-030 when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example await touch.swipe(0.5, 0.8, 0.5, 0.2, 250);
     */
    function swipe(fromX: number, fromY: number, toX: number, toY: number, durationMs?: number): Action<void>;

    function swipe(from: PicturePoint, to: PicturePoint, durationMs?: number): Action<void>;

    function swipe(fromX: PicturePoint, fromY: undefined, toX: PicturePoint, toY?: undefined, durationMs?: number): Action<void>;

    function swipe(fromX: PicturePoint, fromY: undefined, toX: number, toY: number, durationMs?: number): Action<void>;

    function swipe(fromX: number, fromY: number, toX: PicturePoint, toY?: undefined, durationMs?: number): Action<void>;

    /**
     * Puts a finger down and keeps it there until up() or the end of the task that put it down.
     * @param x Where to put the finger: a ratio of the picture width, or a point.
     * @param y A ratio of the picture height; leave it out with a point.
     * @throws GS-API-002 when a position is outside 0 to 1. Positions are picture ratios; divide pixels by the picture size.
     * @throws GS-API-003 when the duration is not more than 0 and at most 60000. Pass a duration in milliseconds.
     * @throws GS-API-030 when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example const finger = touch.down(0.3, 0.7); await finger.move(0.6, 0.7, 300); finger.up();
     */
    function down(x: number, y: number): Finger;

    function down(x: PicturePoint): Finger;

    /**
     * Pinches with two fingers around a position: from is how far apart they start and to how far apart they end, as ratios of the picture width, so a growing distance zooms in. If the task is cancelled, both fingers are lifted.
     * @param center The position between the two fingers.
     * @param from How far apart the fingers start, from 0 to 1.
     * @param to How far apart they end, from 0 to 1.
     * @param durationMs How long the fingers take to move; 300 when not given.
     * @throws GS-API-002 when a position is outside 0 to 1. Positions are picture ratios; divide pixels by the picture size.
     * @throws GS-API-003 when the duration is not more than 0 and at most 60000. Pass a duration in milliseconds.
     * @throws GS-API-030 when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example await touch.pinch(point(0.5, 0.5), 0.1, 0.4, 300);
     */
    function pinch(center: PicturePoint, from: number, to: number, durationMs?: number): Action<void>;

    /**
     * Lifts every finger the script has put down.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example touch.releaseAll();
     */
    function releaseAll(): void;
}

/** The phone this script runs on. */
declare namespace device {
    /**
     * The phone this script runs on: model, Android version, screen size and rotation.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example const { width, height } = device.info();
     */
    function info(): DeviceInfo;

    /**
     * Presses the phone's Back key.
     * @throws GS-API-030 when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example await device.back();
     */
    function back(): Action<void>;

    /**
     * Presses the phone's Home key.
     * @throws GS-API-030 when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example await device.home();
     */
    function home(): Action<void>;

    /**
     * Opens the list of recent apps.
     * @throws GS-API-030 when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example await device.recents();
     */
    function recents(): Action<void>;

    /**
     * Presses the phone's Menu key.
     * @throws GS-API-030 when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example await device.menu();
     */
    function menu(): Action<void>;

    /**
     * Presses the phone's Power key.
     * @throws GS-API-030 when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example await device.power();
     */
    function power(): Action<void>;

    /**
     * Turns the phone's volume up one step.
     * @throws GS-API-030 when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example await device.volumeUp();
     */
    function volumeUp(): Action<void>;

    /**
     * Turns the phone's volume down one step.
     * @throws GS-API-030 when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example await device.volumeDown();
     */
    function volumeDown(): Action<void>;

    /**
     * Mutes or unmutes the phone.
     * @throws GS-API-030 when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example await device.mute();
     */
    function mute(): Action<void>;

    /**
     * Pulls down the phone's notification shade, as the More menu of the mirror window's toolbar does; true once the phone is asked, false when it cannot be asked now, such as while GameScrcpy reconnects to it. When the phone does not allow it, its mirror window says so. The shade stays down when the logic stops.
     * @throws GS-API-090 when the logic does not run on a phone. Start the script on a phone that is mirroring.
     * @throws GS-API-091 when the phone is connected without the GameScrcpy server, such as in a window without a picture or through a capture card. Mirror the phone over USB or WiFi to reach its notification shade.
     * @since 1 (stable)
     * @example if (device.notifications()) await sleep(800);
     */
    function notifications(): boolean;

    /**
     * Opens the phone's quick settings, as the More menu of the mirror window's toolbar does; true once the phone is asked, false when it cannot be asked now. They stay open when the logic stops.
     * @throws GS-API-090 when the logic does not run on a phone. Start the script on a phone that is mirroring.
     * @throws GS-API-091 when the phone is connected without the GameScrcpy server, such as in a window without a picture or through a capture card. Mirror the phone over USB or WiFi to reach its notification shade.
     * @since 1 (stable)
     * @example device.quickSettings();
     */
    function quickSettings(): boolean;

    /**
     * Closes the phone's notification shade and quick settings, as the More menu of the mirror window's toolbar does; true once the phone is asked, false when it cannot be asked now.
     * @throws GS-API-090 when the logic does not run on a phone. Start the script on a phone that is mirroring.
     * @throws GS-API-091 when the phone is connected without the GameScrcpy server, such as in a window without a picture or through a capture card. Mirror the phone over USB or WiFi to reach its notification shade.
     * @since 1 (stable)
     * @example device.collapsePanels();
     */
    function collapsePanels(): boolean;

    /**
     * Presses and releases an Android key on the phone.
     * @param key The Android key to press.
     * @throws GS-API-004 when the key name is not one of AndroidKey. Use a name from the AndroidKey type, such as "back".
     * @throws GS-API-030 when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example await device.pressKey("back");
     */
    function pressKey(key: AndroidKey): Action<void>;

    /**
     * Types text into the box that has the focus on the phone.
     * @param text The text to type into the focused box on the phone.
     * @throws GS-API-011 when the text is not a string or is longer than 65536 bytes. Pass shorter text.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example device.type("gg");
     */
    function type(text: string): void;

    /**
     * Starts an app on the phone; false when the phone cannot be asked now.
     * @param packageName The app's package name, such as "com.tencent.tmgp.pubgmhd".
     * @param options Whether to stop the app first.
     * @throws GS-API-011 when the name is not a string or is empty. Pass a name.
     * @since 1 (stable)
     * @example device.launch("com.android.settings");
     */
    function launch(packageName: string, options?: LaunchOptions): boolean;

    /**
     * Turns the phone's screen off while mirroring goes on.
     * @since 1 (stable)
     * @example device.screenOff();
     */
    function screenOff(): boolean;

    /**
     * Turns the phone's screen back on.
     * @since 1 (stable)
     * @example device.screenOn();
     */
    function screenOn(): boolean;

    /**
     * The app in front on the phone, when the phone has said which.
     * @since 1 (stable)
     * @example const app = device.foregroundApp();
     */
    function foregroundApp(): ForegroundApp | null;

    /**
     * Starts a task each time another app comes to the front.
     * @param handler What to run when another app comes to the front.
     * @param options What happens when it comes again while the last task is running.
     * @throws GS-API-008 when the handler is not a function. Pass a function.
     * @throws GS-API-009 when again is not one of ignore, restart, stop, queue or parallel. Use one of the five rules.
     * @since 1 (stable)
     * @example device.onForegroundApp(({ game }) => { if (game) mapping.on(); });
     */
    function onForegroundApp(handler: (event: TaskContext & ForegroundApp) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task each time the phone's screen turns.
     * @param handler What to run when the screen turns; the event has the new size and rotation.
     * @param options What happens when it turns again while the last task is running.
     * @throws GS-API-008 when the handler is not a function. Pass a function.
     * @throws GS-API-009 when again is not one of ignore, restart, stop, queue or parallel. Use one of the five rules.
     * @since 1 (stable)
     * @example device.onRotate(({ rotation }) => { if (rotation % 2 === 1) mapping.on(); });
     */
    function onRotate(handler: (event: TaskContext & DeviceInfo) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Whether the phone is connected now; false while GameScrcpy is reconnecting to it. Taps, swipes, key presses and other actions made while it is not connected wait and happen once it is back; the logic and tasks go on, and stop only when reconnecting is given up.
     * @since 1 (stable)
     * @example if (!device.connected()) log.warn("waiting for the phone");
     */
    function connected(): boolean;

    /**
     * Starts a task each time the connection to the phone drops and reconnecting begins.
     * @param handler What to run when the connection to the phone drops and GameScrcpy starts reconnecting.
     * @param options What happens when it drops again while the last task is running.
     * @throws GS-API-008 when the handler is not a function. Pass a function.
     * @throws GS-API-009 when again is not one of ignore, restart, stop, queue or parallel. Use one of the five rules.
     * @since 1 (stable)
     * @example device.onDisconnect(() => log.warn("the phone dropped"));
     */
    function onDisconnect(handler: (event: TaskContext) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task each time the phone is connected again after it dropped.
     * @param handler What to run when the phone is connected again after it dropped.
     * @param options What happens when it comes back again while the last task is running.
     * @throws GS-API-008 when the handler is not a function. Pass a function.
     * @throws GS-API-009 when again is not one of ignore, restart, stop, queue or parallel. Use one of the five rules.
     * @since 1 (stable)
     * @example device.onReconnect(async () => { await device.home(); });
     */
    function onReconnect(handler: (event: TaskContext) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task each time an input box opens, changes or closes on the phone. It needs the GameScrcpy Companion input method on the phone; without it, and in replays, it never fires.
     * @param handler What to run when an input box opens or closes on the phone; the event says which and what kind of box.
     * @param options What happens when it changes again while the last task is running.
     * @throws GS-API-008 when the handler is not a function. Pass a function.
     * @throws GS-API-009 when again is not one of ignore, restart, stop, queue or parallel. Use one of the five rules.
     * @since 1 (stable)
     * @example device.onInputBox(({ shown, action }) => { if (shown && action === "send") log.info("chat box"); });
     */
    function onInputBox(handler: (event: TaskContext & InputBox) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Keeps the phone's screen upright or sideways, or lets it follow the phone again, like the screen direction in the mirror toolbar; false when the phone cannot be asked now.
     * @param orientation Keep the screen upright or sideways, or let it follow the phone again.
     * @throws GS-API-017 when the orientation is not "auto", "portrait" or "landscape". Write one of the three.
     * @since 1 (stable)
     * @example device.rotate("landscape");
     */
    function rotate(orientation: "auto" | "portrait" | "landscape"): boolean;
}

/** The phone's clipboard, not the computer's. */
declare namespace clipboard {
    /**
     * Reads the text on the phone's clipboard, an empty string when it holds no text; null when the phone cannot share its clipboard now, such as when it is not mirroring or mirrors an app on its own display, and in replays.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example const copied = await clipboard.read(); if (copied) log.info("copied", copied);
     */
    function read(): Promise<string | null>;

    /**
     * Puts text on the phone's clipboard; the computer's clipboard stays as it is. Resolves with true when the text was sent to the phone, false when the phone cannot share its clipboard now and in replays.
     * @param text What to put on the phone's clipboard, up to 262000 bytes as UTF-8.
     * @throws GS-API-011 when the text is not a string. Pass the text as a string.
     * @throws GS-API-017 when the text is longer than 262000 bytes as UTF-8. Pass shorter text.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example await clipboard.write("Room 4421");
     */
    function write(text: string): Promise<boolean>;
}

/** The controls of the profile in use, by id; the script's output never feeds back into its input. */
declare namespace mapping {
    /**
     * The controls of the profile in use, with their ids and whether they are on.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example const ids = mapping.controls().map((control) => control.id);
     */
    function controls(): ControlInfo[];

    /**
     * The component with this id, or null.
     * @param id The control id from the profile in use.
     * @since 1 (stable)
     * @example mapping.get("fire")?.trigger();
     */
    function get(id: string): Component | null;

    /**
     * All components of the profile, or those of one type, of one class or with one interface.
     * @param type Only components of this type, of this class or with this interface.
     * @since 1 (stable)
     * @example for (const stick of mapping.all("wasd")) stick.reset();
     */
    function all(type?: ComponentType | Interface<unknown> | { readonly prototype: Component } | { readonly component?: Component }): Component[];

    /**
     * Presses and releases a control as if its key were tapped; the script's own output never triggers scripts.
     * @param id The control id from the profile in use.
     * @throws GS-API-005 when there is no control with this id in the profile in use. Use an id from mapping.controls().
     * @throws GS-API-030 when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example await mapping.trigger("jump");
     */
    function trigger(id: string): Action<void>;

    /**
     * Holds a control down for a while, then releases it; released early if the task is cancelled.
     * @param id The control id from the profile in use.
     * @param durationMs How long to hold it down, in milliseconds.
     * @throws GS-API-005 when there is no control with this id in the profile in use. Use an id from mapping.controls().
     * @throws GS-API-003 when the duration is missing, not more than 0 or more than 60000. Pass a duration in milliseconds.
     * @throws GS-API-030 when until() was used and its condition did not come true in time on any try. Raise timeoutMs or retry, or check what the condition waits for.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example await mapping.hold("sprint", 1500);
     */
    function hold(id: string, durationMs: number): Action<void>;

    /**
     * Turns a control on or off until the script ends or the profile changes; this ends a binding of the control's enabled.
     * @param id The control id from the profile in use.
     * @param enabled Whether the control reacts to its keys.
     * @throws GS-API-005 when there is no control with this id in the profile in use. Use an id from mapping.controls().
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example mapping.setEnabled("fire", false);
     */
    function setEnabled(id: string, enabled: boolean): void;

    /**
     * Turns key mapping on for this phone.
     * @since 1 (stable)
     * @example mapping.on();
     */
    function on(): boolean;

    /**
     * Turns key mapping off for this phone, so keys go to the phone as they are.
     * @since 1 (stable)
     * @example mapping.off();
     */
    function off(): boolean;

    /**
     * Turns key mapping on or off for this phone.
     * @since 1 (stable)
     * @example mapping.toggle();
     */
    function toggle(): boolean;

    /**
     * Whether key mapping is on for this phone.
     * @since 1 (stable)
     * @example if (!mapping.isOn()) mapping.on();
     */
    function isOn(): boolean;
}

/** The key hints drawn on the phone picture. */
declare namespace hints {
    /**
     * Shows the key hints on the picture.
     * @since 1 (stable)
     * @example hints.show();
     */
    function show(): boolean;

    /**
     * Hides the key hints on the picture.
     * @since 1 (stable)
     * @example hints.hide();
     */
    function hide(): boolean;

    /**
     * Shows or hides the key hints.
     * @since 1 (stable)
     * @example hints.toggle();
     */
    function toggle(): boolean;

    /**
     * Sets how solid the key hints are.
     * @param opacity From 0 (invisible) to 1 (solid).
     * @since 1 (stable)
     * @example hints.setOpacity(0.3);
     */
    function setOpacity(opacity: number): boolean;

    /**
     * While on, every key hint of this phone is drawn at full opacity, even when the hints are hidden or faded; typing on the phone still hides them. Each logic and each task has its own switch, which turns off when that logic or task stops.
     * @param on true to draw every key hint at full opacity, false to give them back their usual look.
     * @since 1 (stable)
     * @example keys.Tab.onPress(() => { hints.reveal(true); }); keys.Tab.onRelease(() => { hints.reveal(false); });
     */
    function reveal(on: boolean): boolean;
}

/** The profile in use on this phone. */
declare namespace profile {
    /**
     * The profile this script belongs to.
     * @since 1 (stable)
     * @example log.info(profile.current()?.name);
     */
    function current(): ProfileInfo | null;

    /**
     * Switches this phone to another profile by id or name; this script stops and that profile's logic starts.
     * @param name The id or the name of a profile in the library.
     * @throws GS-API-011 when the name is not a string or is empty. Pass a name.
     * @since 1 (stable)
     * @example profile.switchTo("Driving");
     */
    function switchTo(name: string): boolean;

    /**
     * Switches this phone to the next profile in the library.
     * @since 1 (stable)
     * @example profile.next();
     */
    function next(): boolean;

    /**
     * Switches this phone to the previous profile in the library.
     * @since 1 (stable)
     * @example profile.previous();
     */
    function previous(): boolean;

    /**
     * Asks in the phone's mirror window whether to keep the changes this logic made to its components (each shown as old and new value) in the profile, and resolves with whether they were saved. Saving reloads the profile and starts the logic again, as saving in the key editor does. Resolves with false at once when nothing was changed, while the key editor is open, for a profile that is not in the library, and in a task.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example controls.fire.setRate(12); await profile.save();
     */
    function save(): Promise<boolean>;
}

/** Values this script keeps between runs (up to 1 MiB of JSON). */
declare namespace store {
    /**
     * Reads a value this script stored earlier, or undefined.
     * @param key The name of the value.
     * @throws GS-API-006 when the key is not a string or the value is not a JSON value. Store plain data only.
     * @throws GS-API-007 when the store would be larger than 1 MiB. Remove keys you no longer need.
     * @since 1 (stable)
     * @example const best = store.get("best") ?? 0;
     */
    function get(key: string): StoreValue | undefined;

    /**
     * Stores a value that stays after the script and the program restart.
     * @param key The name of the value.
     * @param value A JSON value: null, a boolean, number, string, array or plain object.
     * @throws GS-API-006 when the key is not a string or the value is not a JSON value. Store plain data only.
     * @throws GS-API-007 when the store would be larger than 1 MiB. Remove keys you no longer need.
     * @since 1 (stable)
     * @example store.set("best", 42);
     */
    function set(key: string, value: StoreValue): void;

    /**
     * Removes a stored value.
     * @param key The name of the value.
     * @throws GS-API-006 when the key is not a string or the value is not a JSON value. Store plain data only.
     * @throws GS-API-007 when the store would be larger than 1 MiB. Remove keys you no longer need.
     * @since 1 (stable)
     * @example store.remove("best");
     */
    function remove(key: string): void;

    /**
     * The names of all stored values.
     * @since 1 (stable)
     * @example for (const key of store.keys()) log.info(key);
     */
    function keys(): string[];
}

/** Messages, questions and drawings in the phone's mirror window. */
declare namespace ui {
    /**
     * Shows a short message in the phone's mirror window.
     * @param message What to show; longer than 100 characters is cut short.
     * @throws GS-API-011 when the message or question is missing or not a string. Pass the text as a string.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example ui.toast("Round 2");
     */
    function toast(message: string): void;

    /**
     * Asks a yes or no question in the phone's mirror window and resolves with the answer; cancelling the task closes the question.
     * @param question The question, up to 200 characters.
     * @param options Other words for the two buttons.
     * @throws GS-API-011 when the message or question is missing or not a string. Pass the text as a string.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example if (await ui.confirm("Sell the old gear?")) log.info("selling");
     */
    function confirm(question: string, options?: ConfirmOptions): Promise<boolean>;

    /**
     * Offers choices in the phone's mirror window and resolves with the index chosen, or null when it is closed.
     * @param question The question, up to 200 characters.
     * @param choices Two to eight choices, each up to 60 characters.
     * @throws GS-API-011 when the question is missing or not a string. Pass the question as a string.
     * @throws GS-API-012 when the choices are not two to eight strings. Pass between two and eight short strings.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example const mode = await ui.choose("Mode", ["Easy", "Hard"]);
     */
    function choose(question: string, choices: string[]): Promise<number | null>;

    /**
     * Asks for a line of text in the phone's mirror window and resolves with it, or null when it is closed.
     * @param question The question, up to 200 characters.
     * @param options The text already in the box.
     * @throws GS-API-011 when the message or question is missing or not a string. Pass the text as a string.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example const team = await ui.ask("Team name");
     */
    function ask(question: string, options?: AskOptions): Promise<string | null>;

    /**
     * Draws a box on the phone picture until it is disposed, its duration ends or the task that drew it ends.
     * @param x Left edge, as a ratio of the picture width.
     * @param y Top edge, as a ratio of the picture height.
     * @param width Width as a ratio of the picture width; the box stays inside the picture.
     * @param height Height as a ratio of the picture height.
     * @param options Color, label and how long it stays.
     * @throws GS-API-002 when a position or size is outside 0 to 1, or the box leaves the picture. Positions and sizes are picture ratios.
     * @throws GS-API-003 when the duration is not more than 0 and at most 600000. Pass a duration in milliseconds.
     * @throws GS-API-010 when 64 drawings are already shown. Dispose the drawings you no longer need.
     * @throws GS-API-011 when the text, label or options are not strings or an object. Pass strings and a plain object.
     * @throws GS-API-014 when the color is not one of DrawColor. Use accent, good, warn, bad or white.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example const found = ui.box(0.4, 0.3, 0.2, 0.1, { color: "good", label: "chest" });
     */
    function box(x: number, y: number, width: number, height: number, options?: BoxOptions): Drawing;

    /**
     * Writes text on the phone picture until it is disposed, its duration ends or the task that drew it ends.
     * @param x Left edge, as a ratio of the picture width.
     * @param y Top edge, as a ratio of the picture height.
     * @param text What to write; longer than 100 characters is cut short.
     * @param options Color and how long it stays.
     * @throws GS-API-002 when a position or size is outside 0 to 1, or the box leaves the picture. Positions and sizes are picture ratios.
     * @throws GS-API-003 when the duration is not more than 0 and at most 600000. Pass a duration in milliseconds.
     * @throws GS-API-010 when 64 drawings are already shown. Dispose the drawings you no longer need.
     * @throws GS-API-011 when the text, label or options are not strings or an object. Pass strings and a plain object.
     * @throws GS-API-014 when the color is not one of DrawColor. Use accent, good, warn, bad or white.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example ui.text(0.05, 0.05, "Round 2", { durationMs: 3000 });
     */
    function text(x: number, y: number, text: string, options?: TextOptions): Drawing;

    /**
     * Marks a point on the phone picture with a ring and a dot until it is disposed, its duration ends or the task that drew it ends; the label shows to the right of the ring.
     * @param x Where the point is, as a ratio of the picture width.
     * @param y Where the point is, as a ratio of the picture height.
     * @param options Color, label and how long it stays.
     * @throws GS-API-002 when a position or size is outside 0 to 1, or the box leaves the picture. Positions and sizes are picture ratios.
     * @throws GS-API-003 when the duration is not more than 0 and at most 600000. Pass a duration in milliseconds.
     * @throws GS-API-010 when 64 drawings are already shown. Dispose the drawings you no longer need.
     * @throws GS-API-011 when the text, label or options are not strings or an object. Pass strings and a plain object.
     * @throws GS-API-014 when the color is not one of DrawColor. Use accent, good, warn, bad or white.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example const spot = ui.mark(0.5, 0.8, { color: "warn", label: "tap here", durationMs: 2000 });
     */
    function mark(x: number, y: number, options?: MarkOptions): Drawing;

    /**
     * Shows a picture on the phone picture in a thin frame until it is disposed, its duration ends or the task that drew it ends. An image from vision.capture() that is no longer kept shows nothing.
     * @param image A template name, a picture in the profile folder or an image from vision.capture().
     * @param region Where to show it, stretched to fit; an image from vision.capture() goes where it was taken by default.
     * @param options The frame's color, a label and how long it stays.
     * @throws GS-API-015 when the image is not a template name, a png, jpg or bmp path inside the profile folder or an image from vision.capture(). Use a name made in the vision workbench, or a relative path such as "res/chest.png".
     * @throws GS-API-018 when there is no template or region with that name. Use a name from res/resources.json.
     * @throws GS-API-020 when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
     * @throws GS-API-002 when the region is missing for a template or a picture file, or is outside the picture. Regions are picture ratios.
     * @throws GS-API-003 when the duration is not more than 0 and at most 600000. Pass a duration in milliseconds.
     * @throws GS-API-010 when 64 drawings are already shown. Dispose the drawings you no longer need.
     * @throws GS-API-011 when the label or options are not a string or an object. Pass strings and a plain object.
     * @throws GS-API-014 when the color is not one of DrawColor. Use accent, good, warn, bad or white.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example const score = await vision.capture(res.region("Score")); if (score) ui.image(score, { x: 0.7, y: 0.02, width: 0.28, height: 0.08 });
     */
    function image(image: TemplateName | ImagePath | CapturedImage, region?: Region | RegionName, options?: BoxOptions): Drawing;

    /**
     * Shows a card at the top of the phone picture with the title, a bar and the percentage. Each logic and each task has one card: calling again updates it, and it goes away with ui.progress(null) or when the logic or task stops.
     * @param title What the card says, up to 80 characters; null takes the card away.
     * @param value How far along, from 0 to 1; leave it out only with null.
     * @throws GS-API-017 when the progress is not a number from 0 to 1. Pass a ratio such as done / total.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example ui.progress("Rounds", done / total);
     */
    function progress(title: string | null, value?: number): void;

    /**
     * Shows a Windows notification; clicking it opens the phone's mirror window. Each logic and each task shows at most one every 10 seconds; false means this one was not shown.
     * @param title The title, up to 64 characters.
     * @param text The line under the title, up to 256 characters.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example ui.notify("Done", "All 30 rounds finished");
     */
    function notify(title: string, text?: string): boolean;

    /**
     * Gives a panel of this extension, declared in contributes.panels: a side panel of the mirror window, a page of the main window, a section of Settings, part of the extension's settings window, the settings of a device it adds, or what its toolbar button opens. GameScrcpy draws the tree given to render() with its own controls; no interface code of the extension runs.
     * @param name The name the extension's manifest gives the panel in contributes.panels.
     * @throws GS-API-011 when the name is not a string or is empty. Pass the name from contributes.panels.
     * @since 1 (stable)
     * @example const stats = ui.panel("stats"); stats.render({ type: "text", text: "Rounds: 3" });
     */
    function panel(name: string): UiPanel;

    /**
     * Puts this extension's group of items into the right-click menu on the phone picture, in place of the last ones; ui.menu.on("select", handler) hears the item picked.
     * @param items Up to 16 items; an empty list takes the group away.
     * @throws GS-API-075 when the items are not a list of up to 16 items with an id and a title each; the message names the item that is wrong. Fix the item the message names.
     * @throws GS-API-076 when the extension declares no right-click menu for this logic, or the logic is not an extension's. Declare a panel with where "menu" whose script is this one.
     * @since 1 (stable)
     * @example ui.menu([{ id: "reset", title: "Reset the counter" }]);
     */
    function menu(items: readonly UiMenuItem[]): void;

    /**
     * Gives a status widget of this extension, declared in contributes.panels: a little text, icons and progress shown with the status of the mirror window.
     * @param name The name the extension's manifest gives the widget in contributes.panels.
     * @throws GS-API-011 when the name is not a string or is empty. Pass the name from contributes.panels.
     * @since 1 (stable)
     * @example ui.status("fps").render({ type: "text", text: "60" });
     */
    function status(name: string): UiStatus;

    /**
     * Gives a toolbar button of this extension, declared in contributes.panels with where "toolbar": a button on the mirror window's toolbar with the declared title and icon while this logic runs. A click sends click; once ui.panel() of the same name has drawn a tree, the click also opens it under the button.
     * @param name The name the extension's manifest gives the panel in contributes.panels.
     * @throws GS-API-011 when the name is not a string or is empty. Pass the name from contributes.panels.
     * @since 1 (stable)
     * @example const quick = ui.toolbar("quick"); quick.on("click", () => log.info("pressed"));
     */
    function toolbar(name: string): UiToolbar;

    /**
     * Gives a menu of this extension, declared in contributes.panels with where "contextMenu": its items are added to the More menu of the cards its on names, device cards and profile cards.
     * @param name The name the extension's manifest gives the panel in contributes.panels.
     * @throws GS-API-011 when the name is not a string or is empty. Pass the name from contributes.panels.
     * @since 1 (stable)
     * @example ui.contextMenu("more").set([{ id: "copy", title: "Copy the id" }]);
     */
    function contextMenu(name: string): UiContextMenu;

    /**
     * Gives this extension's group of items in the tray menu, declared in contributes.panels with where "tray".
     * @param name The name the extension's manifest gives the panel in contributes.panels.
     * @throws GS-API-011 when the name is not a string or is empty. Pass the name from contributes.panels.
     * @since 1 (stable)
     * @example ui.tray("quick").set([{ id: "pause", title: "Pause", checked: false }]);
     */
    function tray(name: string): UiTray;
}

/** What the user holds and points at, readable only while the phone's mirror window is in front. */
declare namespace input {
    /**
     * An input source of this logic, such as a device it reads: the keys it presses and the axes it moves reach the phone's key mapping as the input Ext.<extension id>.<name>.<key>, or Ext.mine.<name>.<key> for your own logic, which profiles bind like any key. While the key editor waits for a key, a press is taken as that key.
     * @param name The name of the input source: 1 to 32 lowercase letters, digits or -.
     * @throws GS-API-085 when the name is not 1 to 32 lowercase letters, digits or -. Use a name such as "deck" or "pedal-board".
     * @throws GS-API-080 when the logic belongs to an extension whose manifest does not ask for the permission hardware. Add "hardware" to the permissions in the extension's manifest.
     * @since 1 (stable)
     * @example const deck = input.source("deck"); deck.press("k1"); deck.release("k1");
     */
    function source(name: string): InputSource;

    /**
     * Where the pointer is over the phone picture, as ratios; null when the phone's mirror window is not in front or the pointer is outside the picture.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @throws GS-API-098 when this logic may not read your keys and cursor yet. Allow it when the phone's mirror window asks; logic made in GameScrcpy and profiles imported with confirmation may read them from the start.
     * @since 1 (stable)
     * @example const at = input.cursor();
     */
    function cursor(): PicturePoint | null;

    /**
     * Whether a key or mouse button is held; always false when the phone's mirror window is not in front.
     * @param name A key or mouse button, written as in profiles: "KeyW", "Space", "Mouse.Left".
     * @throws GS-API-013 when the name is not a string. Write the key as in profiles, such as "KeyW".
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @throws GS-API-098 when this logic may not read your keys and cursor yet. Allow it when the phone's mirror window asks; logic made in GameScrcpy and profiles imported with confirmation may read them from the start.
     * @since 1 (stable)
     * @example if (input.isDown("ShiftLeft")) log.info("walking");
     */
    function isDown(name: InputName): boolean;
}

/** Serial ports, USB HID devices and Bluetooth LE devices the user allows. Every logic, your own too, can only open the devices the user ticked; the logic of an extension also needs the permission hardware. */
declare namespace hardware {
    /**
     * Shows the user a list of the connected devices of that kind; the user ticks the ones this logic may use. Resolves with every ticked device, those allowed before included, or with an empty list when the user cancels. Bluetooth devices are looked for while the list is open. In an extension the ticked devices are allowed to the whole extension, so its own DLL can open them too, with device_open of the C header.
     * @param options The kind of device and the filters that the listed devices must match.
     * @throws GS-API-085 when the kind is not serial, hid or ble, a filter is not valid, or 8 requests are already waiting. Pass { kind } and filters as HardwareFilter describes.
     * @throws GS-API-080 when the logic belongs to an extension whose manifest does not ask for the permission hardware. Add "hardware" to the permissions in the extension's manifest.
     * @since 1 (stable)
     * @example const [pad] = await hardware.request({ kind: "hid", filters: [{ vendorId: 0x0fd9 }] });
     */
    function request(options: HardwareRequest): Promise<HardwareInfo[]>;

    /**
     * The devices this logic may open, connected or not; present says whether each one is connected now.
     * @throws GS-API-080 when the logic belongs to an extension whose manifest does not ask for the permission hardware. Add "hardware" to the permissions in the extension's manifest.
     * @since 1 (stable)
     * @example for (const device of hardware.list()) log.info(device.name, device.present);
     */
    function list(): HardwareInfo[];

    /**
     * Opens an allowed device; a device can be open only once in GameScrcpy. A device opened in a task closes when the task ends, one opened at the top level when the logic stops.
     * @param id The id of an allowed device, from hardware.request() or hardware.list().
     * @param options For a serial port: speed, data bits, parity, stop bits, flow control and the DTR and RTS lines.
     * @throws GS-API-081 when the device has not been allowed. Ask for it with hardware.request() first.
     * @throws GS-API-082 when the device is not connected or went away. Plug it in or turn it on, then open it again.
     * @throws GS-API-083 when Windows could not open, read or write the device, or it is already open. The message says why; close the program or logic that holds it.
     * @throws GS-API-085 when the serial options are not possible, or this logic already has 16 devices open. Use the values SerialOptions describes, and close the devices you no longer need.
     * @throws GS-API-080 when the logic belongs to an extension whose manifest does not ask for the permission hardware. Add "hardware" to the permissions in the extension's manifest.
     * @since 1 (stable)
     * @example const port = await hardware.open(board.id, { baudRate: 9600 }); await port.write("ping\n");
     */
    function open(id: string, options?: SerialOptions): Promise<SerialDevice | HidDevice | BleDevice>;

    /**
     * Gives up the permission to use a device, as its remove button on the Extensions page does; the device closes if it is open.
     * @param id The id of an allowed device.
     * @throws GS-API-085 when the id is not a string that is not empty. Pass the id of a device from hardware.list().
     * @throws GS-API-080 when the logic belongs to an extension whose manifest does not ask for the permission hardware. Add "hardware" to the permissions in the extension's manifest.
     * @since 1 (stable)
     * @example await hardware.forget(device.id);
     */
    function forget(id: string): Promise<void>;

    /**
     * Tells other extensions and GameScrcpy's own sessions that this logic is using the hardware, so that they leave it alone. Resolves with false when someone else holds it. Devices opened with hardware.open() are claimed while they are open. A claim made in a task ends with the task, one made at the top level when the logic stops.
     * @param resource The hardware: serial:COM9, ble:AA:BB:CC:DD:EE:FF, hid: and the device path, or usb:vid:pid:serial number in hexadecimal, such as usb:2341:0043:A1B2.
     * @param options Whether to wait for whoever holds it, and how to let it go to others.
     * @throws GS-API-085 when the resource does not name hardware, or the options are not an object. Name it as serial:, ble:, hid: or usb: and the rest, as the resource parameter describes.
     * @throws GS-API-080 when the logic belongs to an extension whose manifest does not ask for the permission hardware. Add "hardware" to the permissions in the extension's manifest.
     * @since 1 (stable)
     * @example if (await hardware.claim("serial:COM9", { onWanted: () => true })) log.info("the board is ours");
     */
    function claim(resource: string, options?: HardwareClaimOptions): Promise<boolean>;

    /**
     * Lets go of hardware claimed with hardware.claim().
     * @param resource The hardware, named as for hardware.claim().
     * @throws GS-API-085 when the resource does not name hardware, or the options are not an object. Name it as serial:, ble:, hid: or usb: and the rest, as the resource parameter describes.
     * @throws GS-API-080 when the logic belongs to an extension whose manifest does not ask for the permission hardware. Add "hardware" to the permissions in the extension's manifest.
     * @since 1 (stable)
     * @example hardware.release("serial:COM9");
     */
    function release(resource: string): void;
}

/** Web services over https and WebSocket, made by GameScrcpy for the logic, and connections from programs on this computer over HTTP on 127.0.0.1. The logic of an extension needs the permission network, which lists the hosts it may reach, and network.listen to listen. */
declare namespace net {
    /**
     * Opens an HTTP server on 127.0.0.1 that programs on this computer, such as Stream Deck or OBS, can call; nothing from other computers can reach it. Each request goes to the handler, which answers with an object, a text or nothing (204); a handler that throws answers 500. A server opened in a task closes when the task ends, one opened at the top level when the logic stops.
     * @param port The port on 127.0.0.1, from 1024 to 65535.
     * @param handler Answers one request; it runs as a task of its own for each request, several at a time.
     * @param options Web pages of other sites that may call it from a browser.
     * @throws GS-API-100 when the logic belongs to an extension whose manifest does not ask for the permission network.listen, or local connections are turned off for it in its details. Add "network.listen" to the permissions in the extension's manifest, or turn local connections on.
     * @throws GS-API-101 when the port is not from 1024 to 65535, or an origin is not written as https://host. Pass a port from 1024 to 65535 and origins such as "https://example.com".
     * @throws GS-API-102 when another program uses the port, or the extension listens on 4 ports already. Pick another port, or close a server you no longer need.
     * @since 1 (stable)
     * @example await net.listen(8765, (request) => ({ json: { path: request.path, query: request.query } }));
     */
    function listen(port: number, handler: (request: NetRequest) => NetAnswer | Promise<NetAnswer>, options?: NetListenOptions): Promise<NetServer>;

    /**
     * Asks a web service over https and resolves with its answer, whatever its status; GameScrcpy makes the request, so the logic never holds a connection of its own. The logic of an extension may only reach the hosts its manifest lists under the permission network, and redirects are followed only to such hosts; your own logic may reach any https host. The body is at most 1 MiB and the answer at most 8 MiB, and each logic may have 8 requests going at once. Cookies are not kept: set the cookie header yourself when a service needs one. Cancelling the task cancels the request.
     * @param url An https address on a host this logic may reach, up to 4096 characters.
     * @param options The method, headers and body of the request, and how long to wait for it.
     * @throws GS-API-155 when the logic belongs to an extension whose manifest does not ask for the permission network or does not list the host, or the server sends the request on to a host it does not list or to an address that is not https. List the host, such as "api.example.com" or "*.example.com", under hosts of the permission network in the extension's manifest.
     * @throws GS-API-156 when the address is not https, the method is not GET, HEAD, POST, PUT, PATCH, DELETE or OPTIONS, a header is one GameScrcpy sets itself or one HTTP does not allow, the body is not text or bytes, or timeoutMs is not from 1000 to 60000. Use an https:// address and the options as NetFetchOptions describes.
     * @throws GS-API-157 when the body is over 1 MiB, the answer over 8 MiB, or the logic has 8 requests going already. Send and ask for less, or wait for a request to finish.
     * @throws GS-API-158 when the host could not be reached, its certificate is not valid, the connection broke, the server sent the request on more than 5 times, or no answer came within timeoutMs; the message says which. Check the address and the network, or allow more time.
     * @since 1 (stable)
     * @example const reply = await net.fetch("https://api.example.com/scores", { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ score: 10 }) }); log.info(reply.status, reply.json());
     */
    function fetch(url: string, options?: NetFetchOptions): Promise<NetResponse>;

    /**
     * Opens a WebSocket connection over wss and resolves once the server has taken it; the hosts are the same as for net.fetch. Messages are text or bytes of at most 1 MiB each, and each logic may have 4 sockets open. A socket opened in a task closes when the task ends, one opened at the top level when the logic stops.
     * @param url A wss address on a host this logic may reach, up to 4096 characters.
     * @param options The subprotocols to offer the server.
     * @throws GS-API-155 when the logic belongs to an extension whose manifest does not ask for the permission network or does not list the host, or the server sends the request on to a host it does not list or to an address that is not https. List the host, such as "api.example.com" or "*.example.com", under hosts of the permission network in the extension's manifest.
     * @throws GS-API-156 when the address is not wss, or a protocol is not a name that HTTP allows. Use a wss:// address and protocols such as "chat".
     * @throws GS-API-157 when the logic has 4 sockets open already. Close a socket you no longer need.
     * @throws GS-API-158 when the host could not be reached, its certificate is not valid, or the server did not take the WebSocket connection; the message says why. Check the address, the network and the protocols.
     * @since 1 (stable)
     * @example const live = await net.socket("wss://live.example.com/feed"); live.onMessage(({ data }) => log.info(data)); live.send("hello");
     */
    function socket(url: string, options?: NetSocketOptions): Promise<NetSocket>;
}

/** Reading text aloud and listening for voice commands, with Windows or an engine that an extension provides; the logic of an extension needs the permission speech.listen to listen. */
declare namespace speech {
    /**
     * Reads a text aloud with the engine, voice, rate and volume set in Settings, Audio, unless the options pick others. Each engine reads one text at a time and the others wait their turn. Resolves with true once the text has been read, and with false when it is cut off by interrupt, speech.stop(), the task being cancelled or the logic stopping.
     * @param text What to read, 1 to 1000 characters.
     * @param options The engine, voice, rate and volume, and whether to cut in at once.
     * @throws GS-API-110 when there is no such engine, it cannot read aloud, or it has no such voice. Pick an engine and a voice from speech.engines() and speech.voices(), or leave them out.
     * @throws GS-API-111 when the text is not 1 to 1000 characters, rate is not from 0.5 to 2, volume is not from 0 to 1, or the logic has 32 texts waiting already. Pass a shorter text and values in range, or wait for the texts before it.
     * @throws GS-API-112 when the engine failed; the message says why. Try another voice or engine, or check its extension.
     * @since 1 (stable)
     * @example await speech.say("The boss comes in ten seconds", { rate: 1.2 });
     */
    function say(text: string, options?: SpeechSayOptions): Promise<boolean>;

    /**
     * Stops what this logic is reading and drops the texts it has waiting.
     * @since 1 (stable)
     * @example speech.stop();
     */
    function stop(): void;

    /**
     * The speech engines: Windows, and those that extensions provide, with what each can do.
     * @since 1 (stable)
     * @example for (const engine of speech.engines()) log.info(engine.id, engine.say, engine.listen);
     */
    function engines(): readonly SpeechEngineInfo[];

    /**
     * The voices of an engine.
     * @param options The engine whose voices to list; the one in Settings when left out.
     * @throws GS-API-110 when there is no such engine. Pick an engine from speech.engines().
     * @throws GS-API-112 when the engine could not list its voices; the message says why. Check the engine's extension.
     * @since 1 (stable)
     * @example const voices = await speech.voices(); log.info(voices.map((voice) => voice.name).join(", "));
     */
    function voices(options?: SpeechVoicesOptions): Promise<readonly SpeechVoice[]>;

    /**
     * Listens to the microphone for voice commands with the engine and language set in Settings, Audio, unless the options pick others. Each command heard with at least the confidence asked for (0.6 by default) runs the handler as a task, one after another. A listener opened in a task closes when the task ends, one opened at the top level when the logic stops; it also closes when its engine stops, and the log says why.
     * @param phrases The commands to listen for, 1 to 256 of 1 to 64 characters each; null takes any words when the engine can.
     * @param handler What to run for each command heard, one after another.
     * @param options The engine, the language and how sure the engine must be.
     * @throws GS-API-110 when there is no such engine, it cannot listen, it does not know the language, or it only listens for a list of commands. Pick an engine and a language from speech.engines(), or pass a list of commands.
     * @throws GS-API-111 when the commands are not 1 to 256 texts of 1 to 64 characters, confidence is not from 0 to 1, or the logic listens with 8 listeners already. Pass fewer or shorter commands and values in range, or close a listener.
     * @throws GS-API-113 when the logic belongs to an extension whose manifest does not ask for the permission speech.listen. Add "speech.listen" to the permissions in the extension's manifest, with a reason.
     * @throws GS-API-008 when the handler is not a function. Pass a function.
     * @since 1 (stable)
     * @example await speech.listen(["open map", "close bag"], ({ phrase }) => log.info("heard", phrase));
     */
    function listen(phrases: readonly string[] | null, handler: (heard: TaskContext & SpeechHeard) => void | Promise<void>, options?: SpeechListenOptions): Promise<SpeechListener>;
}

/** The phone's mirror window. */
declare namespace window {
    /**
     * Whether the phone's mirror window is the active window, the one that gets the keyboard; false while another window is in front or the mirror window is closed.
     * @since 1 (stable)
     * @example if (!window.focused()) log.info("the mirror window is in the background");
     */
    function focused(): boolean;

    /**
     * Starts a task each time the phone's mirror window becomes the active window.
     * @param handler What to run when the phone's mirror window becomes the active window.
     * @param options What happens when it comes again while the last task is running.
     * @throws GS-API-008 when the handler is not a function. Pass a function.
     * @throws GS-API-009 when again is not one of ignore, restart, stop, queue or parallel. Use one of the five rules.
     * @since 1 (stable)
     * @example window.onFocus(() => hints.show());
     */
    function onFocus(handler: (event: TaskContext) => void | Promise<void>, options?: ListenOptions): Subscription;

    /**
     * Starts a task each time the phone's mirror window stops being the active window, such as when another window comes to the front.
     * @param handler What to run when the phone's mirror window stops being the active window.
     * @param options What happens when it comes again while the last task is running.
     * @throws GS-API-008 when the handler is not a function. Pass a function.
     * @throws GS-API-009 when again is not one of ignore, restart, stop, queue or parallel. Use one of the five rules.
     * @since 1 (stable)
     * @example window.onBlur(() => touch.releaseAll());
     */
    function onBlur(handler: (event: TaskContext) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** Finding pictures in the phone's picture: the next frame, or the picture already shown when none comes within 0.25 s. */
declare namespace vision {
    /**
     * Looks for a picture in the next frame, or in the picture already shown when no new frame comes within 0.25 s, and resolves with the best place; null when it is not there, or when there is no picture before the timeout.
     * @param image A template name from res/resources.json, whose threshold, match mode, region and picture size are the defaults, a picture in the profile folder such as "res/chest.png" (png, jpg or bmp) cut from the mirrored picture at the same size, or an image from vision.capture(), compared in color and 0.9 alike by default like a template cut in the vision workbench; transparent pixels are ignored.
     * @param options Where to look, how alike it must be, how many and how long to wait for a picture.
     * @throws GS-API-015 when the image is not a template name, a png, jpg or bmp path inside the profile folder or an image from vision.capture(). Use a name made in the vision workbench, or a relative path such as "res/chest.png".
     * @throws GS-API-018 when there is no template or region with that name. Use a name from res/resources.json.
     * @throws GS-API-020 when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
     * @throws GS-API-016 when the picture cannot be read, is empty, fully transparent or a single colour. Save it again as a png cut from the mirrored picture.
     * @throws GS-API-037 when the image from vision.capture() is no longer kept: a script keeps its 8 latest. Keep the ones you need with save(), or capture again.
     * @throws GS-API-002 when the region is outside the picture. Regions are picture ratios.
     * @throws GS-API-003 when the timeout is not more than 0 and at most 10000. Pass milliseconds.
     * @throws GS-API-017 when the threshold is not from 0 to 1, or max is not from 1 to 64. Use a ratio and a whole number.
     * @throws GS-API-019 when match is not "color" or "grey". Write one of the two.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example const chest = await vision.find("res/chest.png"); if (chest) await touch.tap(chest.x, chest.y);
     */
    function find(image: TemplateName | ImagePath | CapturedImage, options?: FindOptions): Promise<Found | null>;

    /**
     * Looks for every place a picture appears in the next frame (or the picture already shown), best first, without overlapping matches.
     * @param image A template name from res/resources.json, whose threshold, match mode, region and picture size are the defaults, a picture in the profile folder such as "res/chest.png" (png, jpg or bmp) cut from the mirrored picture at the same size, or an image from vision.capture(), compared in color and 0.9 alike by default like a template cut in the vision workbench; transparent pixels are ignored.
     * @param options Where to look, how alike it must be, how many and how long to wait for a picture.
     * @throws GS-API-015 when the image is not a template name, a png, jpg or bmp path inside the profile folder or an image from vision.capture(). Use a name made in the vision workbench, or a relative path such as "res/chest.png".
     * @throws GS-API-018 when there is no template or region with that name. Use a name from res/resources.json.
     * @throws GS-API-020 when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
     * @throws GS-API-016 when the picture cannot be read, is empty, fully transparent or a single colour. Save it again as a png cut from the mirrored picture.
     * @throws GS-API-037 when the image from vision.capture() is no longer kept: a script keeps its 8 latest. Keep the ones you need with save(), or capture again.
     * @throws GS-API-002 when the region is outside the picture. Regions are picture ratios.
     * @throws GS-API-003 when the timeout is not more than 0 and at most 10000. Pass milliseconds.
     * @throws GS-API-017 when the threshold is not from 0 to 1, or max is not from 1 to 64. Use a ratio and a whole number.
     * @throws GS-API-019 when match is not "color" or "grey". Write one of the two.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example const coins = await vision.findAll("res/coin.png", { max: 8 });
     */
    function findAll(image: TemplateName | ImagePath | CapturedImage, options?: FindOptions): Promise<Found[]>;

    /**
     * Looks for a color in the next frame (or the picture already shown) and resolves with its largest group of pixels: a pixel matches when its red, green and blue each differ from the color by at most tolerance times 255, and matching pixels that touch, diagonally too, form one group. x and y are the center of the group's bounding box, width and height its size, and score the share of matching pixels in that box; null when no pixel matches, or when there is no picture before the timeout.
     * @param color A color written "#RRGGBB", or the name of a color in res/resources.json, whose tolerance and region are the defaults.
     * @param options Where to look, how far a pixel may be from the color and how long to wait for a picture.
     * @throws GS-API-019 when the color is not a string written "#RRGGBB" or a name. Write it as "#F5C542" or use a name.
     * @throws GS-API-018 when there is no color or region with that name. Use a name from res/resources.json.
     * @throws GS-API-020 when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
     * @throws GS-API-002 when the region is outside the picture. Regions are picture ratios.
     * @throws GS-API-003 when the timeout is not more than 0 and at most 10000. Pass milliseconds.
     * @throws GS-API-017 when the tolerance is not from 0 to 1. Use a ratio.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example const banner = await vision.findColor("#F5C542", { tolerance: 0.15 }); if (banner) await touch.tap(banner.x, banner.y);
     */
    function findColor(color: ColorName | HexColor, options?: ColorOptions): Promise<Found | null>;

    /**
     * Reads the color of one pixel in the next frame, or in the picture already shown when no new frame comes within 0.25 s, and resolves with it written "#RRGGBB"; null when there is no picture before the timeout.
     * @param at The pixel to read, as picture ratios.
     * @param options How long to wait for a picture.
     * @throws GS-API-002 when the position is not a point inside the picture. Pass a point such as point(0.5, 0.5).
     * @throws GS-API-011 when the options are not an object. Pass a plain object.
     * @throws GS-API-003 when the timeout is not more than 0 and at most 10000. Pass milliseconds.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example const sky = await vision.pickColor(point(0.5, 0.05)); log.info("sky", sky);
     */
    function pickColor(at: PicturePoint, options?: PixelOptions): Promise<HexColor | null>;

    /**
     * Checks one pixel in the next frame (or the picture already shown) against a color: true when its red, green and blue each differ from the color by at most tolerance times 255; false when they do not, or when there is no picture before the timeout.
     * @param at The pixel to compare, as picture ratios.
     * @param color A color written "#RRGGBB", or the name of a color in res/resources.json, whose tolerance is the default.
     * @param options How far the pixel may be from the color and how long to wait for a picture.
     * @throws GS-API-002 when the position is not a point inside the picture. Pass a point such as point(0.5, 0.5).
     * @throws GS-API-019 when the color is not a string written "#RRGGBB" or a name. Write it as "#F5C542" or use a name.
     * @throws GS-API-018 when there is no color with that name. Use a name from res/resources.json.
     * @throws GS-API-020 when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
     * @throws GS-API-011 when the options are not an object. Pass a plain object.
     * @throws GS-API-003 when the timeout is not more than 0 and at most 10000. Pass milliseconds.
     * @throws GS-API-017 when the tolerance is not from 0 to 1. Use a ratio.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example if (await vision.matchColor(point(0.9, 0.1), "#E53935")) await touch.tap(0.9, 0.1);
     */
    function matchColor(at: PicturePoint, color: ColorName | HexColor, options?: PixelMatchOptions): Promise<boolean>;

    /**
     * Looks for a picture again and again until it appears and resolves with where it is; null when it has not appeared within timeoutMs.
     * @param image A template name, a picture in the profile folder or an image from vision.capture(), as for find().
     * @param options Where to look and how alike it must be, as for find(), and how long and how often to look.
     * @throws GS-API-015 when the image is not a template name, a png, jpg or bmp path inside the profile folder or an image from vision.capture(). Use a name made in the vision workbench, or a relative path such as "res/chest.png".
     * @throws GS-API-018 when there is no template or region with that name. Use a name from res/resources.json.
     * @throws GS-API-020 when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
     * @throws GS-API-016 when the picture cannot be read, is empty, fully transparent or a single colour. Save it again as a png cut from the mirrored picture.
     * @throws GS-API-037 when the image from vision.capture() is no longer kept: a script keeps its 8 latest. Keep the ones you need with save(), or capture again.
     * @throws GS-API-002 when the region is outside the picture. Regions are picture ratios.
     * @throws GS-API-003 when the time is out of range. Pass milliseconds in the range given.
     * @throws GS-API-017 when the threshold is not from 0 to 1, or max is not from 1 to 64. Use a ratio and a whole number.
     * @throws GS-API-019 when match is not "color" or "grey". Write one of the two.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example const start = await vision.waitFor("res/start.png", { timeoutMs: 30000 }); if (start) await touch.tap(start);
     */
    function waitFor(image: TemplateName | ImagePath | CapturedImage, options?: WaitOptions): Promise<Found | null>;

    /**
     * Looks for a picture again and again until it is gone; true when it went away, false when it was still there after timeoutMs.
     * @param image A template name, a picture in the profile folder or an image from vision.capture(), as for find().
     * @param options Where to look and how alike it must be, as for find(), and how long and how often to look.
     * @throws GS-API-015 when the image is not a template name, a png, jpg or bmp path inside the profile folder or an image from vision.capture(). Use a name made in the vision workbench, or a relative path such as "res/chest.png".
     * @throws GS-API-018 when there is no template or region with that name. Use a name from res/resources.json.
     * @throws GS-API-020 when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
     * @throws GS-API-016 when the picture cannot be read, is empty, fully transparent or a single colour. Save it again as a png cut from the mirrored picture.
     * @throws GS-API-037 when the image from vision.capture() is no longer kept: a script keeps its 8 latest. Keep the ones you need with save(), or capture again.
     * @throws GS-API-002 when the region is outside the picture. Regions are picture ratios.
     * @throws GS-API-003 when the time is out of range. Pass milliseconds in the range given.
     * @throws GS-API-017 when the threshold is not from 0 to 1, or max is not from 1 to 64. Use a ratio and a whole number.
     * @throws GS-API-019 when match is not "color" or "grey". Write one of the two.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example await vision.waitGone("res/loading.png", { timeoutMs: 60000 });
     */
    function waitGone(image: TemplateName | ImagePath | CapturedImage, options?: WaitOptions): Promise<boolean>;

    /**
     * Looks for a picture once and taps the middle of the best place; resolves with where it was, or null without tapping when it is not there.
     * @param image A template name from res/resources.json, whose threshold, match mode, region and picture size are the defaults, a picture in the profile folder such as "res/chest.png" (png, jpg or bmp) cut from the mirrored picture at the same size, or an image from vision.capture(), compared in color and 0.9 alike by default like a template cut in the vision workbench; transparent pixels are ignored.
     * @param options Where to look, how alike it must be, how many and how long to wait for a picture.
     * @throws GS-API-015 when the image is not a template name, a png, jpg or bmp path inside the profile folder or an image from vision.capture(). Use a name made in the vision workbench, or a relative path such as "res/chest.png".
     * @throws GS-API-018 when there is no template or region with that name. Use a name from res/resources.json.
     * @throws GS-API-020 when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
     * @throws GS-API-016 when the picture cannot be read, is empty, fully transparent or a single colour. Save it again as a png cut from the mirrored picture.
     * @throws GS-API-037 when the image from vision.capture() is no longer kept: a script keeps its 8 latest. Keep the ones you need with save(), or capture again.
     * @throws GS-API-002 when the region is outside the picture. Regions are picture ratios.
     * @throws GS-API-003 when the timeout is not more than 0 and at most 10000. Pass milliseconds.
     * @throws GS-API-017 when the threshold is not from 0 to 1, or max is not from 1 to 64. Use a ratio and a whole number.
     * @throws GS-API-019 when match is not "color" or "grey". Write one of the two.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example await vision.findAndTap("res/close.png");
     */
    function findAndTap(image: TemplateName | ImagePath | CapturedImage, options?: FindOptions): Promise<Found | null>;

    /**
     * Keeps looking for a picture, starting onAppear each time it appears and onGone each time it goes away, each run as its own task; dispose() stops looking.
     * @param image A template name, a picture in the profile folder or an image from vision.capture(), as for find().
     * @param onAppear What to run when the picture appears; the event says where it is.
     * @param onGone What to run when the picture goes away.
     * @param options Where to look and how alike it must be, as for find(), how often to look and what happens when it comes again while the last task is still running.
     * @throws GS-API-008 when the handler is not a function. Pass a function.
     * @throws GS-API-009 when again is not one of ignore, restart, stop, queue or parallel. Use one of the five rules.
     * @throws GS-API-003 when the time is out of range. Pass milliseconds in the range given.
     * @throws GS-API-015 when the image is not a template name, a png, jpg or bmp path inside the profile folder or an image from vision.capture(). Use a name made in the vision workbench, or a relative path such as "res/chest.png".
     * @throws GS-API-018 when there is no template or region with that name. Use a name from res/resources.json.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example vision.watch("res/revive.png", (found) => touch.tap(found), () => log.info("gone"));
     */
    function watch(image: TemplateName | ImagePath | CapturedImage, onAppear: (event: TaskContext & Found) => void | Promise<void>, onGone?: (event: TaskContext) => void | Promise<void>, options?: WatchOptions): Subscription;

    /**
     * Takes part of the next frame, or of the picture already shown when no new frame comes within 0.25 s, as an image: look for it later with find(), show it with ui.image() or keep it with save(). Resolves with null when there is no picture before the timeout. A script keeps its 8 latest images; older ones can no longer be used.
     * @param region The part to take; the whole picture by default.
     * @param options How long to wait for a picture.
     * @throws GS-API-002 when the region is outside the picture. Regions are picture ratios.
     * @throws GS-API-018 when there is no region with that name. Use a name from res/resources.json.
     * @throws GS-API-020 when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
     * @throws GS-API-011 when the options are not an object. Pass a plain object.
     * @throws GS-API-003 when the timeout is not more than 0 and at most 10000. Pass milliseconds.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example const before = await vision.capture(res.region("Score"));
     */
    function capture(region?: Region | RegionName, options?: PixelOptions): Promise<CapturedImage | null>;

    /**
     * Looks at part of the next frame (or the picture already shown) and compares it with how it looked the last time changed() looked at the same region: true when it changed. The first look only remembers it and resolves with false, as does a look without a picture before the timeout.
     * @param region The part to look at; the whole picture by default.
     * @param options How much must change and how long to wait for a picture.
     * @throws GS-API-002 when the region is outside the picture. Regions are picture ratios.
     * @throws GS-API-018 when there is no region with that name. Use a name from res/resources.json.
     * @throws GS-API-020 when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
     * @throws GS-API-011 when the options are not an object. Pass a plain object.
     * @throws GS-API-017 when the threshold is not from 0 to 1. Use a ratio.
     * @throws GS-API-003 when the timeout is not more than 0 and at most 10000. Pass milliseconds.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example if (await vision.changed(res.region("Score"))) log.info("the score changed");
     */
    function changed(region?: Region | RegionName, options?: ChangeOptions): Promise<boolean>;

    /**
     * Keeps looking at part of the picture and starts a task each time it changed since the last look; the first look only remembers it. dispose() stops looking.
     * @param region The part to keep looking at.
     * @param handler What to run each time it changed.
     * @param options How much must change, how often to look and what happens when it changes again while the last task is running.
     * @throws GS-API-008 when the handler is not a function. Pass a function.
     * @throws GS-API-009 when again is not one of ignore, restart, stop, queue or parallel. Use one of the five rules.
     * @throws GS-API-003 when the time is out of range. Pass milliseconds in the range given.
     * @throws GS-API-002 when the region is outside the picture. Regions are picture ratios.
     * @throws GS-API-018 when there is no region with that name. Use a name from res/resources.json.
     * @throws GS-API-017 when the threshold is not from 0 to 1. Use a ratio.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example vision.onChange(res.region("Chat"), () => ui.notify("New message"));
     */
    function onChange(region: Region | RegionName, handler: (event: TaskContext) => void | Promise<void>, options?: OnChangeOptions): Subscription;

    /**
     * Reads the text in the next frame, or in the picture already shown when no new frame comes within 0.25 s, with the text recognition that comes with Windows, and resolves with each line and where it is; null when there is no picture before the timeout. Chinese and Japanese characters are joined without spaces, other words with one. Name the language the game is written in: Chinese recognition reads English words poorly and English recognition cannot read Chinese. Reading a whole picture can take a few tenths of a second, so give a region when you can.
     * @param options Where to read, in which language and how long to wait for a picture.
     * @throws GS-API-011 when the options are not an object, or the language is not a tag such as "zh-Hans". Pass a plain object and a language tag.
     * @throws GS-API-002 when the region is outside the picture. Regions are picture ratios.
     * @throws GS-API-018 when there is no region with that name. Use a name from res/resources.json.
     * @throws GS-API-020 when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
     * @throws GS-API-003 when the timeout is not more than 0 and at most 10000. Pass milliseconds.
     * @throws GS-API-039 when this computer has no text recognition for the language, or Windows could not read the picture. Add the language in Windows Settings, Time & language, Language & region, or name one the computer has.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example const gold = await vision.readText({ region: res.region("Gold") }); if (gold) log.info(gold.text);
     */
    function readText(options?: ReadTextOptions): Promise<TextResult | null>;

    /**
     * Reads the text in the next frame (or the picture already shown) and resolves with where a line says it: a line that says only that comes first, then the first line from the top that contains it, ignoring capitals and spaces. The place covers the words that say it and score is always 1; null when no line says it, or when there is no picture before the timeout.
     * @param text The text to look for, up to 200 characters; capitals and spaces do not matter.
     * @param options Where to look, in which language and how long to wait for a picture.
     * @throws GS-API-011 when the text is not a string of 1 to 200 characters that are not all spaces, the options are not an object, or the language is not a tag such as "zh-Hans". Pass the words to find, a plain object and a language tag.
     * @throws GS-API-002 when the region is outside the picture. Regions are picture ratios.
     * @throws GS-API-018 when there is no region with that name. Use a name from res/resources.json.
     * @throws GS-API-020 when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
     * @throws GS-API-003 when the timeout is not more than 0 and at most 10000. Pass milliseconds.
     * @throws GS-API-039 when this computer has no text recognition for the language, or Windows could not read the picture. Add the language in Windows Settings, Time & language, Language & region, or name one the computer has.
     * @throws GS-API-001 when no phone is connected to this script. Start the script on a phone that is mirroring.
     * @since 1 (stable)
     * @example const ok = await vision.findText("OK"); if (ok) await touch.tap(ok);
     */
    function findText(text: string, options?: ReadTextOptions): Promise<Found | null>;

    /**
     * The recognition engines: "system", the text recognition that comes with Windows, and those that extensions provide, with what each recognizes.
     * @since 1 (stable)
     * @example for (const engine of vision.engines()) log.info(engine.id, engine.kinds.join(", "));
     */
    function engines(): readonly VisionEngineInfo[];

    /**
     * Recognizes text ("ocr"), things ("detect") or what the picture shows ("classify") with an engine, in the next frame or in the picture already shown when no new frame comes within 0.25 s, and resolves with what it found, in the order the engine gives: x and y are the middle and width and height the size, as ratios of the whole picture, and confidence goes from 0 to 1. Resolves with null when there is no picture within 2 seconds. An engine works on one picture at a time and stops waiting for an extension's engine after 5 seconds. The logic of an extension needs the permission device.screen.
     * @param engine The id of an engine from vision.engines(): "system" for the text recognition that comes with Windows, or "<extension id>/<name>" for one an extension provides.
     * @param kind What to recognize: "ocr" for text, "detect" for things, "classify" for what the picture shows.
     * @param region The part of the picture to look at; the whole picture by default.
     * @param params What the engine takes besides the picture, such as { language: "en" } for "system".
     * @throws GS-API-170 when there is no engine with this id, or the extension that provides it is off or did not load. Pick an engine from vision.engines(), or turn its extension on.
     * @throws GS-API-171 when the engine does not recognize this kind, or does not know the language asked for. Pick an engine whose kinds include it, or a language it knows.
     * @throws GS-API-172 when the engine is not an id, kind is not "ocr", "detect" or "classify", params is not an object of at most 64 KiB as JSON, or the engine does not take these params. Pass the id, one of the three kinds and a plain object the engine takes.
     * @throws GS-API-173 when the engine failed or gave a result that is not a valid list; the message says why. Try again, or check the engine's extension.
     * @throws GS-API-174 when the engine took longer than 5 seconds, is still working on an earlier picture, or has 16 pictures waiting already. Pass a smaller region, or ask less often.
     * @throws GS-API-002 when the region is outside the picture. Regions are picture ratios.
     * @throws GS-API-018 when there is no region with that name. Use a name from res/resources.json.
     * @throws GS-API-072 when the logic belongs to an extension whose manifest does not ask for the permission device.screen. Add "device.screen" to the permissions in the extension's manifest, with a reason.
     * @throws GS-API-073 when the logic has no phone, such as the app script of an extension. Recognize from a session script.
     * @since 1 (stable)
     * @example const lines = await vision.recognize("system", "ocr", res.region("Chat"), { language: "en" }); for (const line of lines ?? []) log.info(line.text);
     */
    function recognize(engine: string, kind: VisionKind, region?: Region | RegionName, params?: VisionParams): Promise<readonly VisionResult[] | null>;

    /**
     * An engine by its id, with read(), detect() and classify() as short ways to call recognize(); the id is checked only when it is used.
     * @param id The id of an engine from vision.engines().
     * @throws GS-API-172 when the id is not a string of 1 to 160 characters. Pass the id of an engine from vision.engines().
     * @since 1 (stable)
     * @example const bosses = await vision.engine("alice.finder/boss").detect(); if (bosses?.length) await touch.tap(bosses[0]);
     */
    function engine(id: string): VisionEngine;
}

/** The named points, regions and paths of the profile, kept in res/resources.json by the vision workbench. */
declare namespace res {
    /**
     * A named point of the profile, as picture ratios.
     * @param name The name of a point in res/resources.json.
     * @throws GS-API-018 when the name is not a string or there is no such resource. Use a name from res/resources.json.
     * @throws GS-API-020 when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
     * @since 1 (stable)
     * @example const entry = res.point("Event entry"); await touch.tap(entry.x, entry.y);
     */
    function point(name: PointName): PicturePoint;

    /**
     * A named region of the profile, as picture ratios.
     * @param name The name of a region in res/resources.json.
     * @throws GS-API-018 when the name is not a string or there is no such resource. Use a name from res/resources.json.
     * @throws GS-API-020 when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
     * @since 1 (stable)
     * @example const corner = res.region("Bottom right"); const found = await vision.find("res/coin.png", { region: corner });
     */
    function region(name: RegionName): Region;

    /**
     * A named path of the profile: its points as picture ratios and how long a swipe along it takes.
     * @param name The name of a path in res/resources.json.
     * @throws GS-API-018 when the name is not a string or there is no such resource. Use a name from res/resources.json.
     * @throws GS-API-020 when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
     * @since 1 (stable)
     * @example const drag = res.path("Drag map"); const end = drag.points[drag.points.length - 1]; await touch.swipe(drag.points[0].x, drag.points[0].y, end.x, end.y, drag.durationMs);
     */
    function path(name: PathName): PicturePath;
}

/** Numbers and positions. */
declare namespace math {
    /**
     * Keeps a number between low and high.
     * @param value The number.
     * @param low The smallest it may be.
     * @param high The largest it may be.
     * @throws GS-API-017 when a value is not a finite number. Pass numbers.
     * @since 1 (stable)
     * @example math.clamp(1.4, 0, 1);
     */
    function clamp(value: number, low: number, high: number): number;

    /**
     * The number t of the way from from to to.
     * @param from Where it starts.
     * @param to Where it ends.
     * @param t How far along, 0 at from and 1 at to.
     * @throws GS-API-017 when a value is not a finite number. Pass numbers.
     * @since 1 (stable)
     * @example math.lerp(0, 10, 0.25);
     */
    function lerp(from: number, to: number, t: number): number;

    /**
     * A random number from low up to high.
     * @param low The smallest it may be (0 by default).
     * @param high The largest it may be (1 by default).
     * @throws GS-API-017 when a value is not a finite number. Pass numbers.
     * @since 1 (stable)
     * @example math.random(0.4, 0.6);
     */
    function random(low?: number, high?: number): number;

    /**
     * A random whole number from low to high, both included.
     * @param low The smallest whole number it may be.
     * @param high The largest whole number it may be.
     * @throws GS-API-017 when a value is not a finite number. Pass numbers.
     * @since 1 (stable)
     * @example math.randomInt(1, 6);
     */
    function randomInt(low: number, high: number): number;

    /**
     * A random position inside a region.
     * @param region Part of the picture, as ratios.
     * @throws GS-API-017 when a value is not a finite number. Pass numbers.
     * @since 1 (stable)
     * @example await touch.tap(math.randomIn(res.region("Button")));
     */
    function randomIn(region: Region): PicturePoint;

    /**
     * How far apart two positions are, as a ratio of the picture.
     * @param from The first position.
     * @param to The second position.
     * @throws GS-API-017 when a value is not a finite number. Pass numbers.
     * @since 1 (stable)
     * @example math.distance(point(0, 0), point(0.3, 0.4));
     */
    function distance(from: PicturePoint, to: PicturePoint): number;

    /**
     * The direction from one position to another in degrees: 0 is up, 90 right, 180 down and 270 left.
     * @param from The first position.
     * @param to The second position.
     * @throws GS-API-017 when a value is not a finite number. Pass numbers.
     * @since 1 (stable)
     * @example math.angle(point(0.5, 0.5), point(0.5, 0.2));
     */
    function angle(from: PicturePoint, to: PicturePoint): number;

    /**
     * The nearest whole number; a half goes up.
     * @param value The number.
     * @throws GS-API-017 when a value is not a finite number. Pass numbers.
     * @since 1 (stable)
     * @example math.round(1.6);
     */
    function round(value: number): number;

    /**
     * The largest whole number that is not more than the value.
     * @param value The number.
     * @throws GS-API-017 when a value is not a finite number. Pass numbers.
     * @since 1 (stable)
     * @example math.floor(1.6);
     */
    function floor(value: number): number;

    /**
     * The smallest whole number that is not less than the value.
     * @param value The number.
     * @throws GS-API-017 when a value is not a finite number. Pass numbers.
     * @since 1 (stable)
     * @example math.ceil(1.2);
     */
    function ceil(value: number): number;

    /**
     * The value without its sign.
     * @param value The number.
     * @throws GS-API-017 when a value is not a finite number. Pass numbers.
     * @since 1 (stable)
     * @example math.abs(-3);
     */
    function abs(value: number): number;

    /**
     * The smaller of two numbers.
     * @param a The first number.
     * @param b The second number.
     * @throws GS-API-017 when a value is not a finite number. Pass numbers.
     * @since 1 (stable)
     * @example math.min(2, 5);
     */
    function min(a: number, b: number): number;

    /**
     * The larger of two numbers.
     * @param a The first number.
     * @param b The second number.
     * @throws GS-API-017 when a value is not a finite number. Pass numbers.
     * @since 1 (stable)
     * @example math.max(2, 5);
     */
    function max(a: number, b: number): number;
}

/** Lists of values, changed in place. */
declare namespace list {
    /**
     * Adds a value to the end of the list.
     * @param items The list.
     * @param value The value.
     * @throws GS-API-011 when the list is not a list. Pass a list, such as a variable of the list kind.
     * @throws GS-API-017 when the position is not a whole number in the list. Pass a position from 0 up to the length of the list.
     * @since 1 (stable)
     * @example list.add(names, "bo");
     */
    function add(items: unknown[], value: unknown): void;

    /**
     * Puts a value into the list at a position; the rest move up.
     * @param items The list.
     * @param index The position, counted from 0.
     * @param value The value.
     * @throws GS-API-011 when the list is not a list. Pass a list, such as a variable of the list kind.
     * @throws GS-API-017 when the position is not a whole number in the list. Pass a position from 0 up to the length of the list.
     * @since 1 (stable)
     * @example list.insert(names, 0, "bo");
     */
    function insert(items: unknown[], index: number, value: unknown): void;

    /**
     * Takes the value at a position out of the list and gives it back.
     * @param items The list.
     * @param index The position, counted from 0.
     * @throws GS-API-011 when the list is not a list. Pass a list, such as a variable of the list kind.
     * @throws GS-API-017 when the position is not a whole number in the list. Pass a position from 0 up to the length of the list.
     * @since 1 (stable)
     * @example const first = list.removeAt(names, 0);
     */
    function removeAt(items: unknown[], index: number): unknown;

    /**
     * Takes the first value equal to it out of the list; says whether it found one.
     * @param items The list.
     * @param value The value.
     * @throws GS-API-011 when the list is not a list. Pass a list, such as a variable of the list kind.
     * @throws GS-API-017 when the position is not a whole number in the list. Pass a position from 0 up to the length of the list.
     * @since 1 (stable)
     * @example list.remove(names, "bo");
     */
    function remove(items: unknown[], value: unknown): boolean;

    /**
     * The position of the first value equal to it, or -1 when it is not there.
     * @param items The list.
     * @param value The value.
     * @throws GS-API-011 when the list is not a list. Pass a list, such as a variable of the list kind.
     * @throws GS-API-017 when the position is not a whole number in the list. Pass a position from 0 up to the length of the list.
     * @since 1 (stable)
     * @example if (list.indexOf(names, "bo") < 0) await ui.toast("missing");
     */
    function indexOf(items: unknown[], value: unknown): number;

    /**
     * The value at a position, or undefined when the list is shorter.
     * @param items The list.
     * @param index The position, counted from 0.
     * @throws GS-API-011 when the list is not a list. Pass a list, such as a variable of the list kind.
     * @throws GS-API-017 when the position is not a whole number in the list. Pass a position from 0 up to the length of the list.
     * @since 1 (stable)
     * @example const first = list.at(names, 0);
     */
    function at(items: unknown[], index: number): unknown;

    /**
     * Replaces the value at a position.
     * @param items The list.
     * @param index The position, counted from 0.
     * @param value The value.
     * @throws GS-API-011 when the list is not a list. Pass a list, such as a variable of the list kind.
     * @throws GS-API-017 when the position is not a whole number in the list. Pass a position from 0 up to the length of the list.
     * @since 1 (stable)
     * @example list.setAt(names, 0, "bo");
     */
    function setAt(items: unknown[], index: number, value: unknown): void;

    /**
     * How many values the list has.
     * @param items The list.
     * @throws GS-API-011 when the list is not a list. Pass a list, such as a variable of the list kind.
     * @throws GS-API-017 when the position is not a whole number in the list. Pass a position from 0 up to the length of the list.
     * @since 1 (stable)
     * @example if (list.size(names) === 0) await ui.toast("empty");
     */
    function size(items: unknown[]): number;

    /**
     * Whether the list has a value equal to it.
     * @param items The list.
     * @param value The value.
     * @throws GS-API-011 when the list is not a list. Pass a list, such as a variable of the list kind.
     * @throws GS-API-017 when the position is not a whole number in the list. Pass a position from 0 up to the length of the list.
     * @since 1 (stable)
     * @example if (list.has(names, "bo")) await sleep(10);
     */
    function has(items: unknown[], value: unknown): boolean;

    /**
     * Sorts the list in place: numbers by size, other values as text.
     * @param items The list.
     * @throws GS-API-011 when the list is not a list. Pass a list, such as a variable of the list kind.
     * @throws GS-API-017 when the position is not a whole number in the list. Pass a position from 0 up to the length of the list.
     * @since 1 (stable)
     * @example list.sort(names);
     */
    function sort(items: unknown[]): void;

    /**
     * Mixes the list into a random order, in place.
     * @param items The list.
     * @throws GS-API-011 when the list is not a list. Pass a list, such as a variable of the list kind.
     * @throws GS-API-017 when the position is not a whole number in the list. Pass a position from 0 up to the length of the list.
     * @since 1 (stable)
     * @example list.shuffle(names);
     */
    function shuffle(items: unknown[]): void;

    /**
     * Takes every value out of the list.
     * @param items The list.
     * @throws GS-API-011 when the list is not a list. Pass a list, such as a variable of the list kind.
     * @throws GS-API-017 when the position is not a whole number in the list. Pass a position from 0 up to the length of the list.
     * @since 1 (stable)
     * @example list.clear(names);
     */
    function clear(items: unknown[]): void;
}

/** Dictionaries from keys to values, as JavaScript's Map. */
declare namespace dict {
    /**
     * A new, empty dictionary.
     * @throws GS-API-011 when the dictionary is not a dictionary. Pass a dictionary from dict.make or a variable of the dictionary kind.
     * @since 1 (stable)
     * @example const scores = dict.make();
     */
    function make(): Map<unknown, unknown>;

    /**
     * Stores a value under a key, replacing what was there.
     * @param map The dictionary.
     * @param key The key.
     * @param value The value.
     * @throws GS-API-011 when the dictionary is not a dictionary. Pass a dictionary from dict.make or a variable of the dictionary kind.
     * @since 1 (stable)
     * @example dict.set(scores, "bo", 3);
     */
    function set(map: Map<unknown, unknown>, key: unknown, value: unknown): void;

    /**
     * The value stored under a key, or undefined.
     * @param map The dictionary.
     * @param key The key.
     * @throws GS-API-011 when the dictionary is not a dictionary. Pass a dictionary from dict.make or a variable of the dictionary kind.
     * @since 1 (stable)
     * @example await ui.toast(String(dict.get(scores, "bo")));
     */
    function get(map: Map<unknown, unknown>, key: unknown): unknown;

    /**
     * Whether the dictionary has a value under that key.
     * @param map The dictionary.
     * @param key The key.
     * @throws GS-API-011 when the dictionary is not a dictionary. Pass a dictionary from dict.make or a variable of the dictionary kind.
     * @since 1 (stable)
     * @example if (dict.has(scores, "bo")) await sleep(10);
     */
    function has(map: Map<unknown, unknown>, key: unknown): boolean;

    /**
     * Removes the value under a key; says whether there was one.
     * @param map The dictionary.
     * @param key The key.
     * @throws GS-API-011 when the dictionary is not a dictionary. Pass a dictionary from dict.make or a variable of the dictionary kind.
     * @since 1 (stable)
     * @example dict.remove(scores, "bo");
     */
    function remove(map: Map<unknown, unknown>, key: unknown): boolean;

    /**
     * All the keys of the dictionary, as a list.
     * @param map The dictionary.
     * @throws GS-API-011 when the dictionary is not a dictionary. Pass a dictionary from dict.make or a variable of the dictionary kind.
     * @since 1 (stable)
     * @example for (const key of dict.keys(scores)) log.info(key);
     */
    function keys(map: Map<unknown, unknown>): unknown[];

    /**
     * All the values of the dictionary, as a list.
     * @param map The dictionary.
     * @throws GS-API-011 when the dictionary is not a dictionary. Pass a dictionary from dict.make or a variable of the dictionary kind.
     * @since 1 (stable)
     * @example for (const score of dict.values(scores)) log.info(score);
     */
    function values(map: Map<unknown, unknown>): unknown[];

    /**
     * How many keys the dictionary has.
     * @param map The dictionary.
     * @throws GS-API-011 when the dictionary is not a dictionary. Pass a dictionary from dict.make or a variable of the dictionary kind.
     * @since 1 (stable)
     * @example if (dict.size(scores) > 0) await sleep(10);
     */
    function size(map: Map<unknown, unknown>): number;

    /**
     * Takes every key out of the dictionary.
     * @param map The dictionary.
     * @throws GS-API-011 when the dictionary is not a dictionary. Pass a dictionary from dict.make or a variable of the dictionary kind.
     * @since 1 (stable)
     * @example dict.clear(scores);
     */
    function clear(map: Map<unknown, unknown>): void;
}

/** Sets of values that each appear once, as JavaScript's Set. */
declare namespace set {
    /**
     * A new, empty set.
     * @throws GS-API-011 when the set is not a set. Pass a set from set.make or a variable of the set kind.
     * @since 1 (stable)
     * @example const seen = set.make();
     */
    function make(): Set<unknown>;

    /**
     * Adds a value to the set; a value that is already there stays once.
     * @param items The set.
     * @param value The value.
     * @throws GS-API-011 when the set is not a set. Pass a set from set.make or a variable of the set kind.
     * @since 1 (stable)
     * @example set.add(seen, "bo");
     */
    function add(items: Set<unknown>, value: unknown): void;

    /**
     * Whether the set has that value.
     * @param items The set.
     * @param value The value.
     * @throws GS-API-011 when the set is not a set. Pass a set from set.make or a variable of the set kind.
     * @since 1 (stable)
     * @example if (set.has(seen, "bo")) await sleep(10);
     */
    function has(items: Set<unknown>, value: unknown): boolean;

    /**
     * Takes a value out of the set; says whether it was there.
     * @param items The set.
     * @param value The value.
     * @throws GS-API-011 when the set is not a set. Pass a set from set.make or a variable of the set kind.
     * @since 1 (stable)
     * @example set.remove(seen, "bo");
     */
    function remove(items: Set<unknown>, value: unknown): boolean;

    /**
     * All the values of the set, as a list.
     * @param items The set.
     * @throws GS-API-011 when the set is not a set. Pass a set from set.make or a variable of the set kind.
     * @since 1 (stable)
     * @example for (const name of set.values(seen)) log.info(name);
     */
    function values(items: Set<unknown>): unknown[];

    /**
     * How many values the set has.
     * @param items The set.
     * @throws GS-API-011 when the set is not a set. Pass a set from set.make or a variable of the set kind.
     * @since 1 (stable)
     * @example if (set.size(seen) > 0) await sleep(10);
     */
    function size(items: Set<unknown>): number;

    /**
     * Takes every value out of the set.
     * @param items The set.
     * @throws GS-API-011 when the set is not a set. Pass a set from set.make or a variable of the set kind.
     * @since 1 (stable)
     * @example set.clear(seen);
     */
    function clear(items: Set<unknown>): void;
}

/** Text: joining, filling in, finding, cutting and changing case. */
declare namespace text {
    /**
     * Joins values into one text, with a separator between them.
     * @param parts The values to join, in order.
     * @param separator What goes between them (nothing by default).
     * @throws GS-API-011 when one of the arguments is not text. Pass text.
     * @since 1 (stable)
     * @example const line = text.join(names, ", ");
     */
    function join(parts: unknown[], separator?: string): string;

    /**
     * Fills {0}, {1} and so on in a template with the values.
     * @param template The text, with {0}, {1} and so on where the values go.
     * @param values The values, in the order of the numbers.
     * @throws GS-API-011 when one of the arguments is not text. Pass text.
     * @since 1 (stable)
     * @example await ui.toast(text.format("Round {0} of {1}", round, total));
     */
    function format(template: string, ...values: unknown[]): string;

    /**
     * Where the part starts, or -1 when it is not there.
     * @param text The text.
     * @param part What to look for.
     * @throws GS-API-011 when one of the arguments is not text. Pass text.
     * @since 1 (stable)
     * @example if (text.find(line, "Round") === 0) await sleep(10);
     */
    function find(text: string, part: string): number;

    /**
     * Whether the text contains the part.
     * @param text The text.
     * @param part What to look for.
     * @throws GS-API-011 when one of the arguments is not text. Pass text.
     * @since 1 (stable)
     * @example if (text.has(line, "Round")) await sleep(10);
     */
    function has(text: string, part: string): boolean;

    /**
     * Cuts the text wherever the separator is and gives the pieces as a list.
     * @param text The text to cut.
     * @param separator Where to cut it.
     * @throws GS-API-011 when one of the arguments is not text. Pass text.
     * @since 1 (stable)
     * @example const fields = text.split(line, ",");
     */
    function split(text: string, separator: string): string[];

    /**
     * Gives the text with the first part replaced by the other text.
     * @param text The text.
     * @param part What to look for; only the first one is replaced.
     * @param instead What to put there.
     * @throws GS-API-011 when one of the arguments is not text. Pass text.
     * @since 1 (stable)
     * @example const fixed = text.replace(line, "bo", "Bo");
     */
    function replace(text: string, part: string, instead: string): string;

    /**
     * The same text in upper case.
     * @param text The text.
     * @throws GS-API-011 when one of the arguments is not text. Pass text.
     * @since 1 (stable)
     * @example text.upper(name);
     */
    function upper(text: string): string;

    /**
     * The same text in lower case.
     * @param text The text.
     * @throws GS-API-011 when one of the arguments is not text. Pass text.
     * @since 1 (stable)
     * @example text.lower(name);
     */
    function lower(text: string): string;

    /**
     * The same text without spaces at either end.
     * @param text The text.
     * @throws GS-API-011 when one of the arguments is not text. Pass text.
     * @since 1 (stable)
     * @example text.trim(name);
     */
    function trim(text: string): string;

    /**
     * How many characters the text has.
     * @param text The text.
     * @throws GS-API-011 when one of the arguments is not text. Pass text.
     * @since 1 (stable)
     * @example if (text.length(name) > 8) await sleep(10);
     */
    function length(text: string): number;
}

/** Settings a task declares in static params; the Tasks page shows them and passes the values to the task. */
declare namespace param {
    /**
     * A number setting of a task, shown as a number box on the Tasks page.
     * @param value The value it starts with.
     * @param options Its label, and the smallest and largest value.
     * @throws GS-API-034 when the value is not of the parameter's kind. Pass a number, a string or true or false.
     * @since 1 (stable)
     * @example static params = { rounds: param.number(10, { min: 1 }) };
     */
    function number(value: number, options?: ParamOptions): TaskParam;

    /**
     * A text setting of a task, shown as a text box on the Tasks page.
     * @param value The text it starts with.
     * @param options Its label.
     * @throws GS-API-034 when the value is not of the parameter's kind. Pass a number, a string or true or false.
     * @since 1 (stable)
     * @example static params = { map: param.text("Erangel") };
     */
    function text(value: string, options?: ParamOptions): TaskParam;

    /**
     * An on or off setting of a task, shown as a switch on the Tasks page.
     * @param value Whether it starts on.
     * @param options Its label.
     * @throws GS-API-034 when the value is not of the parameter's kind. Pass a number, a string or true or false.
     * @since 1 (stable)
     * @example static params = { claim: param.toggle(true) };
     */
    function toggle(value: boolean, options?: ParamOptions): TaskParam;
}

/** Properties a component made in logic declares in static props. */
declare namespace prop {
    /**
     * A key property: the component gets it as a Key object in props.
     * @param key The key it starts with, such as "F" or "MouseLeft".
     * @param options Its label.
     * @throws GS-API-034 when the value is not of the parameter's kind. Pass a number, a string or true or false.
     * @since 1 (stable)
     * @example static props = { key: prop.key("F") };
     */
    function key(key?: string, options?: PropOptions): ComponentProp;

    /**
     * A position property: the component gets it as a point in props.
     * @param at The position it starts with.
     * @param options Its label.
     * @throws GS-API-034 when the value is not of the parameter's kind. Pass a number, a string or true or false.
     * @since 1 (stable)
     * @example static props = { at: prop.position(point(0.5, 0.8)) };
     */
    function position(at?: PicturePoint, options?: PropOptions): ComponentProp;

    /**
     * A number property.
     * @param value The value it starts with.
     * @param options Its label, and the smallest and largest value.
     * @throws GS-API-034 when the value is not of the parameter's kind. Pass a number, a string or true or false.
     * @since 1 (stable)
     * @example static props = { count: prop.number(3) };
     */
    function number(value: number, options?: NumberPropOptions): ComponentProp;

    /**
     * A text property.
     * @param value The text it starts with.
     * @param options Its label.
     * @throws GS-API-034 when the value is not of the parameter's kind. Pass a number, a string or true or false.
     * @since 1 (stable)
     * @example static props = { label: prop.text("Go") };
     */
    function text(value: string, options?: PropOptions): ComponentProp;

    /**
     * An on or off property.
     * @param value Whether it starts on.
     * @param options Its label.
     * @throws GS-API-034 when the value is not of the parameter's kind. Pass a number, a string or true or false.
     * @since 1 (stable)
     * @example static props = { loop: prop.toggle(false) };
     */
    function toggle(value: boolean, options?: PropOptions): ComponentProp;

    /**
     * A property that names another component; the component gets that component object in props, or null when the named one is not of the class or interface given.
     * @param of The id of the component it starts with; or a component class, whose type is then the only one the editor lists, or an interface.
     * @param options Its label.
     * @throws GS-API-034 when the value is not of the parameter's kind. Pass a number, a string or true or false.
     * @since 1 (stable)
     * @example static props = { target: prop.component("fire"), scope: prop.component(Hold) };
     */
    function component(of?: string | Interface<unknown> | { readonly prototype: Component } | { readonly component?: Component }, options?: PropOptions): ComponentProp;
}

/** Tools that the app script of an extension offers to AI clients over the local MCP connection. */
declare namespace mcp {
    /**
     * Offers a function of this extension to AI clients as a tool of the local MCP connection. The user confirms each call, as for running a script on a phone. The tool is gone when the subscription is disposed or the logic stops.
     * @param name The tool's name: a lowercase letter, then up to 31 lowercase letters, digits and underscores. AI clients see it as ext_<extension id>_<name>, with the dots and hyphens of the id written as underscores.
     * @param spec What the tool does, for the AI, and the JSON Schema of its input.
     * @param handler Runs as its own task for each call and gives back the tool's result as a JSON value; an error it throws is passed to the client as the tool's error. A call that is not answered within 60 seconds fails.
     * @throws GS-API-074 when the logic is not the app script of an extension, the name or the description is not valid, the input schema is not an object of type object or is larger than 16 KiB, or the extension already offers 32 tools. Call mcp.tool in a script that manifest.json lists with "run": "app", with a valid name, a description and an input schema of type object.
     * @since 1 (stable)
     * @example mcp.tool("greet", { description: "Says hello to someone", input: { type: "object", properties: { name: { type: "string" } } } }, (input) => "Hello " + input.name);
     */
    function tool(name: string, spec: McpToolSpec, handler: (input: any, event: TaskContext) => unknown): Subscription;
}

/** Events of GameScrcpy itself, such as a phone connecting, and events that the native DLL of an extension sends to the extension's own logic and panels and to the logic of the extensions that depend on it. */
declare namespace events {
    /**
     * Starts a task each time an event of the topic comes. GameScrcpy's own topics and their values: device.connected and device.disconnected { phone, name }, session.started { phone, name }, session.ended { phone, name, failed }, profile.applied { phone, name, profile, profileName }, app.foreground { phone, name, package, game }, orientation.changed { phone, name, rotation } in quarter turns, recording.started { phone, name, path }, recording.ended { phone, name, path, durationMs }, window.focused { phone, name, window: "mirror" or "grid", focused }, extension.enabled and extension.disabled { id, name, version }, and settings.changed { keys, values, extension, device }; phone is the phone's serial and name the name it shows, and values holds the new language and look settings among keys. Your own logic hears all of them. The logic of an extension hears extension.enabled, extension.disabled and settings.changed, which then tells only of its own settings and the language and look settings, and a session script also hears its own phone; the other phones, and app and task scripts, need the permission events.app. Any other topic is one the native DLL of this extension sends, which only the extension's own logic and panels and the logic of the extensions that depend on it hear; written "<extension id>:<topic>", it is one the DLL of an extension that this one lists in its dependencies sends. Of each topic a listener gets the first 100 events in a second at once and the rest of that second merged into the last one.
     * @param topic One of GameScrcpy's own topics, such as "device.connected", or a topic the extension's DLL sends: 1 to 64 lowercase letters, digits, dots and dashes; for an extension this one depends on, its id, a colon and the topic, as in "alice.boards:board.added".
     * @param handler What to run for each event; value is what came with it.
     * @param options What happens when another event comes while the last task is running.
     * @throws GS-API-068 when the topic is not 1 to 64 lowercase letters, digits, dots and dashes, after an extension id and a colon or alone. Use one of GameScrcpy's own topics or the topic that the extension's DLL sends.
     * @throws GS-API-008 when the handler is not a function. Pass a function.
     * @throws GS-API-009 when again is not one of ignore, restart, stop, queue or parallel. Use one of the five rules.
     * @since 1 (stable)
     * @example events.on("app.foreground", ({ value }) => log.info("another app is in front", value));
     */
    function on(topic: string, handler: (event: TaskContext & { readonly topic: string; readonly value: StoreValue }) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** Questions from logic to the AI service set up in Settings, AI; they work only while Let logic call AI is on there. */
declare namespace ai {
    /**
     * Asks the AI service set up in Settings, AI, the one the assistant in the editor uses, and resolves with the text of its answer; null when it answers without text, when a picture is asked for and the phone has shown none yet, and in replays, which ask nothing. With picture, the phone's picture or a part of it goes along, made smaller so its long side is at most 1024 pixels. It works only while Let logic call AI is on there: each question spends your quota, and each logic may ask at most 10 times a minute. Cancelling the task stops waiting.
     * @param question What to ask, up to 4000 characters.
     * @param options A picture of the phone to send along, and how long to wait for the answer.
     * @throws GS-API-011 when the question is not a string of 1 to 4000 characters that are not all spaces, the options are not an object, or picture is not true, false, a region or a region name. Pass the question as text and the options as a plain object.
     * @throws GS-API-003 when timeoutMs is not from 1000 to 120000. Pass milliseconds in that range.
     * @throws GS-API-002 when the region is outside the picture. Regions are picture ratios.
     * @throws GS-API-018 when there is no region with that name. Use a name from res/resources.json.
     * @throws GS-API-020 when res/resources.json cannot be read or is not valid. Open the vision workbench, or fix the file.
     * @throws GS-API-095 when Let logic call AI is off in Settings, AI, or no AI service is set up there. Set up the service there, test the connection and turn the setting on.
     * @throws GS-API-096 when this logic called AI 10 times in the last minute. Ask less often, or wait a little.
     * @throws GS-API-097 when the AI service failed or gave no answer within timeoutMs; the message says why. Check the service in Settings, AI, or try again later.
     * @throws GS-API-072 when the logic belongs to an extension whose manifest does not ask for the permission ai. Add "ai" to the permissions in the extension's manifest, with a reason.
     * @throws GS-API-073 when a picture is asked for by logic that has no phone, such as the app script of an extension. Ask without a picture, or from a session script.
     * @throws GS-API-150 when the model service in Settings, AI, is the AI provider of an extension that is not installed, is turned off or may not provide AI. Turn that extension on again, or choose another model service there.
     * @since 1 (stable)
     * @example const answer = await ai.ask("Is a boss on the screen? Answer yes or no.", { picture: true }); if (answer?.toLowerCase().startsWith("yes")) ui.notify("A boss is here");
     */
    function ask(question: string, options?: AiAskOptions): Promise<string | null>;
}

/** The phone's picture as frames, for analysis and AI; the logic of an extension needs the permission media.read. */
declare namespace media {
    /**
     * Hands the phone's picture to a handler: the frame shown when it starts, then new ones, at most fps a second (2 by default), made smaller so that the long side is at most size pixels (640 by default), and cut to region when one is given. The image is a CapturedImage as vision.capture() gives; it stays usable while its handler runs and until two newer frames came. Frames taken in a task stop when the task ends, ones taken at the top level when the logic stops.
     * @param handler Runs as a task for each frame with its picture and the time the phone made it, in milliseconds of the phone's own clock; a frame that comes while it still runs is left out.
     * @param options How often, how large and which part of the picture.
     * @throws GS-API-008 when the handler is not a function. Pass a function.
     * @throws GS-API-120 when fps is not from 0.1 to 10, size is not a whole number from 64 to 1920, or the region is not a part of the picture. Pass fps from 0.1 to 10, size from 64 to 1920 and a region in ratios from 0 to 1.
     * @throws GS-API-121 when the phone shows no picture, such as a touch box without a capture card. Take frames on a phone whose picture is shown.
     * @throws GS-API-122 when this logic takes frames with 4 subscriptions already. Dispose a subscription it no longer needs.
     * @throws GS-API-072 when the logic belongs to an extension whose manifest does not ask for the permission media.read. Add "media.read" to the permissions in the extension's manifest, with a reason.
     * @throws GS-API-073 when the logic has no phone, such as the app script of an extension. Take frames in a session script.
     * @since 1 (stable)
     * @example media.frames(({ image, time }) => log.info(time, image.width, image.height), { fps: 1, size: 320 });
     */
    function frames(handler: (frame: TaskContext & { readonly image: CapturedImage; readonly time: number }) => void | Promise<void>, options?: MediaFrameOptions): Subscription;
}

/** Files the user picks and folders the user chooses for the logic. The logic of an extension needs the permission files.pick to pick files, and files.paths, which names its folders, to open folders. */
declare namespace files {
    /**
     * Shows the Windows dialog to open a file, or with save to save one, and resolves with a handle to the file the user picked, or with null when the user cancels. The handle reads and writes only that file, up to 64 MiB, and lasts until the logic stops. The logic of an extension needs the permission files.pick.
     * @param options Whether to save, which kinds of file to offer and the name to start with.
     * @throws GS-API-160 when the logic belongs to an extension whose manifest does not ask for the permission files.pick. Add "files.pick" to the permissions in the extension's manifest, with a reason.
     * @throws GS-API-161 when the filters or the suggested name are not right. Pass filters such as { name: "Images", extensions: ["png", "jpg"] } and a file name without a folder.
     * @throws GS-API-163 when the logic has 64 files and folders open, or 8 requests for files and folders are waiting. Close the handles you no longer need, or wait for the user to answer.
     * @since 1 (stable)
     * @example const file = await files.pick({ filters: [{ name: "Profiles", extensions: ["json"] }] }); if (file) log.info(file.name, (await file.text()).length);
     */
    function pick(options?: FilePickOptions): Promise<FileHandle | null>;

    /**
     * Opens a folder chosen for this logic. The first time, GameScrcpy asks the user to choose the real folder and remembers it; the user can forget it in the extension's settings, or for your own logic in Settings, General. The handle lists, reads, writes and removes what is inside that folder and nothing else, and lasts until the logic stops.
     * @param name A folder that the extension's manifest names under the permission files.paths, such as "exports"; your own logic may use any name of 1 to 32 lowercase letters, digits and hyphens.
     * @throws GS-API-160 when the logic belongs to an extension whose manifest does not name the folder under the permission files.paths. Name the folder in folders of the permission files.paths, such as { "name": "exports", "purpose": "Where it saves your recordings." }.
     * @throws GS-API-161 when the name is not 1 to 32 lowercase letters, digits and hyphens. Use the name as the manifest declares it.
     * @throws GS-API-163 when the logic has 64 files and folders open, or 8 requests for files and folders are waiting. Close the handles you no longer need, or wait for the user to answer.
     * @throws GS-API-164 when the user closed the dialog without choosing a folder. Ask again when the user wants it, and say in the purpose why the extension needs the folder.
     * @since 1 (stable)
     * @example const exports = await files.folder("exports"); await exports.write("scores/today.csv", "time,score\n");
     */
    function folder(name: string): Promise<FolderHandle>;
}

/** The events of the group of items that ui.menu() puts into the right-click menu. */
declare namespace ui.menu {
    /**
     * Starts a task each time an item of this extension's group in the right-click menu is picked.
     * @param type The event: select, when an item of the group is picked.
     * @param handler What to run; event.id is the item's id, and event.value its new checked state or null.
     * @param options What happens when it comes again while the last task is running.
     * @throws GS-API-017 when the event is not select. Listen to "select".
     * @throws GS-API-008 when the handler is not a function. Pass a function.
     * @throws GS-API-009 when again is not one of ignore, restart, stop, queue or parallel. Use one of the five rules.
     * @since 1 (stable)
     * @example ui.menu.on("select", ({ id }) => log.info("picked", id));
     */
    function on(type: "select", handler: (event: UiEvent) => void | Promise<void>, options?: ListenOptions): Subscription;
}

/** The script API as a module: import { touch, log } from "gamescrcpy" in a logic, task or component file gives the same functions and values as the globals. */
declare module "gamescrcpy" {
    export import sleep = globalThis.sleep;
    export import nextFrame = globalThis.nextFrame;
    export import every = globalThis.every;
    export import after = globalThis.after;
    export import waitUntil = globalThis.waitUntil;
    export import retry = globalThis.retry;
    export import withTimeout = globalThis.withTimeout;
    export import parallel = globalThis.parallel;
    export import sequence = globalThis.sequence;
    export import multiGate = globalThis.multiGate;
    export import race = globalThis.race;
    export import doOnce = globalThis.doOnce;
    export import resetOnce = globalThis.resetOnce;
    export import doN = globalThis.doN;
    export import resetDoN = globalThis.resetDoN;
    export import gate = globalThis.gate;
    export import flipFlop = globalThis.flipFlop;
    export import throttle = globalThis.throttle;
    export import debounce = globalThis.debounce;
    export import stopwatch = globalThis.stopwatch;
    export import point = globalThis.point;
    export import Component = globalThis.Component;
    export import Tap = globalThis.Tap;
    export import Hold = globalThis.Hold;
    export import LongPress = globalThis.LongPress;
    export import Turbo = globalThis.Turbo;
    export import Swipe = globalThis.Swipe;
    export import Pinch = globalThis.Pinch;
    export import Wasd = globalThis.Wasd;
    export import RightWalk = globalThis.RightWalk;
    export import PadStick = globalThis.PadStick;
    export import Camera = globalThis.Camera;
    export import PadCamera = globalThis.PadCamera;
    export import FreeLook = globalThis.FreeLook;
    export import Skill = globalThis.Skill;
    export import Dial = globalThis.Dial;
    export import MultiPress = globalThis.MultiPress;
    export import TempCursor = globalThis.TempCursor;
    export import ActionWheel = globalThis.ActionWheel;
    export import defineInterface = globalThis.defineInterface;
    export import Task = globalThis.Task;
    export import controls = globalThis.controls;
    export import vars = globalThis.vars;
    export import keys = globalThis.keys;
    export import globals = globalThis.globals;
    export import machine = globalThis.machine;
    export import ext = globalThis.ext;
    export import log = globalThis.log;
    export import time = globalThis.time;
    export import touch = globalThis.touch;
    export import device = globalThis.device;
    export import clipboard = globalThis.clipboard;
    export import mapping = globalThis.mapping;
    export import hints = globalThis.hints;
    export import profile = globalThis.profile;
    export import store = globalThis.store;
    export import ui = globalThis.ui;
    export import input = globalThis.input;
    export import hardware = globalThis.hardware;
    export import net = globalThis.net;
    export import speech = globalThis.speech;
    export import window = globalThis.window;
    export import vision = globalThis.vision;
    export import res = globalThis.res;
    export import math = globalThis.math;
    export import list = globalThis.list;
    export import dict = globalThis.dict;
    export import set = globalThis.set;
    export import text = globalThis.text;
    export import param = globalThis.param;
    export import prop = globalThis.prop;
    export import mcp = globalThis.mcp;
    export import events = globalThis.events;
    export import ai = globalThis.ai;
    export import media = globalThis.media;
    export import files = globalThis.files;
}
