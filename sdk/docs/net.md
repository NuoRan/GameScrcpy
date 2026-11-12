# net

Web services over https and WebSocket, made by GameScrcpy for the logic, and connections from programs on this computer over HTTP on 127.0.0.1. The logic of an extension needs the permission network, which lists the hosts it may reach, and network.listen to listen.

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

## net.fetch(url, options?)

Asks a web service over https and resolves with its answer, whatever its status; GameScrcpy makes the request, so the logic never holds a connection of its own. The logic of an extension may only reach the hosts its manifest lists under the permission network, and redirects are followed only to such hosts; your own logic may reach any https host. The body is at most 1 MiB and the answer at most 8 MiB, and each logic may have 8 requests going at once. Cookies are not kept: set the cookie header yourself when a service needs one. Cancelling the task cancels the request.

```ts
net.fetch(url: string, options?: NetFetchOptions): Promise<NetResponse>
```

| Parameter | Type | Description |
|---|---|---|
| `url` | `string` | An https address on a host this logic may reach, up to 4096 characters. |
| `options` (optional) | `NetFetchOptions` | The method, headers and body of the request, and how long to wait for it. |

Returns `Promise<NetResponse>`. stable since API 1.

Errors:

- `GS-API-155` when the logic belongs to an extension whose manifest does not ask for the permission network or does not list the host, or the server sends the request on to a host it does not list or to an address that is not https. List the host, such as "api.example.com" or "*.example.com", under hosts of the permission network in the extension's manifest.
- `GS-API-156` when the address is not https, the method is not GET, HEAD, POST, PUT, PATCH, DELETE or OPTIONS, a header is one GameScrcpy sets itself or one HTTP does not allow, the body is not text or bytes, or timeoutMs is not from 1000 to 60000. Use an https:// address and the options as NetFetchOptions describes.
- `GS-API-157` when the body is over 1 MiB, the answer over 8 MiB, or the logic has 8 requests going already. Send and ask for less, or wait for a request to finish.
- `GS-API-158` when the host could not be reached, its certificate is not valid, the connection broke, the server sent the request on more than 5 times, or no answer came within timeoutMs; the message says which. Check the address and the network, or allow more time.

```ts
const reply = await net.fetch("https://api.example.com/scores", { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ score: 10 }) }); log.info(reply.status, reply.json());
```

## net.socket(url, options?)

Opens a WebSocket connection over wss and resolves once the server has taken it; the hosts are the same as for net.fetch. Messages are text or bytes of at most 1 MiB each, and each logic may have 4 sockets open. A socket opened in a task closes when the task ends, one opened at the top level when the logic stops.

```ts
net.socket(url: string, options?: NetSocketOptions): Promise<NetSocket>
```

| Parameter | Type | Description |
|---|---|---|
| `url` | `string` | A wss address on a host this logic may reach, up to 4096 characters. |
| `options` (optional) | `NetSocketOptions` | The subprotocols to offer the server. |

Returns `Promise<NetSocket>`. stable since API 1.

Errors:

- `GS-API-155` when the logic belongs to an extension whose manifest does not ask for the permission network or does not list the host, or the server sends the request on to a host it does not list or to an address that is not https. List the host, such as "api.example.com" or "*.example.com", under hosts of the permission network in the extension's manifest.
- `GS-API-156` when the address is not wss, or a protocol is not a name that HTTP allows. Use a wss:// address and protocols such as "chat".
- `GS-API-157` when the logic has 4 sockets open already. Close a socket you no longer need.
- `GS-API-158` when the host could not be reached, its certificate is not valid, or the server did not take the WebSocket connection; the message says why. Check the address, the network and the protocols.

```ts
const live = await net.socket("wss://live.example.com/feed"); live.onMessage(({ data }) => log.info(data)); live.send("hello");
```
