type Area = { readonly x: number; readonly y: number; readonly width: number; readonly height: number };

const button = ui.toolbar("read");
const panel = ui.panel("area");
const smallest = 0.05;
const mostSaid = 1000;

let area: Area = areaOf(store.get("area")) ?? { x: 0, y: 0.6, width: 0.4, height: 0.3 };
let language = textOf(store.get("language"));
let lastText = "";
let problem = "";
let reading = false;

function textOf(value: StoreValue | undefined): string {
    return typeof value === "string" ? value : "";
}

function numberIn(value: StoreValue | undefined, low: number, high: number): number | null {
    return typeof value === "number" && value >= low && value <= high ? value : null;
}

function areaOf(value: StoreValue | undefined): Area | null {
    if (value === null || value === undefined || typeof value !== "object" || Array.isArray(value)) {
        return null;
    }
    const x = numberIn(value["x"], 0, 1 - smallest);
    const y = numberIn(value["y"], 0, 1 - smallest);
    const width = numberIn(value["width"], smallest, 1);
    const height = numberIn(value["height"], smallest, 1);
    if (x === null || y === null || width === null || height === null || x + width > 1 || y + height > 1) {
        return null;
    }
    return { x, y, width, height };
}

function errorText(error: unknown): string {
    return error instanceof Error ? error.message : String(error);
}

function languages(): readonly string[] {
    return vision.engines().find((engine) => engine.id === "system")?.languages ?? [];
}

function changeArea(key: keyof Area, value: number): void {
    const next = { ...area, [key]: value };
    const width = Math.min(Math.max(next.width, smallest), 1 - next.x);
    const height = Math.min(Math.max(next.height, smallest), 1 - next.y);
    area = { x: next.x, y: next.y, width, height };
    store.set("area", { x: area.x, y: area.y, width: area.width, height: area.height });
}

async function readNow(): Promise<void> {
    if (reading) {
        return;
    }
    reading = true;
    problem = "";
    refresh();
    try {
        const found = await vision.recognize("system", "ocr", area, language === "" ? undefined : { language });
        if (found === null) {
            problem = "The phone has shown no picture yet.";
        } else {
            const text = found
                .map((part) => ("text" in part ? part.text : ""))
                .join(" ")
                .replace(/\s+/g, " ")
                .trim()
                .slice(0, mostSaid);
            lastText = text;
            if (text === "") {
                problem = "There is no text in that part of the picture.";
            } else {
                refresh();
                await speech.say(text, { interrupt: true });
            }
        }
    } catch (error) {
        problem = errorText(error);
        log.warn(`cannot read aloud: ${problem}`);
    }
    reading = false;
    refresh();
}

function refresh(): void {
    button.set(reading ? { checked: true, title: "Reading aloud…" } : null);
    const known = languages();
    panel.render({
        type: "column",
        children: [
            {
                type: "text",
                style: "caption",
                tone: problem === "" ? "muted" : "bad",
                text: problem !== "" ? problem
                    : "The toolbar button reads the text in this part of the picture aloud, with the text recognition of Windows.",
            },
            { type: "slider", id: "x", title: "Left", value: area.x, min: 0, max: 1 - smallest, step: 0.01 },
            { type: "slider", id: "y", title: "Top", value: area.y, min: 0, max: 1 - smallest, step: 0.01 },
            { type: "slider", id: "width", title: "Width", value: area.width, min: smallest, max: 1, step: 0.01 },
            { type: "slider", id: "height", title: "Height", value: area.height, min: smallest, max: 1, step: 0.01 },
            {
                type: "choice",
                id: "language",
                title: "Language of the text",
                value: known.includes(language) ? language : "default",
                options: [{ id: "default", title: "As in Windows" }, ...known.map((tag) => ({ id: tag, title: tag }))],
            },
            { type: "button", id: "read", title: reading ? "Reading…" : "Read now", style: "primary", enabled: !reading },
            { type: "keyValue", rows: [{ key: "Last read", value: lastText === "" ? null : lastText.slice(0, 200) }] },
        ],
    });
}

button.on("click", () => readNow());

panel.on("change", ({ id, value }) => {
    if ((id === "x" || id === "y" || id === "width" || id === "height") && typeof value === "number") {
        changeArea(id, value);
    } else if (id === "language" && typeof value === "string") {
        language = value === "default" ? "" : value;
        store.set("language", language);
    }
    refresh();
}, { again: "queue" });

panel.on("click", async ({ id }) => {
    if (id === "read") {
        await readNow();
    }
});

panel.on("open", () => refresh());

refresh();
