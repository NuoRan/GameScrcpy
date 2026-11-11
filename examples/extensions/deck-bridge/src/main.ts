type TapArgs = { readonly key: string; readonly ms?: number };

const page = ui.panel("deck");
const deck = input.source("deck");
const keyName = /^[A-Za-z0-9_-]{1,32}$/;
const savedPort = store.get("port");
const held = new Set<string>();

let port = typeof savedPort === "number" && Number.isInteger(savedPort) && savedPort >= 1024 && savedPort <= 65535
    ? savedPort
    : 8765;
let server: NetServer | null = null;
let problem = "";
let requests = 0;
let lastRequest = "";

function heldKeys(): string[] {
    return [...held].sort();
}

function listening(): boolean {
    return server !== null && server.isOpen;
}

function status(): StoreValue {
    return { listening: listening(), port, held: heldKeys() };
}

function press(key: string): void {
    deck.press(key);
    held.add(key);
}

function release(key: string): void {
    deck.release(key);
    held.delete(key);
}

async function tap(key: string, ms: number): Promise<void> {
    press(key);
    try {
        await sleep(ms);
    } finally {
        release(key);
    }
}

function holdTime(given: string | undefined): number {
    const ms = given === undefined ? 80 : Number(given);
    return Number.isInteger(ms) && ms >= 10 && ms <= 2000 ? ms : 80;
}

function note(line: string): void {
    requests += 1;
    lastRequest = line;
    draw();
}

async function answer(request: NetRequest): Promise<NetAnswer> {
    const parts = request.path.split("/").filter((part) => part !== "");
    if (parts.length === 0) {
        return { json: status() };
    }
    const action = parts[0];
    const key = parts[1];
    if (parts.length !== 2 || (action !== "press" && action !== "release" && action !== "tap")) {
        return { status: 404, json: { error: "use /press/KEY, /release/KEY or /tap/KEY" } };
    }
    if (request.method !== "POST") {
        return { status: 405, headers: { allow: "POST" }, json: { error: "send POST to press keys" } };
    }
    if (!keyName.test(key)) {
        return { status: 400, json: { error: "a key is 1 to 32 letters, digits, _ or -" } };
    }
    if (action === "press") {
        press(key);
    } else if (action === "release") {
        release(key);
    } else {
        await tap(key, holdTime(request.query["ms"]));
    }
    note(`${action} ${key}`);
    return { json: { held: heldKeys() } };
}

function draw(): void {
    const address = `http://127.0.0.1:${port}`;
    page.render({
        type: "column",
        children: [
            {
                type: "text",
                style: "heading",
                tone: listening() ? "good" : "warn",
                text: listening() ? `Listening on ${address}` : problem !== "" ? problem : "Not listening",
            },
            {
                type: "text",
                format: "markdown",
                text: [
                    `- Send **POST** to \`${address}/tap/k1\` to tap the key k1, or to \`/press/k1\` and \`/release/k1\` to hold it`,
                    "- Bind it in a profile as `Ext.example.deck-bridge.deck.k1`, or press it while the key editor waits for a key",
                    "- The keys go to the phone whose mirror window is in front",
                ].join("\n"),
            },
            {
                type: "row",
                children: [
                    { type: "number", id: "port", title: "Port", value: port, min: 1024, max: 65535, step: 1 },
                    { type: "button", id: "listen", title: listening() ? "Listen again" : "Listen", style: "primary" },
                ],
            },
            {
                type: "keyValue",
                rows: [
                    { key: "Requests", value: requests },
                    { key: "Last request", value: lastRequest === "" ? null : lastRequest },
                    { key: "Keys held", value: held.size === 0 ? null : heldKeys().join(", ") },
                ],
            },
        ],
    });
}

async function listen(): Promise<void> {
    server?.close();
    server = null;
    try {
        server = await net.listen(port, answer);
        problem = "";
        log.info(`listening on http://127.0.0.1:${port}`);
    } catch (error) {
        problem = error instanceof Error ? error.message : String(error);
        log.warn(`cannot listen on port ${port}: ${problem}`);
    }
    draw();
}

page.on("change", ({ id, value }) => {
    if (id === "port" && typeof value === "number" && Number.isInteger(value) && value >= 1024 && value <= 65535) {
        port = value;
        store.set("port", port);
        draw();
    }
});

page.on("click", async ({ id }) => {
    if (id === "listen") {
        await listen();
    }
});

page.on("open", () => draw());

ext.provide("tap", async ({ key, ms }: TapArgs) => {
    if (!keyName.test(key)) {
        throw new Error("a key is 1 to 32 letters, digits, _ or -");
    }
    await tap(key, ms ?? 80);
    note(`tap ${key} from another extension`);
    return { held: heldKeys() };
});

ext.provide("status", () => status());

draw();
void listen();
