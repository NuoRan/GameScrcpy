# hardware

Serial ports, USB HID devices and Bluetooth LE devices the user allows. Every logic, your own too, can only open the devices the user ticked; the logic of an extension also needs the permission hardware.

## hardware.request(options)

Shows the user a list of the connected devices of that kind; the user ticks the ones this logic may use. Resolves with every ticked device, those allowed before included, or with an empty list when the user cancels. Bluetooth devices are looked for while the list is open. In an extension the ticked devices are allowed to the whole extension, so its own DLL can open them too, with device_open of the C header.

```ts
hardware.request(options: HardwareRequest): Promise<HardwareInfo[]>
```

| Parameter | Type | Description |
|---|---|---|
| `options` | `HardwareRequest` | The kind of device and the filters that the listed devices must match. |

Returns `Promise<HardwareInfo[]>`. stable since API 1.

Errors:

- `GS-API-085` when the kind is not serial, hid or ble, a filter is not valid, or 8 requests are already waiting. Pass { kind } and filters as HardwareFilter describes.
- `GS-API-080` when the logic belongs to an extension whose manifest does not ask for the permission hardware. Add "hardware" to the permissions in the extension's manifest.

```ts
const [pad] = await hardware.request({ kind: "hid", filters: [{ vendorId: 0x0fd9 }] });
```

## hardware.list()

The devices this logic may open, connected or not; present says whether each one is connected now.

```ts
hardware.list(): HardwareInfo[]
```

Returns `HardwareInfo[]`. stable since API 1.

Errors:

- `GS-API-080` when the logic belongs to an extension whose manifest does not ask for the permission hardware. Add "hardware" to the permissions in the extension's manifest.

```ts
for (const device of hardware.list()) log.info(device.name, device.present);
```

## hardware.open(id, options?)

Opens an allowed device; a device can be open only once in GameScrcpy. A device opened in a task closes when the task ends, one opened at the top level when the logic stops.

```ts
hardware.open(id: string, options?: SerialOptions): Promise<SerialDevice | HidDevice | BleDevice>
```

| Parameter | Type | Description |
|---|---|---|
| `id` | `string` | The id of an allowed device, from hardware.request() or hardware.list(). |
| `options` (optional) | `SerialOptions` | For a serial port: speed, data bits, parity, stop bits, flow control and the DTR and RTS lines. |

Returns `Promise<SerialDevice | HidDevice | BleDevice>`. stable since API 1.

Errors:

- `GS-API-081` when the device has not been allowed. Ask for it with hardware.request() first.
- `GS-API-082` when the device is not connected or went away. Plug it in or turn it on, then open it again.
- `GS-API-083` when Windows could not open, read or write the device, or it is already open. The message says why; close the program or logic that holds it.
- `GS-API-085` when the serial options are not possible, or this logic already has 16 devices open. Use the values SerialOptions describes, and close the devices you no longer need.
- `GS-API-080` when the logic belongs to an extension whose manifest does not ask for the permission hardware. Add "hardware" to the permissions in the extension's manifest.

```ts
const port = await hardware.open(board.id, { baudRate: 9600 }); await port.write("ping\n");
```

## hardware.forget(id)

Gives up the permission to use a device, as its remove button on the Extensions page does; the device closes if it is open.

```ts
hardware.forget(id: string): Promise<void>
```

| Parameter | Type | Description |
|---|---|---|
| `id` | `string` | The id of an allowed device. |

Returns `Promise<void>`. stable since API 1.

Errors:

- `GS-API-085` when the id is not a string that is not empty. Pass the id of a device from hardware.list().
- `GS-API-080` when the logic belongs to an extension whose manifest does not ask for the permission hardware. Add "hardware" to the permissions in the extension's manifest.

```ts
await hardware.forget(device.id);
```

## hardware.claim(resource, options?)

Tells other extensions and GameScrcpy's own sessions that this logic is using the hardware, so that they leave it alone. Resolves with false when someone else holds it. Devices opened with hardware.open() are claimed while they are open. A claim made in a task ends with the task, one made at the top level when the logic stops.

```ts
hardware.claim(resource: string, options?: HardwareClaimOptions): Promise<boolean>
```

| Parameter | Type | Description |
|---|---|---|
| `resource` | `string` | The hardware: serial:COM9, ble:AA:BB:CC:DD:EE:FF, hid: and the device path, or usb:vid:pid:serial number in hexadecimal, such as usb:2341:0043:A1B2. |
| `options` (optional) | `HardwareClaimOptions` | Whether to wait for whoever holds it, and how to let it go to others. |

Returns `Promise<boolean>`. stable since API 1.

Errors:

- `GS-API-085` when the resource does not name hardware, or the options are not an object. Name it as serial:, ble:, hid: or usb: and the rest, as the resource parameter describes.
- `GS-API-080` when the logic belongs to an extension whose manifest does not ask for the permission hardware. Add "hardware" to the permissions in the extension's manifest.

```ts
if (await hardware.claim("serial:COM9", { onWanted: () => true })) log.info("the board is ours");
```

## hardware.release(resource)

Lets go of hardware claimed with hardware.claim().

```ts
hardware.release(resource: string): void
```

| Parameter | Type | Description |
|---|---|---|
| `resource` | `string` | The hardware, named as for hardware.claim(). |

Returns `void`. stable since API 1.

Errors:

- `GS-API-085` when the resource does not name hardware, or the options are not an object. Name it as serial:, ble:, hid: or usb: and the rest, as the resource parameter describes.
- `GS-API-080` when the logic belongs to an extension whose manifest does not ask for the permission hardware. Add "hardware" to the permissions in the extension's manifest.

```ts
hardware.release("serial:COM9");
```
