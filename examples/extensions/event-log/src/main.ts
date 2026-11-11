type Entry = { readonly at: string; readonly topic: string; readonly phone: string; readonly detail: string };

const phoneTopics = [
    "device.connected",
    "device.disconnected",
    "session.started",
    "session.ended",
    "profile.applied",
    "app.foreground",
    "orientation.changed",
    "recording.started",
    "recording.ended",
    "window.focused",
];
const appTopics = ["extension.enabled", "extension.disabled", "settings.changed"];
const most = 200;
const page = ui.panel("log");
const entries: Entry[] = [];

let paused = false;
let filter = "all";
let seen = 0;

function clock(): string {
    const now = new Date();
    return [now.getHours(), now.getMinutes(), now.getSeconds()].map((part) => String(part).padStart(2, "0")).join(":");
}

function cut(text: string, length: number): string {
    const characters = Array.from(text);
    return characters.length <= length ? text : characters.slice(0, length - 1).join("") + "…";
}

function shown(value: StoreValue): string {
    return typeof value === "string" ? value : JSON.stringify(value);
}

function describe(topic: string, value: StoreValue): Entry {
    const fields: { readonly [key: string]: StoreValue } =
        value !== null && typeof value === "object" && !Array.isArray(value) ? value : {};
    const name = fields["name"];
    const phone = fields["phone"];
    const rest = Object.entries(fields)
        .filter(([key]) => key !== "name" && key !== "phone")
        .map(([key, item]) => `${key}: ${shown(item)}`)
        .join(", ");
    const who = typeof name === "string" && name !== "" ? name : typeof phone === "string" ? phone : "";
    return { at: clock(), topic, phone: cut(who, 60), detail: cut(rest, 200) };
}

function visible(entry: Entry): boolean {
    if (filter === "phones") {
        return phoneTopics.includes(entry.topic);
    }
    if (filter === "app") {
        return appTopics.includes(entry.topic);
    }
    return true;
}

function draw(): void {
    const rows = entries.filter(visible).map((entry) => [entry.at, entry.topic, entry.phone, entry.detail]);
    page.render({
        type: "column",
        children: [
            {
                type: "row",
                children: [
                    { type: "toggle", id: "pause", title: "Pause", value: paused },
                    {
                        type: "choice",
                        id: "filter",
                        title: "Show",
                        value: filter,
                        options: [
                            { id: "all", title: "Everything" },
                            { id: "phones", title: "Phones and windows" },
                            { id: "app", title: "Extensions and settings" },
                        ],
                    },
                    { type: "button", id: "clear", title: "Clear", enabled: entries.length > 0 },
                ],
            },
            {
                type: "text",
                style: "caption",
                tone: "muted",
                text: `${seen} events so far; the newest ${most} are kept. ` +
                    "The events of phones and windows need the permission events.app.",
            },
            rows.length === 0
                ? {
                    type: "text",
                    text: "Nothing has happened yet. Connect a phone, apply a profile or turn an extension off and on.",
                }
                : { type: "table", columns: ["Time", "Topic", "Phone", "Details"], rows },
        ],
    });
}

function record(topic: string, value: StoreValue): void {
    seen += 1;
    if (paused) {
        return;
    }
    entries.unshift(describe(topic, value));
    if (entries.length > most) {
        entries.length = most;
    }
    draw();
}

page.on("change", ({ id, value }) => {
    if (id === "pause" && typeof value === "boolean") {
        paused = value;
        draw();
    } else if (id === "filter" && typeof value === "string") {
        filter = value;
        draw();
    }
});

page.on("click", ({ id }) => {
    if (id === "clear") {
        entries.length = 0;
        draw();
    }
});

page.on("open", () => draw());

for (const name of [...phoneTopics, ...appTopics]) {
    events.on(name, ({ topic, value }) => record(topic, value), { again: "queue" });
}

draw();
log.info(`listening to ${phoneTopics.length + appTopics.length} topics`);
