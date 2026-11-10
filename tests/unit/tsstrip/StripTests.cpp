#include "Scanner.h"

#include <gs/tsstrip/Strip.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

using namespace gs::tsstrip;

namespace {

#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
constexpr bool kOptimised = true;
constexpr int kRuns = 31;
#else
constexpr bool kOptimised = false;
constexpr int kRuns = 3;
#endif

struct Cut {
    std::string_view text;
    char last = ' ';
    char first = ' ';
};

std::string erasing(std::string_view source, std::initializer_list<Cut> cuts)
{
    std::string out(source);
    std::size_t from = 0;
    for (const Cut& cut : cuts) {
        const std::size_t at = source.find(cut.text, from);
        REQUIRE_MESSAGE(at != std::string_view::npos, "missing cut: " << std::string(cut.text));
        for (std::size_t i = at; i < at + cut.text.size(); ++i) {
            if (out[i] != '\n' && out[i] != '\r') {
                out[i] = ' ';
            }
        }
        if (cut.first != ' ') {
            out[at] = cut.first;
        }
        if (cut.last != ' ') {
            out[at + cut.text.size() - 1] = cut.last;
        }
        from = at + cut.text.size();
    }
    return out;
}

std::string describe(const std::vector<Problem>& problems)
{
    std::string text;
    for (const Problem& problem : problems) {
        text += codeName(problem.code) + " " + std::to_string(problem.line) + ":" + std::to_string(problem.column) + " "
              + problem.message + "\n";
    }
    return text;
}

void checkPositions(std::string_view source, std::string_view output)
{
    REQUIRE(output.size() == source.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
        const char before = source[i];
        const char after = output[i];
        if (before == '\n' || before == '\r') {
            CHECK(after == before);
        } else if (after != before) {
            CHECK((after == ' ' || after == ';' || after == '(' || after == ')'));
        }
    }
}

void checkStrip(std::string_view source, std::initializer_list<Cut> cuts)
{
    const std::string input(source);
    CAPTURE(input);
    const Result result = strip(source);
    CHECK_MESSAGE(result.problems.empty(), describe(result.problems));
    CHECK(result.javascript == erasing(source, cuts));
    checkPositions(source, result.javascript);
}

void checkUnchanged(std::string_view source)
{
    checkStrip(source, {});
}

void checkProblem(std::string_view source, ProblemCode code, std::uint32_t line, std::uint32_t column)
{
    const std::string input(source);
    CAPTURE(input);
    const Result result = strip(source);
    REQUIRE_MESSAGE(!result.problems.empty(), "no problem reported");
    const Problem& first = result.problems.front();
    CHECK_MESSAGE(first.code == code, describe(result.problems));
    CHECK(first.line == line);
    CHECK(first.column == column);
    CHECK(!first.message.empty());
}

bool regexAllowedAfter(const detail::Token& token)
{
    using detail::Keyword;
    using detail::TokenType;
    switch (token.type) {
    case TokenType::Name:
        return token.keyword == Keyword::Return || token.keyword == Keyword::Typeof || token.keyword == Keyword::In
            || token.keyword == Keyword::Of || token.keyword == Keyword::Case || token.keyword == Keyword::Void;
    case TokenType::PrivateName:
    case TokenType::Number:
    case TokenType::String:
    case TokenType::Regex:
    case TokenType::Template:
    case TokenType::TemplateTail:
    case TokenType::RightParen:
    case TokenType::RightBracket:
    case TokenType::RightBrace:
        return false;
    default:
        return true;
    }
}

std::vector<std::string> tokensOf(std::string_view source)
{
    using detail::TokenType;
    detail::Scanner scanner(source);
    std::vector<std::string> tokens;
    std::vector<bool> templates;
    detail::Token previous;
    previous.type = TokenType::Semicolon;
    for (;;) {
        scanner.next();
        detail::Token token = scanner.token();
        if (token.type == TokenType::Eof) {
            break;
        }
        REQUIRE(token.type != TokenType::Invalid);
        if ((token.type == TokenType::Slash || token.type == TokenType::SlashAssign) && regexAllowedAfter(previous)) {
            scanner.rescanRegex();
            token = scanner.token();
        } else if (token.type == TokenType::LeftBrace) {
            templates.push_back(false);
        } else if (token.type == TokenType::TemplateHead) {
            templates.push_back(true);
        } else if (token.type == TokenType::RightBrace && !templates.empty()) {
            if (templates.back()) {
                scanner.rescanTemplateContinuation();
                token = scanner.token();
                if (token.type == TokenType::TemplateTail) {
                    templates.pop_back();
                }
            } else {
                templates.pop_back();
            }
        }
        tokens.emplace_back(scanner.text(token));
        previous = token;
    }
    return tokens;
}

constexpr std::string_view kGameScript = R"ts(import type { Device, Frame, Point } from "gs:device";
import { tap, swipe, type Region, wait } from "gs:input";
import { findTemplate } from "gs:vision";

interface Target {
    name: string;
    region?: Region;
    threshold: number;
}

type State = "idle" | "fighting" | "looting";
type Handler<T> = (frame: Frame, target: T) => Promise<boolean>;

declare const settings: { loops: number; delayMs: number };

const targets: Target[] = [
    { name: "start", threshold: 0.9 },
    { name: "skill", region: { x: 0.6, y: 0.7, w: 0.4, h: 0.3 }, threshold: 0.85 },
] satisfies Target[];

abstract class Routine<T extends Target = Target> {
    protected readonly label: string;
    private attempts = 0;
    static created: number = 0;
    abstract step(frame: Frame): Promise<State>;

    constructor(label: string) {
        this.label = label;
        Routine.created++;
    }

    protected async find(frame: Frame, target: T): Promise<Point | undefined> {
        const match = await findTemplate(frame, target.name, { region: target.region, threshold: target.threshold });
        return match?.center as Point | undefined;
    }

    get tries(): number {
        return this.attempts;
    }

    protected count(this: Routine<T>, by: number = 1): void {
        this.attempts += by;
    }
}

class Battle extends Routine<Target> implements Disposable {
    #state: State = "idle";
    private handlers = new Map<State, Handler<Target>>();

    override async step(frame: Frame): Promise<State> {
        const start = targets.find((t): t is Target => t.name === "start")!;
        const point = await this.find(frame, start);
        if (point) {
            await tap(point.x, point.y);
            this.#state = "fighting";
        } else if (this.tries > 3) {
            this.#state = <State>"looting";
        }
        this.count();
        return this.#state;
    }

    [Symbol.dispose](): void {
        this.handlers.clear();
    }
}

function clamp(value: number, min: number, max: number): number;
function clamp(value: number, min = 0, max = 1): number {
    return Math.min(Math.max(value, min), max);
}

const distance = (a: Point, b: Point): number => Math.hypot(a.x - b.x, a.y - b.y);

const pick = <T,>(items: readonly T[], index: number): T | undefined => items[index];

export async function main(device: Device): Promise<void> {
    const battle = new Battle("battle");
    for (let loop: number = 0; loop < settings.loops; loop++) {
        const frame = (await device.capture()) as Frame;
        const state: State = await battle.step(frame);
        const label = `loop ${loop + 1}/${settings.loops}: ${state}`;
        console.log(label, /\d+/.test(label) ? "ok" : "bad");
        if (state === "looting") {
            const from = pick<Point>([{ x: 0.5, y: 0.8 }], 0)!;
            await swipe(from, { x: from.x, y: clamp(from.y - 0.3) } as Point, 300);
        }
        await wait(settings.delayMs ?? 100);
    }
    const near = distance({ x: 0, y: 0 }, { x: 0.3, y: 0.4 }) < 1;
    return void near;
}

export type { Target, State };
export default main;
)ts";

constexpr std::string_view kGameScriptJs = R"js(;
import { tap, swipe, wait } from "gs:input";
import { findTemplate } from "gs:vision";
;
;
;
;
const targets = [
    { name: "start", threshold: 0.9 },
    { name: "skill", region: { x: 0.6, y: 0.7, w: 0.4, h: 0.3 }, threshold: 0.85 },
];
class Routine {
    label;
    attempts = 0;
    static created = 0;
    ;
    constructor(label) {
        this.label = label;
        Routine.created++;
    }
    async find(frame, target) {
        const match = await findTemplate(frame, target.name, { region: target.region, threshold: target.threshold });
        return match?.center;
    }
    get tries() {
        return this.attempts;
    }
    count(by = 1) {
        this.attempts += by;
    }
}
class Battle extends Routine {
    #state = "idle";
    handlers = new Map();
    async step(frame) {
        const start = targets.find((t) => t.name === "start");
        const point = await this.find(frame, start);
        if (point) {
            await tap(point.x, point.y);
            this.#state = "fighting";
        } else if (this.tries > 3) {
            this.#state = "looting";
        }
        this.count();
        return this.#state;
    }
    [Symbol.dispose]() {
        this.handlers.clear();
    }
}
;
function clamp(value, min = 0, max = 1) {
    return Math.min(Math.max(value, min), max);
}
const distance = (a, b) => Math.hypot(a.x - b.x, a.y - b.y);
const pick = (items, index) => items[index];
export async function main(device) {
    const battle = new Battle("battle");
    for (let loop = 0; loop < settings.loops; loop++) {
        const frame = (await device.capture());
        const state = await battle.step(frame);
        const label = `loop ${loop + 1}/${settings.loops}: ${state}`;
        console.log(label, /\d+/.test(label) ? "ok" : "bad");
        if (state === "looting") {
            const from = pick([{ x: 0.5, y: 0.8 }], 0);
            await swipe(from, { x: from.x, y: clamp(from.y - 0.3) }, 300);
        }
        await wait(settings.delayMs ?? 100);
    }
    const near = distance({ x: 0, y: 0 }, { x: 0.3, y: 0.4 }) < 1;
    return void near;
}
;
export default main;
)js";

}

TEST_CASE("TS-001 type annotations on variables, parameters and return types are erased")
{
    checkStrip("let x: number = 1;", {{": number"}});
    checkStrip("const a: Array<string> = [], b: Map<string, number[]> = new Map();",
               {{": Array<string>"}, {": Map<string, number[]>"}});
    checkStrip("function f(a: number, b?: string): void {}", {{": number"}, {"?"}, {": string"}, {": void"}});
    checkStrip("function g(...rest: number[]): number { return rest.length; }", {{": number[]"}, {": number"}});
    checkStrip("function h({ a, b }: Options, [c]: [number], d: number = 1) {}",
               {{": Options"}, {": [number]"}, {": number"}});
    checkStrip("let y!: string;", {{"!"}, {": string"}});
    checkStrip("try { f(); } catch (e: unknown) { g(e); }", {{": unknown"}});
    checkStrip("for (let i: number = 0; i < n; i++) {}", {{": number"}});
    checkStrip("let y: /* gone */ number = 2; // kept", {{": /* gone */ number"}});
    checkStrip("let multi:\n  | \"a\"\n  | \"b\" = \"a\";", {{":\n  | \"a\"\n  | \"b\""}});
    checkStrip("function first<T>(xs: T[]): T | undefined { return xs[0]; }",
               {{"<T>"}, {": T[]"}, {": T | undefined"}});
    checkStrip("function isText(x: unknown): x is string { return typeof x === \"string\"; }",
               {{": unknown"}, {": x is string"}});
    checkStrip("function check(x: unknown): asserts x is number {}", {{": unknown"}, {": asserts x is number"}});
    checkStrip("function check2(x: unknown): asserts x {}", {{": unknown"}, {": asserts x"}});
    checkStrip("function onClick(this: HTMLElement, event: Event) {}", {{"this: HTMLElement,"}, {": Event"}});
    checkStrip("function only(this: Window) {}", {{"this: Window"}});
    checkStrip("export default async function (a: string) {}", {{": string"}});
}

TEST_CASE("TS-002 arrow functions lose parameter types, return types and type parameters")
{
    checkStrip("const f = (a: number, b: number): number => a + b;", {{": number"}, {": number"}, {": number"}});
    checkStrip("const g = async (x: string): Promise<void> => { await x; };", {{": string"}, {": Promise<void>"}});
    checkStrip("const id = <T,>(x: T): T => x;", {{"<T,>"}, {": T"}, {": T"}});
    checkStrip("const k = <T extends object>(x: T) => x;", {{"<T extends object>"}, {": T"}});
    checkStrip("items.map((item: Item, index): string => `${index}`);", {{": Item"}, {": string"}});
    checkStrip("const p = ({ a }: { a: number }) => a;", {{": { a: number }"}});
    checkStrip("const q = (a?: number) => a;", {{"?"}, {": number"}});
    checkStrip("const r = async <T,>(x: T) => x;", {{"<T,>"}, {": T"}});
    checkStrip("const s = (\n  a: number,\n  b: string,\n) => a;", {{": number"}, {": string"}});
    checkStrip("list.filter((x): x is Item => x !== null);", {{": x is Item"}});
    checkStrip("const v = ok ? (a): string => a : (b) => b;", {{": string"}});
    checkUnchanged("const w = ok ? (a) : b => b;");
    checkStrip("class C { handler = (e: Event): void => { this.go(e); }; }", {{": Event"}, {": void"}});
}

TEST_CASE("TS-003 assertions, satisfies, non-null and type arguments are erased")
{
    checkStrip("const a = b as string;", {{"as string"}});
    checkStrip("const c = [1, 2] as const;", {{"as const"}});
    checkStrip("const d = { x: 1 } satisfies Point;", {{"satisfies Point"}});
    checkStrip("const e = value as unknown as Target;", {{"as unknown"}, {"as Target"}});
    checkStrip("const n = maybe!.length + list[0]!;", {{"!"}, {"!"}});
    checkStrip("const m = <number>input;", {{"<number>"}});
    checkStrip("element!.focus();", {{"!"}});
    checkStrip("if (a! && b!) {}", {{"!"}, {"!"}});
    checkStrip("const s = new Set<string>();", {{"<string>"}});
    checkStrip("const v = useValue<number>(1);", {{"<number>"}});
    checkStrip("const w = obj.method<A, B>(x).then<C>(y);", {{"<A, B>"}, {"<C>"}});
    checkStrip("const t = tag<Props>`text ${value}`;", {{"<Props>"}});
    checkStrip("const make = factory<Widget>;", {{"<Widget>"}});
    checkStrip("const z = new Map<string, Array<number>>;", {{"<string, Array<number>>"}});
    checkStrip("a?.b!.c<number>();", {{"!"}, {"<number>"}});
    checkStrip("f(a<b>(c));", {{"<b>"}});
    checkStrip("const frame = (await device.capture()) as Frame;", {{"as Frame"}});
}

TEST_CASE("TS-004 comparisons and shifts that are not type arguments stay")
{
    checkUnchanged("if (a < b && c > d) { x = a < b; }");
    checkUnchanged("const r = a < b > c;");
    checkUnchanged("for (let i = 0; i < n; i++) { total += i >> 1; total >>>= 2; flag = i >= 3; }");
    checkUnchanged("const shifted = a << 2, cmp = (a < b) === (c > d);");
    checkUnchanged("const h = (a / b) * (c / d), g = x < y || z > (w);");
}

TEST_CASE("TS-005 interfaces, type aliases and declarations disappear and leave a semicolon")
{
    checkStrip("interface Point { x: number; y: number }\nlet p = 1;",
               {{"interface Point { x: number; y: number }", ';'}});
    checkStrip("type Id = string | number;\nlet id = 1;", {{"type Id = string | number;", ';'}});
    checkStrip("export type Pair<A, B = A> = [first: A, second?: B, ...rest: A[]];",
               {{"export type Pair<A, B = A> = [first: A, second?: B, ...rest: A[]];", ';'}});
    checkStrip("type Handler = (event: Event, ...args: any[]) => void\nhandle()",
               {{"type Handler = (event: Event, ...args: any[]) => void", ';'}});
    checkStrip("type Ctor = new (...args: any[]) => object;\n"
               "type Abstract = abstract new () => object;\n"
               "type Key = keyof typeof table;\n"
               "type Sym = { readonly id: unique symbol };\n"
               "type Elem<T> = T extends (infer U)[] ? U : never;\n"
               "type Ret<T> = T extends (...args: any[]) => infer R extends string ? R : never;\n"
               "type Mapped<T> = { readonly [K in keyof T as `get${Capitalize<K & string>}`]-?: T[K] };\n"
               "type Tpl = `${number}px` | `${number}%`;\n"
               "type Lookup = Table[\"rows\"][number];\n"
               "type Fn = { (x: number): string; new (x: string): Fn; [key: string]: unknown; m?<T>(a: T): void };\n"
               "type Imp = import(\"./types\").Config<string>;\n"
               "type Neg = -1 | 1n;\n"
               "type Cond<T> = T extends string ? \"s\" : T extends number ? \"n\" : never;\n"
               "type Get = { get x(): number; set x(v: number) };\n"
               "type Fn2 = <T>(x: T) => T;\n",
               {{"type Ctor = new (...args: any[]) => object;", ';'},
                {"type Abstract = abstract new () => object;", ';'},
                {"type Key = keyof typeof table;", ';'},
                {"type Sym = { readonly id: unique symbol };", ';'},
                {"type Elem<T> = T extends (infer U)[] ? U : never;", ';'},
                {"type Ret<T> = T extends (...args: any[]) => infer R extends string ? R : never;", ';'},
                {"type Mapped<T> = { readonly [K in keyof T as `get${Capitalize<K & string>}`]-?: T[K] };", ';'},
                {"type Tpl = `${number}px` | `${number}%`;", ';'},
                {"type Lookup = Table[\"rows\"][number];", ';'},
                {"type Fn = { (x: number): string; new (x: string): Fn; [key: string]: unknown; m?<T>(a: T): void };",
                 ';'},
                {"type Imp = import(\"./types\").Config<string>;", ';'},
                {"type Neg = -1 | 1n;", ';'},
                {"type Cond<T> = T extends string ? \"s\" : T extends number ? \"n\" : never;", ';'},
                {"type Get = { get x(): number; set x(v: number) };", ';'},
                {"type Fn2 = <T>(x: T) => T;", ';'}});
    checkStrip("declare const VERSION: string;\n"
               "declare function log(message: string): void;\n"
               "declare class Native { call(): void; value: number }\n"
               "declare module \"engine\" { export function tap(x: number, y: number): void; }\n"
               "declare global { interface Window { gs: unknown } }\n"
               "declare namespace Api { const version: string }\n"
               "declare enum Mode { Fast, Slow }\n"
               "declare let ready: boolean, count: number;\n"
               "export declare const exported: number;\n",
               {{"declare const VERSION: string;", ';'},
                {"declare function log(message: string): void;", ';'},
                {"declare class Native { call(): void; value: number }", ';'},
                {"declare module \"engine\" { export function tap(x: number, y: number): void; }", ';'},
                {"declare global { interface Window { gs: unknown } }", ';'},
                {"declare namespace Api { const version: string }", ';'},
                {"declare enum Mode { Fast, Slow }", ';'},
                {"declare let ready: boolean, count: number;", ';'},
                {"export declare const exported: number;", ';'}});
    checkStrip("namespace Types {\n  export interface A { x: number }\n  export type B = string;\n}\nlet t = 1;",
               {{"namespace Types {\n  export interface A { x: number }\n  export type B = string;\n}", ';'}});
    checkStrip("module Shapes.Inner { type C = number }", {{"module Shapes.Inner { type C = number }", ';'}});
    checkStrip("function pick(a: string): string;\nfunction pick(a: number): number;\nfunction pick(a: any) { return a; }",
               {{"function pick(a: string): string;", ';'}, {"function pick(a: number): number;", ';'}, {": any"}});
    checkStrip("export function over(a: string): void;\nexport function over(a: any) {}",
               {{"export function over(a: string): void;", ';'}, {": any"}});
    checkStrip("export interface Shape { area(): number }\nexport default interface Config { debug: boolean }\n",
               {{"export interface Shape { area(): number }", ';'},
                {"export default interface Config { debug: boolean }", ';'}});
    checkStrip("export as namespace Lib;", {{"export as namespace Lib;", ';'}});
    checkUnchanged("let type = 1, interface2 = type;\ntype = 2;\nlet declare = 3; declare = 4;");
}

TEST_CASE("TS-006 classes lose modifiers, type-only members, overloads and heritage types")
{
    checkStrip("abstract class Shape<T> extends Base<T> implements Drawable, Sized<T> {\n"
               "  private readonly id: number = 1;\n"
               "  protected name?: string;\n"
               "  public static count = 0;\n"
               "  declare cache: Map<string, T>;\n"
               "  [key: string]: unknown;\n"
               "  override accessor size!: number;\n"
               "  abstract area(): number;\n"
               "  get label(): string { return this.name ?? \"\"; }\n"
               "  set label(value: string) { this.name = value; }\n"
               "  constructor(id: number) { super(); this.id = id; }\n"
               "  move(dx: number): void;\n"
               "  move(dx: number, dy?: number): void {}\n"
               "  #secret: number = 0;\n"
               "  static { Shape.count++; }\n"
               "}\n",
               {{"abstract"},
                {"<T>"},
                {"<T>"},
                {"implements Drawable, Sized<T>"},
                {"private"},
                {"readonly"},
                {": number"},
                {"protected"},
                {"?"},
                {": string"},
                {"public"},
                {"declare cache: Map<string, T>;", ';'},
                {"[key: string]: unknown;", ';'},
                {"override"},
                {"accessor"},
                {"!"},
                {": number"},
                {"abstract area(): number;", ';'},
                {": string"},
                {": string"},
                {": number"},
                {"move(dx: number): void;", ';'},
                {": number"},
                {"?"},
                {": number"},
                {": void"},
                {": number"}});
    checkStrip("const Klass = class<T> implements I { x?: T };", {{"<T>"}, {"implements I"}, {"?"}, {": T"}});
    checkStrip("export abstract class Base {}\nexport default abstract class Other {}", {{"abstract"}, {"abstract"}});
    checkStrip("class Opt { m?(): void; m?() {} }", {{"m?(): void;", ';'}, {"?"}});
    checkStrip("const o = { m(a: number): void {}, get v(): number { return 1; }, async *g<T>(x: T) {} };",
               {{": number"}, {": void"}, {": number"}, {"<T>"}, {": T"}});
    checkUnchanged("class Plain { static async *gen() {} get() {} set = 1; static() {} readonly; }");
}

TEST_CASE("TS-007 type-only imports and exports are erased and the rest stays valid")
{
    checkStrip("import type { A, B } from \"./types\";\nimport type Def from \"./def\";\nimport type * as NS from \"./ns\";\n",
               {{"import type { A, B } from \"./types\";", ';'},
                {"import type Def from \"./def\";", ';'},
                {"import type * as NS from \"./ns\";", ';'}});
    checkStrip("import { type A, b, type C as D } from \"./m\";", {{"type A,"}, {", type C as D"}});
    checkStrip("import def, { type T } from \"./m\";", {{"type T"}});
    checkUnchanged("import { type } from \"./m\";");
    checkUnchanged("import type from \"./m\";");
    checkUnchanged("import { type as alias } from \"./m\";");
    checkStrip("export type { A } from \"./a\";\nexport type * from \"./b\";\nexport type * as C from \"./c\";\n",
               {{"export type { A } from \"./a\";", ';'},
                {"export type * from \"./b\";", ';'},
                {"export type * as C from \"./c\";", ';'}});
    checkStrip("export { type A, b };", {{"type A,"}});
    checkUnchanged("import data from \"./data.json\" with { type: \"json\" };\nexport * from \"./m\";\nexport * as ns from \"./n\";");
    checkStrip("import(\"./lazy\").then((m: Module) => m.run());", {{": Module"}});
    checkUnchanged("const meta = import.meta.url;");
}

TEST_CASE("TS-008 erasing never changes what the remaining JavaScript means")
{
    checkStrip("const f = (a: number)\n  : number => a;", {{": number"}, {")"}, {": number", ')'}});
    checkStrip("const g = (a: number): {\n  x: number\n} => ({ x: a });", {{": number"}, {")"}, {": {\n  x: number\n}", ')'}});
    checkStrip("const h = async <T,>\n(x: T) => x;", {{"<T,>", ' ', '('}, {"("}, {": T"}});
    checkStrip("function make() {\n  return <T,>\n  (x: T) => x;\n}", {{"<T,>", ' ', '('}, {"("}, {": T"}});
    checkStrip("let a = b as string\n(c as any).run()", {{"as string", ';'}, {"as any"}});
    checkStrip("value as Foo\n[1, 2].forEach(run)", {{"as Foo", ';'}});
    checkStrip("return_ = x!\n`tpl`", {{"!"}});
    checkStrip("let a = b\ntype T = string\n(c)", {{"type T = string", ';'}});
    checkStrip("let a = b\ninterface I {}\n[1].map(f)", {{"interface I {}", ';'}});
    checkStrip("class K {\n  x: number\n  [k: string]: any\n  [y]() {}\n}", {{": number", ';'}, {"[k: string]: any", ';'}});
    checkProblem("class L {\n  x = 1\n  [k: string]: any\n}", ProblemCode::Syntax, 3, 5);
    checkStrip("x!\n[1, 2].forEach(f)", {{"!"}});
    checkProblem("function f() {\n  return <number>\n    value;\n}", ProblemCode::UnsafeAssertion, 2, 10);
}

TEST_CASE("TS-009 JavaScript lexing is complete and plain JavaScript passes through unchanged")
{
    checkUnchanged("const re = /ab+c/gi.test(s) ? a / b / c : x;");
    checkUnchanged("const t = `a ${`b ${c}`} ${{ d: 1 }.d}`;");
    checkUnchanged("#!/usr/bin/env node\nconst n = 1_000n + 0x1F + .5e-3;");
    checkUnchanged("class A { #x = 1; static has(o) { return #x in o; } }");
    checkUnchanged("a ??= b?.c?.[d]?.(e) ?? f;");
    checkUnchanged("const \\u0061bc = 1;");
    checkUnchanged("x = y\n/re/g.exec(z)");
    checkUnchanged("if (ok) /x/.test(s);");
    checkUnchanged("const fn = function* () { yield 1; yield* other(); };");
    checkUnchanged("outer: for (const x of xs) { switch (x) { case 1: continue outer; default: break; } }");
    checkUnchanged("for await (const chunk of stream) { total += chunk.length; }");
    checkUnchanged("for (const key in object) if (key in other) count++;");
    checkUnchanged("do x++; while (x < 10)\nlet after = 1;");
    checkUnchanged("const { a, b: [c, d = 2], ...rest } = obj; [x, y] = [y, x];");
    checkUnchanged("new Foo;\nnew Foo.Bar(1).baz();\nnew.target;");
    checkUnchanged("let x /* keep */ = 1; // tail");
    checkUnchanged("label: {\n  break label;\n}");
    checkUnchanged("const big = 2n ** 64n, neg = -x, inv = ~y, not = !z, typ = typeof w, v = void 0;");
}

TEST_CASE("TS-010 non-erasable syntax is reported with code, line and column")
{
    checkProblem("enum Color { Red, Green }", ProblemCode::Enum, 1, 1);
    checkProblem("let a = 1;\nconst enum E { A = 1 }", ProblemCode::ConstEnum, 2, 1);
    checkProblem("namespace App {\n  export const version = 1;\n}", ProblemCode::Namespace, 1, 1);
    checkProblem("class P {\n  constructor(private readonly x: number) {}\n}", ProblemCode::ParameterProperty, 2, 15);
    checkProblem("import fs = require(\"fs\");", ProblemCode::ImportRequire, 1, 1);
    checkProblem("import Alias = Some.Name;", ProblemCode::ImportAlias, 1, 1);
    checkProblem("export = main;", ProblemCode::ExportAssignment, 1, 1);
    checkProblem("@sealed\nclass A {}", ProblemCode::Decorator, 1, 1);
    checkProblem("class B {\n  @log method() {}\n}", ProblemCode::Decorator, 2, 3);
    checkProblem("const view = <div className=\"x\">hi</div>;", ProblemCode::Jsx, 1, 14);
    checkProblem("let x = (1 + ;", ProblemCode::Syntax, 1, 14);
    checkProblem("let s = \"abc", ProblemCode::Syntax, 1, 9);
    checkProblem("const \xE5\x90\x8D = 1; enum E {}", ProblemCode::Enum, 1, 14);
    checkProblem("const s = \"\xF0\x9F\x98\x80\"; enum E {}", ProblemCode::Enum, 1, 17);

    const Result mixed = strip("enum E { A }\nlet x: number = 1;");
    REQUIRE(mixed.problems.size() == 1);
    CHECK(mixed.problems[0].code == ProblemCode::Enum);
    CHECK(mixed.javascript == "enum E { A }\nlet x" + std::string(9, ' ') + "= 1;");

    CHECK(codeName(ProblemCode::Enum) == "GS0001");
    CHECK(codeName(ProblemCode::UnsafeAssertion) == "GS0010");
    CHECK(codeName(ProblemCode::Syntax) == "GS0100");
}

TEST_CASE("TS-011 erased characters keep UTF-16 columns and line separators")
{
    const Result chinese = strip("let s: \"\xE4\xB8\xAD\xE6\x96\x87\" = \"\xE4\xB8\xAD\xE6\x96\x87\";");
    CHECK(chinese.problems.empty());
    CHECK(chinese.javascript == "let s" + std::string(6, ' ') + " = \"\xE4\xB8\xAD\xE6\x96\x87\";");

    const Result emoji = strip("let e: \"\xF0\x9F\x98\x80\" = 1;");
    CHECK(emoji.problems.empty());
    CHECK(emoji.javascript == "let e" + std::string(6, ' ') + " = 1;");

    const Result separator = strip("let t: /* \xE2\x80\xA8 */ number = 1; enum E {}");
    CHECK(separator.javascript.find("\xE2\x80\xA8") != std::string::npos);
    REQUIRE(separator.problems.size() == 1);
    CHECK(separator.problems[0].line == 2);
}

TEST_CASE("TS-012 a realistic game script strips to the hand-written JavaScript token by token")
{
    const Result result = strip(kGameScript);
    CHECK_MESSAGE(result.problems.empty(), describe(result.problems));
    checkPositions(kGameScript, result.javascript);
    const std::vector<std::string> stripped = tokensOf(result.javascript);
    const std::vector<std::string> expected = tokensOf(kGameScriptJs);
    const std::size_t common = std::min(stripped.size(), expected.size());
    for (std::size_t i = 0; i < common; ++i) {
        if (stripped[i] != expected[i]) {
            CAPTURE(i);
            CHECK(stripped[i] == expected[i]);
            break;
        }
    }
    CHECK(stripped.size() == expected.size());
}

TEST_CASE("TS-013 deep nesting is reported instead of overflowing the stack")
{
    const std::string deep = std::string(20000, '(') + "1" + std::string(20000, ')') + ";";
    const Result result = strip(deep);
    REQUIRE(!result.problems.empty());
    CHECK(result.problems[0].code == ProblemCode::Syntax);
    CHECK(result.javascript == deep);
}

TEST_CASE("TS-014 1500 lines strip in under 2 ms in optimised builds")
{
    std::string source;
    std::size_t lines = 0;
    while (lines < 1500) {
        source += kGameScript;
        lines += static_cast<std::size_t>(std::count(kGameScript.begin(), kGameScript.end(), '\n'));
    }
    std::vector<double> samples;
    for (int run = 0; run < kRuns; ++run) {
        const auto start = std::chrono::steady_clock::now();
        const Result result = strip(source);
        const auto stop = std::chrono::steady_clock::now();
        REQUIRE(result.problems.empty());
        samples.push_back(std::chrono::duration<double, std::milli>(stop - start).count());
    }
    std::sort(samples.begin(), samples.end());
    const double median = samples[samples.size() / 2];
    MESSAGE(std::string(kOptimised ? "optimised build: " : "debug build, not checked: ") << lines << " lines in " << median
                                                                                 << " ms (median of " << kRuns << ")");
    if (kOptimised) {
        CHECK(median < 2.0);
    }
}

TEST_CASE("TS-015 the byte option erases every byte of a Chinese type to one space, so offsets stay put")
{
    const std::string source = "let \xE5\x80\xBC: \xE7\xB1\xBB\xE5\x9E\x8B = 1;\nconst list = [] as \xE5\x88\x97\xE8\xA1\xA8[];\n";
    const Result bytes = strip(source, Offsets::Bytes);
    CHECK_MESSAGE(bytes.problems.empty(), describe(bytes.problems));
    CHECK(bytes.javascript.size() == source.size());
    CHECK(bytes.javascript
          == "let \xE5\x80\xBC" + std::string(8, ' ') + " = 1;\nconst list = []" + std::string(12, ' ') + ";\n");
    checkPositions(source, bytes.javascript);

    const Result units = strip(source);
    CHECK(units.problems.empty());
    CHECK(units.javascript == "let \xE5\x80\xBC" + std::string(4, ' ') + " = 1;\nconst list = []" + std::string(8, ' ') + ";\n");
}

TEST_CASE("TS-016 the byte option puts an inserted semicolon on the last erased byte of a Chinese type")
{
    const std::string source = "const x = y as \xE7\xB1\xBB\xE5\x9E\x8B\n(z)\n";
    const Result bytes = strip(source, Offsets::Bytes);
    CHECK_MESSAGE(bytes.problems.empty(), describe(bytes.problems));
    REQUIRE(bytes.javascript.size() == source.size());
    CHECK(bytes.javascript == "const x = y" + std::string(9, ' ') + ";\n(z)\n");

    const std::string emoji = "let e: \"\xF0\x9F\x98\x80\" = 1; type \xE5\x90\x8D = string;\nlet n = 2;";
    const Result wide = strip(emoji, Offsets::Bytes);
    CHECK(wide.problems.empty());
    CHECK(wide.javascript == "let e" + std::string(8, ' ') + " = 1;" + std::string(18, ' ') + ";\nlet n = 2;");
}

TEST_CASE("TS-017 the byte option reports problems at the same places and keeps line breaks inside erased types")
{
    const std::string source = "let t: {\n  \xE5\x90\x8D: string\n} = { \xE5\x90\x8D: \"a\" };\nenum E { A }\n";
    const Result bytes = strip(source, Offsets::Bytes);
    const Result units = strip(source);
    CHECK(bytes.javascript.size() == source.size());
    CHECK(std::count(bytes.javascript.begin(), bytes.javascript.end(), '\n') == 4);
    REQUIRE(bytes.problems.size() == 1);
    CHECK(bytes.problems == units.problems);
    CHECK(bytes.problems[0].line == 4);
}
