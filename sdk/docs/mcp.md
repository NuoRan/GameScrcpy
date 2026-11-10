# mcp

Tools that the app script of an extension offers to AI clients over the local MCP connection.

## mcp.tool(name, spec, handler)

Offers a function of this extension to AI clients as a tool of the local MCP connection. The user confirms each call, as for running a script on a phone. The tool is gone when the subscription is disposed or the logic stops.

```ts
mcp.tool(name: string, spec: McpToolSpec, handler: (input: any, event: TaskContext) => unknown): Subscription
```

| Parameter | Type | Description |
|---|---|---|
| `name` | `string` | The tool's name: a lowercase letter, then up to 31 lowercase letters, digits and underscores. AI clients see it as ext_<extension id>_<name>, with the dots and hyphens of the id written as underscores. |
| `spec` | `McpToolSpec` | What the tool does, for the AI, and the JSON Schema of its input. |
| `handler` | `(input: any, event: TaskContext) => unknown` | Runs as its own task for each call and gives back the tool's result as a JSON value; an error it throws is passed to the client as the tool's error. A call that is not answered within 60 seconds fails. |

Returns `Subscription`. stable since API 1.

Errors:

- `GS-API-074` when the logic is not the app script of an extension, the name or the description is not valid, the input schema is not an object of type object or is larger than 16 KiB, or the extension already offers 32 tools. Call mcp.tool in a script that manifest.json lists with "run": "app", with a valid name, a description and an input schema of type object.

```ts
mcp.tool("greet", { description: "Says hello to someone", input: { type: "object", properties: { name: { type: "string" } } } }, (input) => "Hello " + input.name);
```
