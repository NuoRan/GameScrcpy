# net

Connections from programs on this computer over HTTP on 127.0.0.1; the logic of an extension needs the permission network.listen.

## net.listen(port, handler, options?)

Opens an HTTP server on 127.0.0.1 that programs on this computer, such as Stream Deck or OBS, can call; nothing from other computers can reach it. Each request goes to the handler, which answers with an object, a text or nothing (204); a handler that throws answers 500. A server opened in a task closes when the task ends, one opened at the top level when the logic stops.

```ts
net.listen(port: number, handler: (request: NetRequest) => NetAnswer | Promise<NetAnswer>, options?: NetListenOptions): Promise<NetServer>
```

| Parameter | Type | Description |
|---|---|---|
| `port` | `number` | The port on 127.0.0.1, from 1024 to 65535. |
| `handler` | `(request: NetRequest) => NetAnswer \| Promise<NetAnswer>` | Answers one request; it runs as a task of its own for each request, several at a time. |
| `options` (optional) | `NetListenOptions` | Web pages of other sites that may call it from a browser. |

Returns `Promise<NetServer>`. stable since API 1.

Errors:

- `GS-API-100` when the logic belongs to an extension whose manifest does not ask for the permission network.listen, or local connections are turned off for it in its details. Add "network.listen" to the permissions in the extension's manifest, or turn local connections on.
- `GS-API-101` when the port is not from 1024 to 65535, or an origin is not written as https://host. Pass a port from 1024 to 65535 and origins such as "https://example.com".
- `GS-API-102` when another program uses the port, or the extension listens on 4 ports already. Pick another port, or close a server you no longer need.

```ts
await net.listen(8765, (request) => ({ json: { path: request.path, query: request.query } }));
```
