import type { Region } from "./types";
import * as helpers from "./helpers.ts";

type Mode = "fast" | "slow";
interface Options {
    mode: Mode;
}

const table: Record<string, number> = { a: 1, "b-c": 2, [`k${1}`]: 3 };
let counter = 0;
counter++;
counter += 2;
const { first, second = 2, ...others } = { first: 1, third: 3 };
const [head, , tail = 5, ...rest] = [1, 2, 3, 4, 5];

label: for (const item of [1, 2, 3]) {
    for (const inner in table) {
        if (inner === "a") continue label;
        if (item > 2) break label;
    }
}

do {
    counter--;
} while (counter > 0);

switch (counter) {
    case 0:
        log.info("zero");
        break;
    case 1: {
        log.info("one");
        break;
    }
    default:
        log.warn(`many: ${counter}`);
}

try {
    throw new Error("nope");
} catch ({ message }) {
    log.error(message);
} finally {
    counter = 0;
}

try {
    JSON.parse("{");
} catch {
    log.warn("bad json");
}

class Base<T> {
    static #count = 0;
    static {
        Base.#count = 1;
    }
    #secret = 42;
    protected readonly value: T;

    constructor(value: T) {
        this.value = value;
    }

    get secret(): number {
        return this.#secret;
    }

    set secret(next: number) {
        this.#secret = next;
    }

    static make(): Base<number> {
        return new Base(1);
    }

    has(other: object): boolean {
        return #secret in other;
    }

    *values(): Generator<number> {
        yield 1;
        yield* [2, 3];
    }

    async *stream() {
        for await (const chunk of [Promise.resolve(1)]) {
            yield chunk;
        }
    }
}

class Derived extends Base<string> {
    override get secret(): number {
        return super.secret + 1;
    }
}

const pattern = /a[/\]]b/gi.test("a/b") ? /x/ : /y/u;
const nested = `outer ${`inner ${counter + 1}`} ${table["a"] ?? 0}`;
const tagged = String.raw`a\nb${counter}`;
const optional = (globalThis as any)?.missing?.deep?.(1)?.[0];
const nullish = optional ?? counter ?? 0;
counter ||= 1;
counter &&= 2;
counter ??= 3;
const power = 2 ** 3 ** 2;
const arrows = [(a: number) => a * 2, async (b = 1) => await sleep(b), (...c: number[]) => c.length, x => ({ x })];
const getters = {
    get size() {
        return 1;
    },
    set size(value) {
        counter = value;
    },
    async *walk() {},
    [Symbol.iterator]: function* () {},
};
const deleted = delete table.a;
const kind = typeof counter === "number" && !(counter instanceof Object) && "a" in table;
const regionValue = { x: 0.1, y: 0.2, width: 0.3, height: 0.4 } satisfies Region;
void 0;
export const answer = 42 as const;
export default function main(): void {
    const meta = import.meta;
    log.info(meta, helpers);
}
export { counter as count };
