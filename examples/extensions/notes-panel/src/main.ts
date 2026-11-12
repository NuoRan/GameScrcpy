type Note = { readonly path: string; readonly title: string; readonly text: string; readonly modified: number };

type Release = {
    readonly tag_name?: unknown;
    readonly published_at?: unknown;
    readonly html_url?: unknown;
};

const page = ui.panel("notes");
const releaseUrl = "https://api.github.com/repos/NuoRan/GameScrcpy/releases/latest";
const mostNotes = 200;
const mostNoteBytes = 65536;

let folder: FolderHandle | null = null;
let notes: Note[] = [];
let selected = "";
let draftTitle = "";
let draftText = "";
let problem = "";
let fetching = false;
let asked = false;

function errorText(error: unknown): string {
    return error instanceof Error ? error.message : String(error);
}

function fileName(title: string): string {
    const base = title.replace(/[\\/:*?"<>|]/g, "-").replace(/\s+/g, " ").trim().slice(0, 60);
    return `${base === "" ? "note" : base}.txt`;
}

async function opened(): Promise<FolderHandle> {
    if (folder === null || !folder.isOpen) {
        folder = await files.folder("notes");
    }
    return folder;
}

async function load(): Promise<void> {
    try {
        const notesFolder = await opened();
        const found: Note[] = [];
        for (const entry of await notesFolder.list()) {
            if (entry.folder || !entry.name.toLowerCase().endsWith(".txt") || entry.size > mostNoteBytes) {
                continue;
            }
            found.push({
                path: entry.path,
                title: entry.name.slice(0, -4),
                text: await notesFolder.text(entry.path),
                modified: entry.modified,
            });
        }
        notes = found.sort((a, b) => b.modified - a.modified).slice(0, mostNotes);
        if (!notes.some((note) => note.path === selected)) {
            selected = "";
        }
        problem = "";
    } catch (error) {
        problem = errorText(error);
    }
    draw();
}

async function save(title: string, text: string): Promise<void> {
    await (await opened()).write(fileName(title), text);
}

async function addNote(): Promise<void> {
    const title = draftTitle.trim();
    if (title === "") {
        problem = "Give the note a title.";
        draw();
        return;
    }
    try {
        await save(title, draftText.trim());
        draftTitle = "";
        draftText = "";
        await load();
    } catch (error) {
        problem = errorText(error);
        draw();
    }
}

async function addRelease(): Promise<void> {
    fetching = true;
    draw();
    try {
        const reply = await net.fetch(releaseUrl, { headers: { accept: "application/vnd.github+json" } });
        if (reply.status !== 200) {
            throw new Error(`GitHub answered with status ${reply.status}.`);
        }
        const release = reply.json() as Release;
        const tag = typeof release.tag_name === "string" ? release.tag_name : "";
        if (tag === "") {
            throw new Error("GitHub did not name the newest release.");
        }
        const day = typeof release.published_at === "string" ? release.published_at.slice(0, 10) : "";
        const link = typeof release.html_url === "string" ? release.html_url : "";
        await save(`GameScrcpy ${tag}`, [day === "" ? "" : `Released ${day}`, link].filter((part) => part !== "").join("\n"));
        await load();
    } catch (error) {
        problem = errorText(error);
    }
    fetching = false;
    draw();
}

async function removeSelected(): Promise<void> {
    const note = notes.find((each) => each.path === selected);
    if (note === undefined) {
        return;
    }
    try {
        await (await opened()).remove(note.path);
        selected = "";
        await load();
    } catch (error) {
        problem = errorText(error);
        draw();
    }
}

function draw(): void {
    const chosen = notes.find((note) => note.path === selected);
    page.render({
        type: "column",
        children: [
            { type: "text", style: "heading", text: folder === null ? "Notes" : `${notes.length} notes` },
            {
                type: "text",
                style: "caption",
                tone: problem === "" ? "muted" : "bad",
                text: problem !== "" ? problem
                    : folder === null ? "The first time, choose the folder your notes go to."
                    : "Each note is a text file in the folder you chose for Notes.",
            },
            {
                type: "row",
                children: [
                    { type: "button", id: "reload", title: folder === null ? "Choose the folder" : "Reload", icon: "refresh-cw" },
                    {
                        type: "button",
                        id: "release",
                        title: fetching ? "Asking GitHub…" : "Add the newest GameScrcpy release",
                        icon: "import",
                        enabled: !fetching,
                    },
                ],
            },
            {
                type: "list",
                id: "notes",
                selected,
                items: notes.map((note) => ({
                    id: note.path,
                    title: note.title,
                    subtitle: note.text.split("\n", 1)[0].slice(0, 80),
                })),
            },
            {
                type: "group",
                title: chosen === undefined ? "No note selected" : chosen.title,
                children: [
                    { type: "text", text: chosen === undefined ? "Pick a note in the list to read it." : chosen.text },
                    { type: "button", id: "remove", title: "Remove the note", style: "danger", enabled: chosen !== undefined },
                ],
            },
            {
                type: "group",
                title: "Add a note",
                children: [
                    { type: "field", id: "title", title: "Title", value: draftTitle, placeholder: "Boss timings" },
                    { type: "field", id: "text", title: "Note", value: draftText, placeholder: "The dragon comes back every 5 minutes" },
                    { type: "button", id: "add", title: "Add", style: "primary" },
                ],
            },
        ],
    });
}

page.on("change", ({ id, value }) => {
    if (id === "title" && typeof value === "string") {
        draftTitle = value;
    } else if (id === "text" && typeof value === "string") {
        draftText = value;
    }
}, { again: "queue" });

page.on("submit", async ({ id }) => {
    if (id === "title" || id === "text") {
        await addNote();
    }
}, { again: "queue" });

page.on("click", async ({ id }) => {
    if (id === "add") {
        await addNote();
    } else if (id === "reload") {
        await load();
    } else if (id === "release") {
        await addRelease();
    } else if (id === "remove") {
        await removeSelected();
    }
}, { again: "queue" });

page.on("select", ({ id, value }) => {
    if (id === "notes" && typeof value === "string") {
        selected = value;
        draw();
    }
});

page.on("open", async () => {
    if (folder === null && asked) {
        draw();
        return;
    }
    asked = true;
    await load();
});

draw();
