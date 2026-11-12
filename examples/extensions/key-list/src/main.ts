type Control = {
    readonly id: string;
    readonly name: string;
    readonly type: "tap";
    readonly anchor: "topLeft";
    readonly offset: readonly [number, number];
    readonly inputs: { readonly press: string };
};

type Profile = {
    readonly format: "gamescrcpy.profile";
    readonly version: 3;
    readonly meta: { readonly name: string };
    readonly screen: { readonly scaleMode: "stretch" };
    readonly controls: readonly Control[];
};

type Loose = Readonly<Record<string, unknown>>;

const anchors: Readonly<Record<string, readonly [number, number]>> = {
    topLeft: [0, 0],
    top: [0.5, 0],
    topRight: [1, 0],
    left: [0, 0.5],
    center: [0.5, 0.5],
    right: [1, 0.5],
    bottomLeft: [0, 1],
    bottom: [0.5, 1],
    bottomRight: [1, 1],
};
const keyName = /^[A-Za-z0-9][A-Za-z0-9]{0,31}$/;
const nameLength = 64;

function codeOf(key: string): string {
    if (/^[A-Za-z]$/.test(key)) {
        return `Key${key.toUpperCase()}`;
    }
    if (/^[0-9]$/.test(key)) {
        return `Digit${key}`;
    }
    return key;
}

function keyOf(code: string): string {
    const letter = /^Key([A-Z])$/.exec(code);
    if (letter !== null) {
        return letter[1];
    }
    const digit = /^Digit([0-9])$/.exec(code);
    return digit !== null ? digit[1] : code;
}

function oneLine(text: string): string {
    return text.replace(/\s+/g, " ").trim();
}

function profileFrom(text: string, file: string): Profile {
    let name = oneLine(file.replace(/\.[^.]*$/, "")).slice(0, nameLength) || "Key list";
    const controls: Control[] = [];
    text.split(/\r?\n/).forEach((raw, index) => {
        const line = raw.trim();
        if (line === "") {
            return;
        }
        if (line.startsWith("#")) {
            const title = oneLine(line.slice(1));
            if (index === 0 && title !== "") {
                name = title.slice(0, nameLength);
            }
            return;
        }
        const [key = "", x = "", y = "", ...words] = line.split(/\s+/);
        const left = Number(x);
        const top = Number(y);
        if (!keyName.test(key) || !(left >= 0 && left <= 1) || !(top >= 0 && top <= 1)) {
            throw new Error(`Line ${index + 1} is not "key x y name", such as "J 0.82 0.71 Attack": ${line}`);
        }
        const number = controls.length + 1;
        controls.push({
            id: `tap-${number}`,
            name: (oneLine(words.join(" ")) || `${key} ${number}`).slice(0, nameLength),
            type: "tap",
            anchor: "topLeft",
            offset: [left, top],
            inputs: { press: codeOf(key) },
        });
    });
    if (controls.length === 0) {
        throw new Error('The key list has no keys. Write one on each line as "key x y name", such as "J 0.82 0.71 Attack".');
    }
    return { format: "gamescrcpy.profile", version: 3, meta: { name }, screen: { scaleMode: "stretch" }, controls };
}

function objectOf(value: unknown): Loose | null {
    return value !== null && typeof value === "object" && !Array.isArray(value) ? (value as Loose) : null;
}

function within(value: number): number {
    return Math.min(1, Math.max(0, value));
}

function textFrom(profile: unknown): string {
    const root = objectOf(profile);
    const meta = objectOf(root?.["meta"]);
    const screen = objectOf(root?.["screen"]);
    const title = meta?.["name"];
    const stretched = screen?.["scaleMode"] === "stretch";
    const portrait = meta?.["orientation"] === "portrait";
    const unit: readonly [number, number] = stretched ? [1, 1] : portrait ? [1, 9 / 16] : [9 / 16, 1];
    const listed: unknown = root?.["controls"];
    const controls: readonly unknown[] = Array.isArray(listed) ? listed : [];
    const lines = [`# ${typeof title === "string" && oneLine(title) !== "" ? oneLine(title) : "Key list"}`];
    let left = 0;
    for (const item of controls) {
        const control = objectOf(item);
        const press = objectOf(control?.["inputs"])?.["press"];
        const offset: unknown = control?.["offset"];
        if (control === null || typeof press !== "string" || press.includes(" ") || !Array.isArray(offset) ||
            offset.length !== 2 || typeof offset[0] !== "number" || typeof offset[1] !== "number") {
            left += 1;
            continue;
        }
        const anchorName = control["anchor"];
        const anchor = anchors[typeof anchorName === "string" && anchorName in anchors ? anchorName : "center"];
        const x = within(anchor[0] + offset[0] * unit[0]);
        const y = within(anchor[1] + offset[1] * unit[1]);
        const name = typeof control["name"] === "string" ? oneLine(control["name"]) : "";
        lines.push(`${keyOf(press)} ${x.toFixed(3)} ${y.toFixed(3)} ${name}`.trimEnd());
    }
    if (left > 0) {
        lines.push(`# ${left} controls without a single key or a position were left out`);
    }
    return `${lines.join("\n")}\n`;
}

export function convert(input: ConvertInput): ConvertOutput {
    if (input.to === "profile") {
        const profile = profileFrom(input.text, input.name);
        return { name: `${profile.meta.name.replace(/[\\/:*?"<>|]/g, "-")}.json`, json: profile };
    }
    return { text: textFrom(input.json()) };
}
