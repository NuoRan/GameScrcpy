type Command = { readonly phrase: string; readonly key: string; readonly reply: string };

const page = ui.panel("voice");
const keys = input.source("voice");
const keyName = /^[A-Za-z0-9_-]{1,32}$/;
const english: readonly Command[] = [
    { phrase: "open map", key: "map", reply: "Map" },
    { phrase: "open bag", key: "bag", reply: "Bag" },
    { phrase: "reload", key: "reload", reply: "Reloading" },
];
const chinese: readonly Command[] = [
    { phrase: "打开地图", key: "map", reply: "地图" },
    { phrase: "打开背包", key: "bag", reply: "背包" },
    { phrase: "换弹", key: "reload", reply: "换弹" },
];

let engine = textOf(store.get("engine"));
let language = textOf(store.get("language"));
let confidence = confidenceOf(store.get("confidence"));
let listening = store.get("listening") !== false;
let commands: Command[] = commandsFrom(store.get("commands")) ?? [...defaultCommands()];
let listener: SpeechListener | null = null;
let generation = 0;
let problem = "";
let notice = "";
let lastHeard = "";
let selected = "";
let draftPhrase = "";
let draftKey = "";
let draftReply = "";

function textOf(value: StoreValue | undefined): string {
    return typeof value === "string" ? value : "";
}

function confidenceOf(value: StoreValue | undefined): number {
    return typeof value === "number" && value >= 0.3 && value <= 0.95 ? value : 0.6;
}

function usable(phrase: string, key: string, reply: string): boolean {
    return phrase !== "" && phrase.length <= 64 && (key === "" || keyName.test(key)) && reply.length <= 60;
}

function commandsFrom(value: StoreValue | undefined): Command[] | null {
    if (!Array.isArray(value)) {
        return null;
    }
    const found: Command[] = [];
    for (const item of value) {
        if (item !== null && typeof item === "object" && !Array.isArray(item)) {
            const phrase = item["phrase"];
            const key = item["key"];
            const reply = item["reply"];
            if (typeof phrase === "string" && typeof key === "string" && typeof reply === "string" &&
                usable(phrase, key, reply)) {
                found.push({ phrase, key, reply });
            }
        }
    }
    return found;
}

function listenEngines(): SpeechEngineInfo[] {
    return speech.engines().filter((each) => each.listen && each.id.length <= 64);
}

function chosenEngine(engines: readonly SpeechEngineInfo[]): SpeechEngineInfo | undefined {
    return engines.find((each) => each.id === engine) ?? engines[0];
}

function defaultCommands(): readonly Command[] {
    const tag = language !== "" ? language : (chosenEngine(listenEngines())?.languages[0] ?? "");
    return tag.toLowerCase().startsWith("zh") ? chinese : english;
}

function saveCommands(): void {
    store.set("commands", commands.map((command) => ({ phrase: command.phrase, key: command.key, reply: command.reply })));
}

async function heard({ phrase, text, confidence: sure }: TaskContext & SpeechHeard): Promise<void> {
    lastHeard = `${text} (${Math.round(sure * 100)}%)`;
    draw();
    const said = phrase === null ? "" : phrase.toLowerCase();
    const command = commands.find((each) => each.phrase.toLowerCase() === said);
    if (command === undefined) {
        return;
    }
    if (command.key !== "") {
        keys.press(command.key);
        try {
            await sleep(80);
        } finally {
            keys.release(command.key);
        }
    }
    if (command.reply !== "") {
        await speech.say(command.reply, { interrupt: true });
    }
}

async function restart(): Promise<void> {
    generation += 1;
    const mine = generation;
    listener?.close();
    listener = null;
    problem = "";
    if (listening && commands.length > 0) {
        try {
            const made = await speech.listen(commands.map((command) => command.phrase), heard, {
                engine: engine === "" ? undefined : engine,
                language: language === "" ? undefined : language,
                confidence,
            });
            if (mine !== generation) {
                made.close();
                return;
            }
            listener = made;
        } catch (error) {
            if (mine !== generation) {
                return;
            }
            problem = error instanceof Error ? error.message : String(error);
            log.warn(`cannot listen: ${problem}`);
        }
    }
    draw();
}

async function addCommand(): Promise<void> {
    const phrase = draftPhrase.trim();
    const key = draftKey.trim();
    const reply = draftReply.trim();
    if (!usable(phrase, key, reply)) {
        notice = "A command needs words to say (up to 64 characters); its key is up to 32 letters, digits, _ or -, " +
            "and its answer up to 60 characters.";
        draw();
        return;
    }
    commands = [...commands.filter((each) => each.phrase !== phrase), { phrase, key, reply }].slice(-256);
    draftPhrase = "";
    draftKey = "";
    draftReply = "";
    notice = "";
    saveCommands();
    await restart();
}

function draw(): void {
    const engines = listenEngines();
    const languages = chosenEngine(engines)?.languages ?? [];
    const on = listener !== null && listener.isOpen;
    page.render({
        type: "column",
        children: [
            {
                type: "text",
                style: "heading",
                tone: on ? "good" : problem !== "" ? "bad" : "muted",
                text: on ? `Listening for ${commands.length} commands` : problem !== "" ? problem : "Not listening",
            },
            { type: "toggle", id: "listening", title: "Listen for voice commands", value: listening },
            {
                type: "row",
                children: [
                    {
                        type: "choice",
                        id: "engine",
                        title: "Engine",
                        value: engines.some((each) => each.id === engine) ? engine : "default",
                        options: [
                            { id: "default", title: "As in Settings" },
                            ...engines.map((each) => ({ id: each.id, title: each.title })),
                        ],
                    },
                    {
                        type: "choice",
                        id: "language",
                        title: "Language",
                        value: languages.includes(language) ? language : "default",
                        options: [
                            { id: "default", title: "As in Settings" },
                            ...languages.map((tag) => ({ id: tag, title: tag })),
                        ],
                    },
                ],
            },
            {
                type: "slider",
                id: "confidence",
                title: "How sure the engine must be",
                value: confidence,
                min: 0.3,
                max: 0.95,
                step: 0.05,
            },
            { type: "keyValue", rows: [{ key: "Last heard", value: lastHeard === "" ? null : lastHeard }] },
            {
                type: "list",
                id: "commands",
                selected,
                items: commands.map((command) => ({
                    id: command.phrase,
                    title: command.phrase,
                    subtitle: [command.key === "" ? "no key" : `key ${command.key}`, command.reply]
                        .filter((part) => part !== "")
                        .join(" · "),
                })),
            },
            {
                type: "button",
                id: "remove",
                title: "Remove the selected command",
                style: "danger",
                enabled: selected !== "",
            },
            {
                type: "group",
                title: "Add a command",
                children: [
                    { type: "field", id: "phrase", title: "Say", value: draftPhrase, placeholder: "open map" },
                    { type: "field", id: "key", title: "Key", value: draftKey, placeholder: "map" },
                    { type: "field", id: "reply", title: "Answer", value: draftReply, placeholder: "Map" },
                    { type: "button", id: "add", title: "Add", style: "primary" },
                    { type: "text", style: "caption", tone: "warn", text: notice },
                ],
            },
            {
                type: "text",
                format: "markdown",
                tone: "muted",
                text: "In a profile, bind `Ext.example.voice-commands.voice.map` (the key of a command) to the button " +
                    "the command should press, or say the command while the key editor waits for a key.",
            },
        ],
    });
}

page.on("change", async ({ id, value }) => {
    if (id === "listening" && typeof value === "boolean") {
        listening = value;
        store.set("listening", listening);
        await restart();
    } else if (id === "engine" && typeof value === "string") {
        engine = value === "default" ? "" : value;
        language = "";
        store.set("engine", engine);
        store.set("language", language);
        await restart();
    } else if (id === "language" && typeof value === "string") {
        language = value === "default" ? "" : value;
        store.set("language", language);
        await restart();
    } else if (id === "confidence" && typeof value === "number") {
        confidence = confidenceOf(value);
        store.set("confidence", confidence);
        await restart();
    } else if (id === "phrase" && typeof value === "string") {
        draftPhrase = value;
    } else if (id === "key" && typeof value === "string") {
        draftKey = value;
    } else if (id === "reply" && typeof value === "string") {
        draftReply = value;
    }
}, { again: "queue" });

page.on("click", async ({ id }) => {
    if (id === "add") {
        await addCommand();
    } else if (id === "remove" && selected !== "") {
        commands = commands.filter((each) => each.phrase !== selected);
        selected = "";
        saveCommands();
        await restart();
    }
}, { again: "queue" });

page.on("select", ({ id, value }) => {
    if (id === "commands" && typeof value === "string") {
        selected = value;
        draw();
    }
});

page.on("open", () => draw());

draw();
void restart();
