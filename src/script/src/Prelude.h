#pragma once

#include <array>
#include <string>
#include <string_view>

namespace gs::script::detail {

inline constexpr std::string_view kPreludeName = "<gs>";

inline constexpr std::array<std::string_view, 16> kPreludeParts{
    R"js(((host) => {
    "use strict";
    const defineProperty = Object.defineProperty;
    const freeze = Object.freeze;
    const refuse = () => {
        throw new EvalError("Code generation from strings is disabled");
    };
    const samples = [function () {}, async function () {}, function* () {}, async function* () {}];
    for (const sample of samples) {
        const prototype = Object.getPrototypeOf(sample);
        const original = prototype.constructor;
        const tamed = function () {
            refuse();
        };
        defineProperty(tamed, "name", { value: original.name });
        defineProperty(tamed, "length", { value: 1 });
        defineProperty(tamed, "prototype", { value: prototype, writable: false });
        defineProperty(prototype, "constructor", { value: tamed });
        if (original === globalThis.Function) {
            defineProperty(globalThis, "Function", { value: tamed });
        }
    }
    delete globalThis.eval;
    const locked = { writable: false, configurable: false };
    defineProperty(Error, "prepareStackTrace", { ...locked, value: undefined });
    defineProperty(Error, "stackTraceLimit", { ...locked, value: 10 });
    const replaceable = { writable: true, configurable: true };

    const RealDate = Date;
    const wallNow = host.wallNow;
    const HostDate = function Date(...parts) {
        if (new.target === undefined) {
            return new RealDate(wallNow()).toString();
        }
        return Reflect.construct(RealDate, parts.length === 0 ? [wallNow()] : parts, new.target);
    };
    defineProperty(HostDate, "length", { value: 7 });
    defineProperty(HostDate, "prototype", { value: RealDate.prototype });
    defineProperty(HostDate, "now", { ...replaceable, value: function now() { return wallNow(); } });
    defineProperty(HostDate, "parse", { ...replaceable, value: RealDate.parse });
    defineProperty(HostDate, "UTC", { ...replaceable, value: RealDate.UTC });
    defineProperty(RealDate.prototype, "constructor", { ...replaceable, value: HostDate });
    defineProperty(globalThis, "Date", { ...replaceable, value: HostDate });
    defineProperty(Math, "random", { ...replaceable, value: host.random });

    const secret = Symbol("AbortSignal");
    const states = new WeakMap();
    const stateOf = (signal) => {
        const state = states.get(signal);
        if (state === undefined) {
            throw new TypeError("Illegal invocation");
        }
        return state;
    };
    class AbortSignal {
        constructor(key) {
            if (key !== secret) {
                throw new TypeError("Illegal constructor");
            }
            states.set(this, { aborted: false, reason: undefined, listeners: [] });
            this.onabort = null;
        }
        get aborted() {
            return stateOf(this).aborted;
        }
        get reason() {
            return stateOf(this).reason;
        }
        throwIfAborted() {
            const state = stateOf(this);
            if (state.aborted) {
                throw state.reason;
            }
        }
        addEventListener(type, listener) {
            const state = stateOf(this);
            if (type !== "abort" || listener === null || listener === undefined || state.aborted) {
                return;
            }
            if (!state.listeners.includes(listener)) {
                state.listeners.push(listener);
            }
        }
        removeEventListener(type, listener) {
            const state = stateOf(this);
            if (type === "abort") {
                state.listeners = state.listeners.filter((entry) => entry !== listener);
            }
        }
    }
    defineProperty(AbortSignal.prototype, Symbol.toStringTag, { value: "AbortSignal" });
    defineProperty(globalThis, "AbortSignal", { ...replaceable, value: AbortSignal });

    const fire = (signal, reason) => {
        const state = stateOf(signal);
        if (state.aborted) {
            return state.reason;
        }
        state.aborted = true;
        state.reason = reason;
        const handlers = state.listeners;
        state.listeners = [];
        if (typeof signal.onabort === "function") {
            handlers.push(signal.onabort);
        }
        const event = freeze({ type: "abort", target: signal });
        for (const handler of handlers) {
            try {
                if (typeof handler === "function") {
                    handler.call(signal, event);
                } else if (typeof handler.handleEvent === "function") {
                    handler.handleEvent(event);
                }
            } catch (error) {
                Promise.reject(error);
            }
        }
        return reason;
    };
    const cancelled = () => new DOMException("The task was cancelled", "AbortError");
    const now = host.now;
    const nextFrame = host.nextFrame;

    const rules = ["ignore", "restart", "stop", "queue", "parallel"];
    const lists = new Map();
    const ownedByTask = new Map();
    const listenerOfTask = new Map();
    const disposeKey = typeof Symbol.dispose === "symbol" ? Symbol.dispose : undefined;
    const own = (dispose) => {
        const owner = host.currentTask();
        if (owner === 0) {
            return;
        }
        let owned = ownedByTask.get(owner);
        if (owned === undefined) {
            owned = new Set();
            ownedByTask.set(owner, owned);
        }
        owned.add(dispose);
    };
    const disposable = (dispose) => {
        const handle = { dispose };
        if (disposeKey !== undefined) {
            handle[disposeKey] = dispose;
        }
        return freeze(handle);
    };
    const launch = (listener, extra) => {
        const task = host.spawn(listener.handler, listener.label, extra);
        if (task !== 0) {
            listener.running.add(task);
            listenerOfTask.set(task, listener);
        }
    };
    const stopAll = (listener) => {
        listener.queued.length = 0;
        for (const task of [...listener.running]) {
            host.cancel(task);
        }
    };
    const react = (listener, extra) => {
        if (!listener.active) {
            return;
        }
        if (listener.again === "parallel" || listener.running.size === 0) {
            launch(listener, extra);
        } else if (listener.again === "restart") {
            stopAll(listener);
            launch(listener, extra);
        } else if (listener.again === "stop") {
            stopAll(listener);
        } else if (listener.again === "queue") {
            listener.queued.push(extra);
        }
    };
    const reactAll = (key, extra) => {
        const list = lists.get(key);
        if (list !== undefined) {
            for (const listener of [...list]) {
                react(listener, extra);
            }
        }
    };
    const listen = (kind, id, handler, options, label, released) => {
        if (typeof handler !== "function") {
            throw new TypeError("GS-API-008 the handler must be a function");
        }
        const again = options?.again ?? "ignore";
        if (!rules.includes(again)) {
            throw new RangeError("GS-API-009 again must be one of ignore, restart, stop, queue, parallel");
        }
        const key = kind + ":" + id;
        let list = lists.get(key);
        if (list === undefined) {
            list = new Set();
            lists.set(key, list);
        }
        const listener = { handler, again, label, running: new Set(), queued: [], active: true };
        list.add(listener);
        const dispose = () => {
            if (!listener.active) {
                return;
            }
            listener.active = false;
            list.delete(listener);
            if (kind === "held") {
                stopAll(listener);
            }
            listener.queued.length = 0;
            released?.();
        };
        own(dispose);
        return disposable(dispose);
    };
    const handedOver = new Map();
    const handOver = (id, on) => {
        const count = (handedOver.get(id) ?? 0) + (on ? 1 : -1);
        if (count > 0) {
            handedOver.set(id, count);
        } else {
            handedOver.delete(id);
        }
        if (count === (on ? 1 : 0)) {
            host.handOver(id, on);
        }
    };
    const ended = (task) => {
        const owned = ownedByTask.get(task);
        if (owned !== undefined) {
            ownedByTask.delete(task);
            for (const dispose of owned) {
                try {
                    dispose();
                } catch (error) {
                    Promise.reject(error);
                }
            }
        }
        const listener = listenerOfTask.get(task);
        if (listener !== undefined) {
            listenerOfTask.delete(task);
            listener.running.delete(task);
            if (listener.active && listener.running.size === 0 && listener.queued.length > 0) {
                launch(listener, listener.queued.shift());
            }
        }
    };

    const bindings = new Map();
    let reading = null;
    const thenable = (value) =>
        value !== null && (typeof value === "object" || typeof value === "function") && typeof value.then === "function";
    const asyncCompute = () =>
        new TypeError("GS-API-032 a binding's compute must return its value at once, not a Promise");
    const enabledOf = (id) => {
        const found = host.controls().find((control) => control.id === id);
        return found !== undefined && found.enabled;
    };
    const evaluate = (binding) => {
        const outer = reading;
        const reads = new Map();
        reading = reads;
        binding.busy = true;
        try {
            const value = binding.compute();
            binding.reads = reads;
            return value;
        } catch (error) {
            for (const [id, value] of reads) {
                binding.reads.set(id, value);
            }
            throw error;
        } finally {
            reading = outer;
            binding.busy = false;
        }
    };
    const apply = (binding, value) => {
        const on = Boolean(value);
        if (on !== binding.applied) {
            host.setEnabled(binding.id, on);
            binding.applied = on;
        }
    };
    const unbind = (binding, restore) => {
        if (!binding.active) {
            return;
        }
        binding.active = false;
        bindings.delete(binding.id);
        if (restore && binding.applied !== binding.before) {
            binding.applied = binding.before;
            host.setEnabled(binding.id, binding.before);
        }
    };
    const refresh = (binding) => {
        if (!binding.active || binding.busy) {
            return;
        }
        try {
            const value = evaluate(binding);
            if (thenable(value)) {
                Promise.reject(asyncCompute());
                unbind(binding, true);
                return;
            }
            apply(binding, value);
        } catch (error) {
            Promise.reject(error);
        }
    };
    const changed = (id, value) => {
        for (const binding of [...bindings.values()]) {
            if (binding.reads.has(id) && binding.reads.get(id) !== value) {
                refresh(binding);
            }
        }
    };
    const bindProperty = (id, property, compute) => {
        if (property !== "enabled") {
            throw new RangeError("GS-API-031 only the property \"enabled\" can be bound");
        }
        if (typeof compute !== "function") {
            throw new TypeError("GS-API-008 the compute must be a function");
        }
        const previous = bindings.get(id);
        const binding = {
            id,
            compute,
            reads: new Map(),
            before: previous !== undefined ? previous.before : enabledOf(id),
            applied: false,
            active: true,
            busy: false,
        };
        const value = evaluate(binding);
        if (thenable(value)) {
            throw asyncCompute();
        }
        binding.applied = previous !== undefined ? previous.applied : binding.before;
        apply(binding, value);
        if (previous !== undefined) {
            previous.active = false;
        }
        bindings.set(id, binding);
        const dispose = () => unbind(binding, true);
        own(dispose);
        return disposable(dispose);
    };
    const untilOptions = (options) => {
        if (options === undefined) {
            return { timeoutMs: 5000, retry: 0 };
        }
        if (options === null || typeof options !== "object") {
            throw new TypeError("GS-API-011 the options must be an object");
        }
        const timeoutMs = options.timeoutMs === undefined ? 5000 : Number(options.timeoutMs);
        if (!(timeoutMs >= 1 && timeoutMs <= 600000)) {
            throw new RangeError("GS-API-003 timeoutMs must be from 1 to 600000 milliseconds");
        }
        const retry = options.retry === undefined ? 0 : Number(options.retry);
        if (!Number.isInteger(retry) || retry < 0 || retry > 10) {
            throw new RangeError("GS-API-017 retry must be a whole number from 0 to 10");
        }
        return { timeoutMs, retry };
    };
    const confirm = async (action, run, condition, { timeoutMs, retry }, site) => {
        let pending = action;
        for (let tries = 1; ; tries += 1) {
            await pending;
            const deadline = now() + timeoutMs;
            for (;;) {
                const value = await condition();
                if (value) {
                    return value;
                }
                if (now() >= deadline) {
                    break;
                }
                await nextFrame();
            }
            if (tries > retry) {
                const error = new DOMException("GS-API-030 the condition did not come true within " + timeoutMs
                    + " ms of the action (tried " + (tries === 1 ? "once" : tries + " times") + ")", "TimeoutError");
                error.stack = site;
                throw error;
            }
            pending = run();
        }
    };
    const act = (run) => {
        const start = () => (host.connected() ? run() : host.awaitConnection().then(run));
        const action = start();
        defineProperty(action, "until", {
)js",
    R"js(            value: function until(condition, options) {
                if (typeof condition !== "function") {
                    throw new TypeError("GS-API-008 the condition must be a function");
                }
                return confirm(action, start, condition, untilOptions(options), new Error().stack);
            },
        });
        return action;
    };
    const actionOf = (native) => {
        const wrapped = {
            [native.name](...parts) {
                return act(() => native(...parts));
            },
        }[native.name];
        defineProperty(wrapped, "length", { value: native.length });
        return wrapped;
    };

    const makeListener = (handler, options, label) => {
        if (typeof handler !== "function") {
            throw new TypeError("GS-API-008 the handler must be a function");
        }
        const again = options?.again ?? "ignore";
        if (!rules.includes(again)) {
            throw new RangeError("GS-API-009 again must be one of ignore, restart, stop, queue, parallel");
        }
        return { handler, again, label, running: new Set(), queued: [], active: true };
    };
    const lastStatus = new Map();
    const pressedSince = new Map();
    const idle = freeze({ touching: false, stick: freeze({ x: 0, y: 0 }), aiming: false, walking: false, cancelled: false,
        cell: -1 });
    const statusOf = (id) => host.status(id) ?? idle;
    const moving = (stick) => stick.x !== 0 || stick.y !== 0;
    const degreesOf = (stick) => {
        if (!moving(stick)) {
            return null;
        }
        const angle = (Math.atan2(stick.x, -stick.y) * 180) / Math.PI;
        return Math.round(((angle % 360) + 360) % 360);
    };
    const keyState = new Map();
    const keyEntry = (name) => {
        let entry = keyState.get(name);
        if (entry === undefined) {
            entry = { down: false, pressedAt: null, releasedAt: null, tapAt: null, serial: 0, timers: new Set(), longs: [] };
            keyState.set(name, entry);
        }
        return entry;
    };
    const tapLimitMs = 250;
    const doubleGapMs = 300;
    const onStatus = (id, status) => {
        const before = lastStatus.get(id) ?? idle;
        lastStatus.set(id, status);
        const extra = { control: id };
        const edge = (was, is, start, end) => {
            if (!was && is) {
                reactAll(start + ":" + id, extra);
            } else if (was && !is) {
                reactAll(end + ":" + id, extra);
            }
        };
        edge(before.touching, status.touching, "touchStart", "touchEnd");
        edge(before.walking, status.walking, "walkStart", "walkEnd");
        if (before.touching && !status.touching && (before.cell ?? -1) >= 0) {
            reactAll("choose:" + id, { ...extra, cell: before.cell });
        }
        edge(moving(before.stick), moving(status.stick), "moveStart", "moveEnd");
        if (!before.aiming && status.aiming) {
            reactAll("aimStart:" + id, extra);
        } else if (before.aiming && !status.aiming) {
            reactAll((status.cancelled ? "cancel:" : "cast:") + id, extra);
        }
        const direction = degreesOf(status.stick);
        if (direction !== null && direction !== degreesOf(before.stick)) {
            reactAll("direction:" + id, { ...extra, direction });
        }
    };
    const onKey = (name, down) => {
        const entry = keyEntry(name);
        if (entry.down === down) {
            return;
        }
        const at = now();
        const extra = { key: name };
        entry.down = down;
        if (down) {
            entry.serial += 1;
            const doubled = entry.tapAt !== null && at - entry.tapAt <= doubleGapMs;
            entry.pressedAt = at;
            reactAll("keyPress:" + name, extra);
            reactAll("keyHeld:" + name, extra);
            if (doubled) {
                entry.tapAt = null;
                reactAll("keyDouble:" + name, extra);
            }
            const serial = entry.serial;
            for (const long of entry.longs) {
                if (!long.listener.active) {
                    continue;
                }
                const timer = host.spawn(async () => {
                    await host.sleep(long.ms);
                    if (entry.down && entry.serial === serial) {
                        react(long.listener, extra);
                    }
                }, name + ".onLongPress", {});
                if (timer !== 0) {
                    entry.timers.add(timer);
                }
            }
            return;
        }
        for (const timer of entry.timers) {
            host.cancel(timer);
        }
        entry.timers.clear();
        const held = lists.get("keyHeld:" + name);
        if (held !== undefined) {
            for (const listener of [...held]) {
                stopAll(listener);
            }
        }
        const heldFor = entry.pressedAt === null ? 0 : at - entry.pressedAt;
        entry.releasedAt = at;
        entry.pressedAt = null;
        reactAll("keyRelease:" + name, { ...extra, heldMs: heldFor });
        if (heldFor <= tapLimitMs) {
            entry.tapAt = at;
            reactAll("keyTap:" + name, extra);
        }
    };
    const profileTables = { rebuild: () => {} };
    const extensionTables = { rebuild: () => {} };
    const serviceEvents = new Map();
    const dispatch = (kind, id, value) => {
        if (kind === "control") {
            const extra = { control: id };
            if (value) {
                pressedSince.set(id, now());
                reactAll("press:" + id, extra);
                reactAll("held:" + id, extra);
                return;
            }
            pressedSince.delete(id);
            const held = lists.get("held:" + id);
            if (held !== undefined) {
                for (const listener of [...held]) {
                    stopAll(listener);
                }
            }
            reactAll("release:" + id, extra);
        } else if (kind === "state") {
            changed(id, value);
            reactAll("state:" + id, { id, value });
        } else if (kind === "enabled") {
            reactAll((value ? "enabled:" : "disabled:") + id, { control: id });
        } else if (kind === "status") {
            onStatus(id, value ?? idle);
        } else if (kind === "key") {
            onKey(id, Boolean(value));
        } else if (kind === "lock") {
            reactAll(value ? "lock:" : "unlock:", {});
        } else if (kind === "global") {
            reactAll("global:" + id, { name: id, value });
        } else if (kind === "app") {
            reactAll("app:" + id, { ...(value !== null && typeof value === "object" ? value : {}) });
        } else if (kind === "profile") {
            profileTables.rebuild();
        } else if (kind === "nodes") {
            extensionTables.rebuild();
        } else {
            serviceEvents.get(kind)?.(id, value);
        }
    };
    const world = Symbol("world");
    const placed = Symbol("placed");
    const positionOf = (at) =>
        Array.isArray(at) && at.length === 2 && at.every((value) => typeof value === "number" && Number.isFinite(value))
            ? Object.freeze({ x: at[0], y: at[1] })
            : null;
    const inputText = (binding) => (typeof binding === "string" ? binding : (binding?.input ?? null));
    const primarySlots = ["press", "hold", "walkTo", "look", "stick", "up", "zoomIn"];
    const deepFreeze = (value) => {
        if (value !== null && typeof value === "object") {
            for (const key of Object.keys(value)) {
                deepFreeze(value[key]);
            }
            freeze(value);
        }
        return value;
    };
    const ratioOf = (value, what) => {
        const number = typeof value === "number" ? value : NaN;
        if (!(number >= 0 && number <= 1)) {
            throw new RangeError("GS-API-002 " + what + " must be a picture ratio from 0 to 1");
        }
        return number;
    };
    const pointOf = (first, second) => {
        if (first !== null && typeof first === "object") {
            return [ratioOf(first.x, "x"), ratioOf(first.y, "y")];
        }
        return [ratioOf(first, "x"), ratioOf(second, "y")];
    };
    const numberOf = (value, what) => {
        if (typeof value !== "number" || !Number.isFinite(value)) {
            throw new RangeError("GS-API-017 " + what + " must be a finite number");
        }
        return value;
    };
    const amountOf = (value) => {
        const number = value === undefined ? 1 : value;
        if (!(typeof number === "number" && number >= 0 && number <= 1)) {
            throw new RangeError("GS-API-017 the amount must be from 0 to 1");
        }
        return number;
    };
    const tuneMark = Symbol("tune");
    const tunedNumber = (value, low, high, what) => {
        if (!(typeof value === "number" && value >= low && value <= high)) {
            throw new RangeError("GS-API-017 " + what + " must be from " + low + " to " + high);
        }
        return value;
    };
    const durationOf = (value, fallback, most = 60000) => {
        const number = value === undefined ? fallback : value;
        if (!(typeof number === "number" && number > 0 && number <= most)) {
            throw new RangeError("GS-API-003 the duration must be more than 0 and at most " + most + " milliseconds");
        }
        return number;
    };
    const countOf = (value, least, most) => {
        if (!Number.isInteger(value) || value < least || value > most) {
            throw new RangeError("GS-API-017 the count must be a whole number from " + least + " to " + most);
        }
        return value;
    };
    const nameOf = (value) => {
        if (typeof value !== "string" || value === "") {
            throw new TypeError("GS-API-011 the name must be a string that is not empty");
        }
        return value;
    };
    const parsedSettings = (text) => {
        try {
            const parsed = JSON.parse(text || "{}");
            return parsed !== null && typeof parsed === "object" ? parsed : {};
        } catch {
            return {};
        }
    };
    const builtInSettings = new WeakMap();
    const settingsOf = (component) => builtInSettings.get(component) ?? {};
    const childIds = new Map();
    const interfaces = new Map();
    class Interface {
        #name;
        constructor(token, name) {
            if (token !== world) {
                throw new TypeError("Illegal constructor");
            }
            this.#name = name;
        }
        get name() {
            return this.#name;
        }
        all() {
            return Object.values(controlsTable).filter((control) => implementing(control, this));
        }
    }
    const interfacesOf = (Kind) => {
        const found = new Set();
        for (let kind = Kind; typeof kind === "function" && kind !== Component; kind = Object.getPrototypeOf(kind)) {
            if (Object.hasOwn(kind, "implements") && Array.isArray(kind.implements)) {
                for (const each of kind.implements) {
                    found.add(each);
                }
            }
        }
        return found;
    };
    const implementing = (control, iface) =>
        control !== null && typeof control === "object" && interfacesOf(control.constructor).has(iface);
    const defineInterface = function defineInterface(name) {
        const key = nameOf(name);
        let made = interfaces.get(key);
        if (made === undefined) {
            made = freeze(new Interface(world, key));
            interfaces.set(key, made);
        }
        return made;
    };
    const fits = (control, of) =>
        control !== null && control !== undefined &&
        (of === undefined || of === null || (of instanceof Interface ? implementing(control, of) : control instanceof of));
    const builtInOf = (Kind) => {
        const all = Object.values(kinds);
        for (let kind = Kind; typeof kind === "function" && kind !== Component; kind = Object.getPrototypeOf(kind)) {
            if (all.includes(kind)) {
                return kind;
            }
        }
        return undefined;
    };
    const declaredProps = (Kind) => {
        const chain = [];
        const all = Object.values(kinds);
        for (let kind = Kind; typeof kind === "function" && kind !== Component && !all.includes(kind);
            kind = Object.getPrototypeOf(kind)) {
            chain.unshift(kind);
        }
        const merged = {};
        for (const kind of chain) {
            const own = Object.hasOwn(kind, "props") ? kind.props : null;
            if (own !== null && typeof own === "object") {
                Object.assign(merged, own);
            }
        }
        return merged;
    };
    class Hint {
        #id;
        constructor(token, id) {
            if (token !== world) {
                throw new TypeError("Illegal constructor");
            }
            this.#id = id;
        }
        show() {
            host.app("hint", JSON.stringify({ id: this.#id, visible: true }));
        }
        hide() {
            host.app("hint", JSON.stringify({ id: this.#id, visible: false }));
        }
        setText(text) {
            if (typeof text !== "string") {
                throw new TypeError("GS-API-011 the text must be a string");
            }
            host.app("hint", JSON.stringify({ id: this.#id, text: [...text].slice(0, 16).join("") }));
        }
        moveTo(first, second) {
            const [x, y] = pointOf(first, second);
            host.app("hint", JSON.stringify({ id: this.#id, x, y }));
        }
        flash() {
            host.app("hint", JSON.stringify({ id: this.#id, flash: true }));
        }
        reset() {
            host.app("hint", JSON.stringify({ id: this.#id, reset: true }));
        }
    }
    let lastScriptComponent = 0;
    const propKinds = ["key", "position", "number", "text", "toggle", "component"];
)js",
    R"js(    const resolveProp = (spec, given) => {
        const declared = spec !== null && typeof spec === "object" && propKinds.includes(spec.kind)
            ? spec
            : { kind: "value", value: spec };
        const raw = given === undefined ? declared.value : given;
        if (raw === null || raw === undefined) {
            return null;
        }
        switch (declared.kind) {
        case "key":
            return raw instanceof Key ? raw : keysTable[String(raw)];
        case "position": {
            const [x, y] = Array.isArray(raw) ? pointOf(raw[0], raw[1]) : pointOf(raw);
            return freeze({ x, y });
        }
        case "component": {
            const of = declared.of;
            if (raw instanceof Component) {
                return () => (fits(raw, of) ? raw : null);
            }
            const id = String(raw);
            return () => {
                const found = Object.hasOwn(controlsTable, id) ? controlsTable[id] : null;
                return fits(found, of) ? found : null;
            };
        }
        case "number":
            return typeof raw === "number" && Number.isFinite(raw) ? raw : declared.value ?? null;
        case "text":
            return String(raw);
        case "toggle":
            return Boolean(raw);
        default:
            return raw;
        }
    };
    const resolveProps = (declared, given) => {
        const result = {};
        for (const [name, spec] of Object.entries(declared)) {
            const value = resolveProp(spec, given[name]);
            if (spec?.kind === "component" && typeof value === "function") {
                defineProperty(result, name, { enumerable: true, get: value });
            } else {
                result[name] = value;
            }
        }
        return freeze(result);
    };
    const noScreenAction = (component) =>
        new TypeError("GS-API-036 " + component.type + " has no screen action of its own; write one in its class");
    const handable = ["tap", "hold", "longPress", "turbo", "swipe", "multiPress"];

    class Component {
        #id;
        #name;
        #type;
        #props;
        #hint;
        #script = false;
        #plain = false;
        #enabled = true;
        #finger = null;
        #position = null;
        #children = [];
        #childAt = 0;
        constructor(token, info) {
            if (token === world) {
                this.#id = info.id;
                this.#name = info.name;
                this.#type = info.type;
                const { at, points, ...settings } = parsedSettings(info.props);
                this.#position = positionOf(at);
                builtInSettings.set(this, deepFreeze(Array.isArray(points) ? { ...settings, points } : settings));
                this.#plain = info.given === undefined;
                this.#props = this.#plain ? settingsOf(this) : resolveProps(declaredProps(new.target), info.given);
                this.#hint = freeze(new Hint(world, info.id));
                this.#children = Array.isArray(info.children) ? info.children : [];
                return;
            }
            const Kind = new.target;
            if (Kind === Component) {
                throw new TypeError("Illegal constructor");
            }
            for (let kind = Kind; kind !== Component && kind !== null; kind = Object.getPrototypeOf(kind)) {
                if (Object.values(kinds).includes(kind)) {
                    throw new TypeError("GS-API-036 built-in components come from the profile; extend Component to make your own");
                }
            }
            if (token === placed) {
                this.#script = true;
                this.#type = Kind.name || "Component";
                this.#id = info.id;
                this.#name = typeof info.name === "string" && info.name !== "" ? info.name : this.#type;
                this.#props = resolveProps(declaredProps(Kind), info.given);
                this.#hint = freeze(new Hint(world, info.id));
                this.#enabled = info.enabled !== false;
                this.#position = positionOf(info.at);
                this.#children = Array.isArray(info.children) ? info.children : [];
                Promise.resolve().then(() => this.setup());
                return;
            }
            const given = token !== null && typeof token === "object" ? token : {};
            const about = Kind.info;
            this.#script = true;
            this.#type = Kind.name || "Component";
            this.#id = typeof given.id === "string" && given.id !== "" ? given.id : this.#type + "-" + ++lastScriptComponent;
            this.#name = about !== null && typeof about === "object" && typeof about.name === "string" ? about.name : this.#type;
            this.#props = resolveProps(declaredProps(Kind), given);
            this.#hint = freeze(new Hint(world, this.#id));
            this.#enabled = given.enabled !== false;
            Promise.resolve().then(() => this.setup());
        }
        #firstKey() {
            return Object.values(this.#props).find((value) => value instanceof Key) ?? null;
        }
        #keyed(kind, handler, options) {
            const key = this.#firstKey();
            if (key === null) {
                throw new TypeError("GS-API-036 " + this.#type + " has no key property");
            }
            return key[kind](handler, options);
        }
        setup() {}
        child(kind) {
            if (typeof kind !== "function") {
                throw new TypeError("GS-API-008 the kind must be a component class such as Tap");
            }
            const name = this.#children[this.#childAt];
            this.#childAt += 1;
            const id = name === undefined ? undefined : childIds.get(this.#id + "\n" + name);
            const found = id === undefined || !Object.hasOwn(controlsTable, id) ? null : controlsTable[id];
            return found instanceof kind ? found : null;
        }
        event() {
            const listeners = new Set();
            const label = this.#id + ".event";
            return freeze({
                emit: (value) => {
                    for (const listener of [...listeners]) {
                        react(listener, freeze({ value }));
                    }
                },
                on: (handler, options) => {
                    const listener = makeListener(handler, options, label);
                    listeners.add(listener);
                    const dispose = () => {
                        if (!listener.active) {
                            return;
                        }
                        listener.active = false;
                        listeners.delete(listener);
                        stopAll(listener);
                    };
                    own(dispose);
                    return disposable(dispose);
                },
            });
        }
        get finger() {
            if (!this.#script) {
                throw new TypeError("GS-API-036 only components made in logic have a finger of their own");
            }
            if (this.#finger === null) {
                this.#finger = freeze(new ComponentFinger(world));
            }
            return this.#finger;
        }
        get id() {
            return this.#id;
        }
        get position() {
            return this.#position;
        }
        get name() {
            return this.#name;
        }
        get type() {
            return this.#type;
        }
        get props() {
            return this.#props;
        }
        get key() {
            if (this.#script) {
                return this.#firstKey()?.name ?? null;
            }
            const inputs = settingsOf(this).inputs ?? {};
            for (const slot of primarySlots) {
                const text = inputText(inputs[slot]);
                if (text) {
                    return text;
                }
            }
            return null;
        }
        get inputs() {
            const result = {};
            if (this.#script) {
                for (const [name, value] of Object.entries(this.#props)) {
                    if (value instanceof Key) {
                        result[name] = value.name;
                    }
                }
                return freeze(result);
            }
            for (const [slot, binding] of Object.entries(settingsOf(this).inputs ?? {})) {
                const text = inputText(binding);
                if (text) {
                    result[slot] = text;
                }
            }
            return freeze(result);
        }
        get hint() {
            return this.#hint;
        }
        get enabled() {
            return this.#script ? this.#enabled : enabledOf(this.#id);
        }
        set enabled(on) {
            if (this.#script) {
                const value = Boolean(on);
                if (value !== this.#enabled) {
                    this.#enabled = value;
                    reactAll((value ? "enabled:" : "disabled:") + this.#id, { control: this.#id });
                }
                return;
            }
            const binding = bindings.get(this.#id);
            if (binding !== undefined) {
                unbind(binding, false);
            }
            host.setEnabled(this.#id, Boolean(on));
        }
        enable() {
            this.enabled = true;
        }
        disable() {
            this.enabled = false;
        }
        toggle() {
            this.enabled = !this.enabled;
        }
        get active() {
            return pressedSince.has(this.#id);
        }
        get pressedAt() {
            return pressedSince.get(this.#id) ?? null;
        }
        get heldMs() {
            const at = pressedSince.get(this.#id);
            return at === undefined ? 0 : now() - at;
        }
        get touching() {
            return statusOf(this.#id).touching;
        }
        trigger() {
            if (this.#script) {
                throw noScreenAction(this);
            }
            const id = this.#id;
            return act(() => host.trigger(id));
        }
        hold(durationMs) {
            if (this.#script) {
                throw noScreenAction(this);
            }
            const id = this.#id;
            return act(() => host.hold(id, durationMs));
        }
        press() {
            if (this.#script) {
                throw noScreenAction(this);
            }
            host.press(this.#id, true);
        }
        release() {
            if (this.#script) {
                throw noScreenAction(this);
            }
            host.press(this.#id, false);
        }
        reset() {
            if (this.#script) {
                throw noScreenAction(this);
            }
            host.act(this.#id, "reset", 0, 0);
        }
        [tuneMark](change) {
            if (this.#script) {
                throw noScreenAction(this);
            }
            const { at, points, ...settings } = parsedSettings(host.tune(this.#id, JSON.stringify(change)));
            this.#position = positionOf(at);
            builtInSettings.set(this, deepFreeze(Array.isArray(points) ? { ...settings, points } : settings));
            if (this.#plain) {
                this.#props = settingsOf(this);
            }
            const moved = change.at !== undefined || change.reset === "position" || change.reset === "all";
            if (moved && this.#position !== null) {
                host.app("hint", JSON.stringify({ id: this.#id, x: this.#position.x, y: this.#position.y }));
            }
        }
        moveTo(first, second) {
            if (this.#script) {
                throw noScreenAction(this);
            }
            this[tuneMark]({ at: pointOf(first, second) });
        }
        moveBy(dx, dy) {
            if (this.#script || this.#position === null) {
                throw noScreenAction(this);
            }
            this[tuneMark]({ at: pointOf(this.#position.x + numberOf(dx, "dx"), this.#position.y + numberOf(dy, "dy")) });
        }
        resetPosition() {
            this[tuneMark]({ reset: "position" });
        }
        resetProps() {
            this[tuneMark]({ reset: "all" });
        }
        bind(property, compute) {
            if (this.#script) {
                throw noScreenAction(this);
            }
            return bindProperty(this.#id, property, compute);
        }
        onPress(handler, options) {
            if (this.#script) {
                return this.#keyed("onPress", handler, options);
            }
            return this.#keyEvent("press", handler, options, ".onPress");
        }
        onRelease(handler, options) {
            if (this.#script) {
                return this.#keyed("onRelease", handler, options);
            }
            return this.#keyEvent("release", handler, options, ".onRelease");
        }
        whileHeld(handler, options) {
            if (this.#script) {
                return this.#keyed("whileHeld", handler, options);
            }
            return this.#keyEvent("held", handler, options, ".whileHeld");
        }
        #keyEvent(kind, handler, options, suffix) {
            const consume = options?.consume ?? true;
            if (typeof consume !== "boolean") {
                throw new TypeError("GS-API-011 consume must be true or false");
            }
            const id = this.#id;
            if (!consume || !handable.includes(this.#type)) {
                return listen(kind, id, handler, options, id + suffix);
            }
            const subscription = listen(kind, id, handler, options, id + suffix, () => handOver(id, false));
            handOver(id, true);
            return subscription;
        }
        onEnabled(handler, options) {
            return listen("enabled", this.#id, handler, options, this.#id + ".onEnabled");
        }
        onDisabled(handler, options) {
            return listen("disabled", this.#id, handler, options, this.#id + ".onDisabled");
        }
        onTouchStart(handler, options) {
            return listen("touchStart", this.#id, handler, options, this.#id + ".onTouchStart");
        }
        onTouchEnd(handler, options) {
            return listen("touchEnd", this.#id, handler, options, this.#id + ".onTouchEnd");
        }
    }
    const pressFor = (rateHz, tapMs) => Math.max(1, Math.min(tapMs > 0 ? tapMs : 34, 500 / rateHz));
    const tapSlot = (id, slot) => {
        host.act(id, "down:" + slot, 0, 0);
        host.act(id, "up:" + slot, 0, 0);
    };
    class Tap extends Component {
        get tapMs() {
)js",
    R"js(            return settingsOf(this).tapMs ?? 0;
        }
        setDuration(durationMs) {
            this[tuneMark]({ tapMs: Math.round(tunedNumber(durationMs, 0, 1000, "the tap length in milliseconds")) });
        }
        resetDuration() {
            this[tuneMark]({ reset: "duration" });
        }
        tap() {
            const ms = this.tapMs;
            return ms > 0 ? this.hold(ms) : this.trigger();
        }
        tapTimes(count, gapMs) {
            const times = countOf(count, 1, 100);
            const gap = durationOf(gapMs, 100);
            const id = this.id;
            const ms = this.tapMs;
            return act(async () => {
                for (let index = 0; index < times; index += 1) {
                    if (index > 0) {
                        await host.sleep(gap);
                    }
                    await (ms > 0 ? host.hold(id, ms) : host.trigger(id));
                }
            });
        }
    }
    class Hold extends Component {
        get holding() {
            return this.touching;
        }
        holdFor(durationMs) {
            return this.hold(durationMs);
        }
        toggleHold() {
            if (this.touching) {
                this.release();
            } else {
                this.press();
            }
        }
        onPressed(handler, options) {
            return this.onTouchStart(handler, options);
        }
        onReleased(handler, options) {
            return this.onTouchEnd(handler, options);
        }
    }
    class LongPress extends Component {
        get holdMs() {
            return settingsOf(this).holdMs ?? 1000;
        }
        get holding() {
            return this.touching;
        }
        longPress(durationMs) {
            return this.hold(durationMs === undefined ? this.holdMs : durationMs);
        }
        setDuration(durationMs) {
            this[tuneMark]({ holdMs: Math.round(tunedNumber(durationMs, 100, 10000, "the long press length in milliseconds")) });
        }
        resetDuration() {
            this[tuneMark]({ reset: "duration" });
        }
        onStart(handler, options) {
            return this.onTouchStart(handler, options);
        }
        onEnd(handler, options) {
            return this.onTouchEnd(handler, options);
        }
    }
    class Turbo extends Component {
        #task = 0;
        get rateHz() {
            return settingsOf(this).rateHz ?? 10;
        }
        get tapMs() {
            return settingsOf(this).tapMs ?? 0;
        }
        get firing() {
            return this.#task !== 0;
        }
        start(rateHz) {
            const rate = rateHz === undefined ? this.rateHz : rateHz;
            if (!(typeof rate === "number" && rate >= 1 && rate <= 30)) {
                throw new RangeError("GS-API-017 the rate must be from 1 to 30 presses a second");
            }
            if (this.#task !== 0) {
                return;
            }
            const id = this.id;
            const period = 1000 / rate;
            const down = pressFor(rate, this.tapMs);
            const holder = { task: 0 };
            holder.task = host.spawn(async ({ signal }) => {
                try {
                    while (!signal.aborted) {
                        const started = now();
                        await host.hold(id, down);
                        const left = period - (now() - started);
                        if (left > 0) {
                            await host.sleep(left);
                        }
                    }
                } finally {
                    if (this.#task === holder.task) {
                        this.#task = 0;
                    }
                }
            }, id + ".start", {});
            this.#task = holder.task;
        }
        stop() {
            const task = this.#task;
            this.#task = 0;
            if (task !== 0) {
                host.cancel(task);
            }
        }
        burst(count) {
            const times = countOf(count, 1, 100);
            const id = this.id;
            const period = 1000 / this.rateHz;
            const down = pressFor(this.rateHz, this.tapMs);
            return act(async () => {
                for (let index = 0; index < times; index += 1) {
                    const started = now();
                    await host.hold(id, down);
                    const left = period - (now() - started);
                    if (left > 0 && index + 1 < times) {
                        await host.sleep(left);
                    }
                }
            });
        }
        setRate(rateHz) {
            this[tuneMark]({ rateHz: Math.round(tunedNumber(rateHz, 1, 30, "the rate in presses a second")) });
        }
        resetRate() {
            this[tuneMark]({ reset: "rate" });
        }
    }
    class Swipe extends Component {
        get durationMs() {
            return settingsOf(this).durationMs ?? 200;
        }
        get swiping() {
            return this.touching;
        }
        swipe() {
            return this.trigger();
        }
        setDuration(durationMs) {
            this[tuneMark]({ durationMs: Math.round(tunedNumber(durationMs, 10, 10000, "the swipe length in milliseconds")) });
        }
        resetDuration() {
            this[tuneMark]({ reset: "duration" });
        }
        onStart(handler, options) {
            return this.onTouchStart(handler, options);
        }
        onEnd(handler, options) {
            return this.onTouchEnd(handler, options);
        }
    }
    class Pinch extends Component {
        get pinching() {
            return this.touching;
        }
        zoomIn() {
            tapSlot(this.id, "zoomIn");
        }
        zoomOut() {
            tapSlot(this.id, "zoomOut");
        }
    }
    const pushOf = (direction, amount) => {
        const radians = (numberOf(direction, "the direction") * Math.PI) / 180;
        const length = amountOf(amount);
        const clean = (value) => Math.round(value * 1e6) / 1e6 + 0;
        return [clean(Math.sin(radians) * length), clean(-Math.cos(radians) * length)];
    };
    const radiusChange = (radius) => ({ radius: tunedNumber(radius, 0.01, 1, "the radius") });
    const sensitivityChange = (x, y) => ({
        sensitivity: [tunedNumber(x, 0.01, 100, "the sensitivity"),
            tunedNumber(y === undefined ? x : y, 0.01, 100, "the sensitivity")],
    });
    class Stick extends Component {
        get radius() {
            return settingsOf(this).radius ?? 0.12;
        }
        setRadius(radius) {
            this[tuneMark](radiusChange(radius));
        }
        resetRadius() {
            this[tuneMark]({ reset: "radius" });
        }
        get moving() {
            return moving(statusOf(this.id).stick);
        }
        get direction() {
            return degreesOf(statusOf(this.id).stick);
        }
        get amount() {
            const stick = statusOf(this.id).stick;
            return Math.min(1, Math.hypot(stick.x, stick.y));
        }
        push(direction, amount) {
            const [x, y] = pushOf(direction, amount);
            host.act(this.id, "push", x, y);
        }
        release() {
            host.act(this.id, "push", 0, 0);
        }
        walkFor(direction, durationMs, amount) {
            const [x, y] = pushOf(direction, amount);
            const ms = durationOf(durationMs, 1000);
            const id = this.id;
            return act(async () => {
                host.act(id, "push", x, y);
                try {
                    await host.sleep(ms);
                } finally {
                    host.act(id, "push", 0, 0);
                }
            });
        }
        onMoveStart(handler, options) {
            return listen("moveStart", this.id, handler, options, this.id + ".onMoveStart");
        }
        onStop(handler, options) {
            return listen("moveEnd", this.id, handler, options, this.id + ".onStop");
        }
        onDirectionChange(handler, options) {
            return listen("direction", this.id, handler, options, this.id + ".onDirectionChange");
        }
    }
    class Wasd extends Stick {
        get walkScale() {
            return settingsOf(this).walkScale ?? 0.5;
        }
        get sprintScale() {
            return settingsOf(this).sprintScale ?? 1.6;
        }
    }
    class PadStick extends Stick {}
    class RightWalk extends Component {
        get walking() {
            return statusOf(this.id).walking;
        }
        get walkMsPerUnit() {
            return settingsOf(this).walkMsPerUnit ?? 600;
        }
        get stopOnRelease() {
            return settingsOf(this).stopOnRelease ?? true;
        }
        walkTo(first, second) {
            const [x, y] = pointOf(first, second);
            host.act(this.id, "walkTo", x, y);
        }
        stop() {
            tapSlot(this.id, "stop");
        }
        setRadius(radius) {
            this[tuneMark](radiusChange(radius));
        }
        resetRadius() {
            this[tuneMark]({ reset: "radius" });
        }
        onWalkStart(handler, options) {
            return listen("walkStart", this.id, handler, options, this.id + ".onWalkStart");
        }
        onStop(handler, options) {
            return listen("walkEnd", this.id, handler, options, this.id + ".onStop");
        }
    }
    const turnBy = (id, dx, dy) => host.act(id, "turn", numberOf(dx, "dx"), numberOf(dy, "dy"));
    class Look extends Component {
        get turning() {
            return this.touching;
        }
        recenter() {
            this.reset();
        }
        turn(dx, dy) {
            turnBy(this.id, dx, dy);
        }
        setSensitivity(x, y) {
            this[tuneMark](sensitivityChange(x, y));
        }
        resetSensitivity() {
            this[tuneMark]({ reset: "sensitivity" });
        }
        onTurnStart(handler, options) {
            return this.onTouchStart(handler, options);
        }
        onTurnEnd(handler, options) {
            return this.onTouchEnd(handler, options);
        }
    }
    class Camera extends Look {
        get locked() {
            return host.locked();
        }
        lock() {
            host.act("", "lock", 0, 0);
        }
        unlock() {
            host.act("", "unlock", 0, 0);
        }
        toggleLock() {
            host.act("", "toggleLock", 0, 0);
        }
        onLock(handler, options) {
            return listen("lock", "", handler, options, this.id + ".onLock");
        }
        onUnlock(handler, options) {
            return listen("unlock", "", handler, options, this.id + ".onUnlock");
        }
    }
    class PadCamera extends Look {}
    class FreeLook extends Component {
        get looking() {
            return this.touching;
        }
        begin() {
            this.press();
        }
        end() {
            this.release();
        }
        turn(dx, dy) {
            turnBy(this.id, dx, dy);
        }
        setSensitivity(x, y) {
            this[tuneMark](sensitivityChange(x, y));
        }
        resetSensitivity() {
            this[tuneMark]({ reset: "sensitivity" });
        }
        onStart(handler, options) {
            return this.onTouchStart(handler, options);
        }
        onEnd(handler, options) {
            return this.onTouchEnd(handler, options);
        }
    }
    class Skill extends Component {
        get aiming() {
            return statusOf(this.id).aiming;
        }
        aim() {
            this.press();
        }
        cast() {
            if (settingsOf(this).cast === "manual") {
                tapSlot(this.id, "cast");
            } else {
                this.release();
            }
        }
        cancel() {
            tapSlot(this.id, "cancel");
        }
        turn(dx, dy) {
            turnBy(this.id, dx, dy);
        }
        setRadius(radius) {
            this[tuneMark](radiusChange(radius));
        }
        resetRadius() {
            this[tuneMark]({ reset: "radius" });
        }
        setSensitivity(x, y) {
            this[tuneMark](sensitivityChange(x, y));
        }
        resetSensitivity() {
            this[tuneMark]({ reset: "sensitivity" });
        }
        onAimStart(handler, options) {
            return listen("aimStart", this.id, handler, options, this.id + ".onAimStart");
        }
        onCast(handler, options) {
            return listen("cast", this.id, handler, options, this.id + ".onCast");
        }
        onCancel(handler, options) {
            return listen("cancel", this.id, handler, options, this.id + ".onCancel");
        }
    }
    class Dial extends Component {
        get open() {
            return this.touching;
        }
        get cell() {
            return statusOf(this.id).cell ?? -1;
        }
        get cells() {
            return settingsOf(this).cells ?? 8;
        }
        choose(cell) {
            const cells = this.cells;
            if (!(Number.isInteger(cell) && cell >= 0 && cell < cells)) {
                throw new RangeError("GS-API-017 the cell must be a whole number from 0 to " + (cells - 1));
            }
            const radians = (((settingsOf(this).firstDeg ?? 0) + (cell * 360) / cells) * Math.PI) / 180;
            const id = this.id;
            return act(async () => {
                host.press(id, true);
                try {
                    await host.sleep(50);
                    host.act(id, "turn", 100 * Math.sin(radians), -100 * Math.cos(radians));
                    await host.sleep(50);
                } finally {
                    host.press(id, false);
                }
            });
        }
        onOpen(handler, options) {
            return this.onTouchStart(handler, options);
        }
        onChoose(handler, options) {
            return listen("choose", this.id, handler, options, this.id + ".onChoose");
        }
    }
    class MultiPress extends Component {
        get places() {
            const points = settingsOf(this).points;
            return (Array.isArray(points) ? points.length : 1) + 1;
)js",
    R"js(        }
        get holding() {
            return this.touching;
        }
        holdFor(durationMs) {
            return this.hold(durationMs);
        }
        toggleHold() {
            if (this.touching) {
                this.release();
            } else {
                this.press();
            }
        }
        onPressed(handler, options) {
            return this.onTouchStart(handler, options);
        }
        onReleased(handler, options) {
            return this.onTouchEnd(handler, options);
        }
    }
    class TempCursor extends Component {
        get showing() {
            return this.touching;
        }
        onShow(handler, options) {
            return this.onTouchStart(handler, options);
        }
        onHide(handler, options) {
            return this.onTouchEnd(handler, options);
        }
    }
    class ActionWheel extends Component {
        get open() {
            return this.touching;
        }
        get cell() {
            return statusOf(this.id).cell ?? -1;
        }
        get cells() {
            const cells = settingsOf(this).cells;
            return Array.isArray(cells) ? cells.length : 8;
        }
        get names() {
            const cells = settingsOf(this).cells;
            return Array.isArray(cells) ? cells.map((cell) => (typeof cell?.name === "string" ? cell.name : ""))
                : Array.from({ length: 8 }, () => "");
        }
        choose(cell) {
            const cells = this.cells;
            if (!(Number.isInteger(cell) && cell >= 0 && cell < cells)) {
                throw new RangeError("GS-API-017 the cell must be a whole number from 0 to " + (cells - 1));
            }
            const radians = (cell * 2 * Math.PI) / cells;
            const id = this.id;
            return act(async () => {
                host.press(id, true);
                try {
                    await host.sleep(50);
                    host.act(id, "turn", 100 * Math.sin(radians), -100 * Math.cos(radians));
                    await host.sleep(50);
                } finally {
                    host.press(id, false);
                }
            });
        }
        onOpen(handler, options) {
            return this.onTouchStart(handler, options);
        }
        onChoose(handler, options) {
            const names = () => this.names;
            const named = typeof handler === "function"
                ? (event) => handler(freeze({ ...event, name: names()[event?.cell] ?? "" }))
                : handler;
            return listen("choose", this.id, named, options, this.id + ".onChoose");
        }
    }
    const kinds = { tap: Tap, hold: Hold, longPress: LongPress, turbo: Turbo, swipe: Swipe, pinch: Pinch, wasd: Wasd,
        rightWalk: RightWalk, padStick: PadStick, camera: Camera, padCamera: PadCamera, freeLook: FreeLook, skill: Skill,
        dial: Dial, multiPress: MultiPress, tempCursor: TempCursor, actionWheel: ActionWheel };
    const slotsOf = { tap: ["press"], hold: ["press"], longPress: ["press"], turbo: ["press"], swipe: ["press"],
        pinch: ["zoomIn", "zoomOut"], wasd: ["up", "down", "left", "right", "walk", "sprint"], rightWalk: ["walkTo", "stop"],
        padStick: ["stick"], camera: ["look"], padCamera: ["look"], freeLook: ["hold", "look"],
        skill: ["press", "look", "cancel", "cast"], dial: ["press", "look"], multiPress: ["press"], tempCursor: ["press"],
        actionWheel: ["press", "look"] };
    class Variable {
        #id;
        #name;
        constructor(token, info) {
            if (token !== world) {
                throw new TypeError("Illegal constructor");
            }
            this.#id = info.id;
            this.#name = info.name;
        }
        get id() {
            return this.#id;
        }
        get name() {
            return this.#name;
        }
        get value() {
            const value = host.stateValue(this.#id);
            if (reading !== null) {
                reading.set(this.#id, value);
            }
            return value;
        }
        set value(next) {
            if (typeof next !== "boolean" && typeof next !== "string" &&
                !(typeof next === "number" && Number.isFinite(next))) {
                throw new TypeError("GS-API-010 a variable holds true or false, a finite number or a string");
            }
            host.setState(this.#id, next);
            changed(this.#id, next);
        }
        toggle() {
            this.value = !this.value;
        }
        onChange(handler, options) {
            return listen("state", this.#id, handler, options, this.#id + ".onChange");
        }
    }
    const keyAliases = { Esc: "Escape", Ctrl: "ControlLeft", Control: "ControlLeft", Shift: "ShiftLeft", Alt: "AltLeft",
        Up: "ArrowUp", Down: "ArrowDown", Left: "ArrowLeft", Right: "ArrowRight", Return: "Enter" };
    const canonicalKey = (name) => {
        if (/^[A-Za-z]$/.test(name)) {
            return "Key" + name.toUpperCase();
        }
        if (/^[0-9]$/.test(name)) {
            return "Digit" + name;
        }
        if (Object.hasOwn(keyAliases, name)) {
            return keyAliases[name];
        }
        if (/^Wheel(Up|Down|Left|Right)$/.test(name)) {
            return "Mouse." + name;
        }
        if (/^Mouse[A-Z0-9]/.test(name)) {
            return "Mouse." + name.slice(5);
        }
        if (/^Gamepad[A-Z]/.test(name)) {
            return "Gamepad." + name.slice(7);
        }
        return name;
    };
    const claimed = new Set();
    const claim = (name) => {
        if (claimed.has(name)) {
            return;
        }
        if (!host.claimKey(name)) {
            throw new RangeError("GS-API-013 \"" + name + "\" is not the name of a key, mouse button or gamepad button");
        }
        claimed.add(name);
    };
    class Key {
        #name;
        constructor(token, name) {
            if (token !== world) {
                throw new TypeError("Illegal constructor");
            }
            this.#name = name;
        }
        get name() {
            return this.#name;
        }
        get isDown() {
            return keyEntry(this.#name).down;
        }
        get pressedAt() {
            return keyEntry(this.#name).pressedAt;
        }
        get heldMs() {
            const entry = keyEntry(this.#name);
            return entry.down && entry.pressedAt !== null ? now() - entry.pressedAt : 0;
        }
        #on(kind, handler, options, label) {
            claim(this.#name);
            return listen(kind, this.#name, handler, options, this.#name + "." + label);
        }
        onPress(handler, options) {
            return this.#on("keyPress", handler, options, "onPress");
        }
        onRelease(handler, options) {
            return this.#on("keyRelease", handler, options, "onRelease");
        }
        whileHeld(handler, options) {
            return this.#on("keyHeld", handler, options, "whileHeld");
        }
        onTap(handler, options) {
            return this.#on("keyTap", handler, options, "onTap");
        }
        onDoubleTap(handler, options) {
            return this.#on("keyDouble", handler, options, "onDoubleTap");
        }
        onLongPress(durationMs, handler, options) {
            const ms = durationOf(durationMs, 500);
            const listener = makeListener(handler, options, this.#name + ".onLongPress");
            claim(this.#name);
            const entry = keyEntry(this.#name);
            const long = { ms, listener };
            entry.longs.push(long);
            const dispose = () => {
                if (!listener.active) {
                    return;
                }
                listener.active = false;
                entry.longs = entry.longs.filter((each) => each !== long);
                stopAll(listener);
            };
            own(dispose);
            return disposable(dispose);
        }
    }
    const keyObjects = new Map();
    const keysTable = new Proxy(Object.create(null), {
        get(target, property) {
            if (typeof property !== "string" || property === "then" || property === "toJSON") {
                return undefined;
            }
            const name = canonicalKey(property);
            let key = keyObjects.get(name);
            if (key === undefined) {
                key = freeze(new Key(world, name));
                keyObjects.set(name, key);
            }
            return key;
        },
        set() {
            return false;
        },
        defineProperty() {
            return false;
        },
        deleteProperty() {
            return false;
        },
    });
    class Gate {
        #open;
        constructor(token, open) {
            if (token !== world) {
                throw new TypeError("Illegal constructor");
            }
            this.#open = open;
        }
        get isOpen() {
            return this.#open;
        }
        open() {
            this.#open = true;
        }
        close() {
            this.#open = false;
        }
        toggle() {
            this.#open = !this.#open;
        }
        run(action) {
            if (typeof action !== "function") {
                throw new TypeError("GS-API-008 the action must be a function");
            }
            return this.#open ? action() : undefined;
        }
    }
    class Stopwatch {
        #start;
        #lap;
        constructor(token) {
            if (token !== world) {
                throw new TypeError("Illegal constructor");
            }
            this.#start = now();
            this.#lap = this.#start;
        }
        get elapsedMs() {
            return now() - this.#start;
        }
        reset() {
            this.#start = now();
            this.#lap = this.#start;
        }
        lap() {
            const at = now();
            const length = at - this.#lap;
            this.#lap = at;
            return length;
        }
    }
    class Finger {
        #id;
        #down = true;
        constructor(token, id) {
            if (token !== world) {
                throw new TypeError("Illegal constructor");
            }
            this.#id = id;
        }
        get down() {
            return this.#down;
        }
        move(first, second, third) {
            const [x, y] = pointOf(first, second);
            const given = first !== null && typeof first === "object" ? second ?? third : third;
            const ms = given === undefined ? 0 : durationOf(given, 0);
            if (!this.#down) {
                throw new RangeError("GS-API-033 the finger was already lifted");
            }
            const id = this.#id;
            return act(async () => {
                host.fingerMove(id, x, y, ms);
                if (ms > 0) {
                    await host.sleep(ms);
                }
            });
        }
        up() {
            if (this.#down) {
                this.#down = false;
                host.fingerUp(this.#id);
            }
        }
    }
    class ComponentFinger {
        #finger = null;
        constructor(token) {
            if (token !== world) {
                throw new TypeError("Illegal constructor");
            }
        }
        get isDown() {
            return this.#finger !== null && this.#finger.down;
        }
        down(first, second) {
            if (this.isDown) {
                this.#finger.up();
            }
            const [x, y] = pointOf(first, second);
            this.#finger = freeze(new Finger(world, host.fingerDown(x, y)));
            return this.#finger;
        }
        move(first, second, third) {
            if (!this.isDown) {
                throw new RangeError("GS-API-033 the finger is not down");
            }
            return this.#finger.move(first, second, third);
        }
        up() {
            if (this.#finger !== null) {
                this.#finger.up();
                this.#finger = null;
            }
        }
    }
    const hooked = new WeakMap();
    const eventName = (name) => /^on[A-Z]/.test(name) || name === "whileHeld";
    for (const kind of [Component, Tap, Hold, LongPress, Turbo, Swipe, Pinch, Stick, Wasd, PadStick, RightWalk, Look,
        Camera, PadCamera, FreeLook, Skill, Dial, MultiPress, TempCursor, ActionWheel]) {
        for (const name of Object.getOwnPropertyNames(kind.prototype).filter(eventName)) {
            const listenTo = kind.prototype[name];
            defineProperty(kind.prototype, name, {
                value: {
                    [name](handler, options) {
                        if (handler === undefined || (typeof handler !== "function" && hooked.get(this)?.has(name))) {
                            return undefined;
                        }
                        return listenTo.call(this, handler, options);
                    },
                }[name],
            });
        }
    }
    for (const kind of [Hint, Interface, Component, Tap, Hold, LongPress, Turbo, Swipe, Pinch, Stick, Wasd, PadStick,
        RightWalk, Look, Camera, PadCamera, FreeLook, Skill, Dial, MultiPress, TempCursor, ActionWheel, Variable, Key, Gate, Stopwatch,
        Finger, ComponentFinger]) {
        defineProperty(kind.prototype, Symbol.toStringTag, { value: kind.name });
        freeze(kind.prototype);
    }
    const table = (list, make) => {
        const result = Object.create(null);
        for (const info of list) {
            result[info.id] = freeze(make(info));
        }
        return freeze(result);
    };
    const placedControls = new Map();
    const mountable = [];
    const buildControls = () => {
        const result = Object.create(null);
        mountable.length = 0;
        childIds.clear();
        for (const info of host.controls()) {
            const settings = parsedSettings(info.props);
            if (typeof settings.parent === "string" && typeof settings.child === "string") {
)js",
    R"js(                childIds.set(settings.parent + "\n" + settings.child, info.id);
            }
            if (info.type === "component") {
                mountable.push(info);
                defineProperty(result, info.id, { enumerable: true, get: () => placedControls.get(info.id) ?? null });
                continue;
            }
            const made = freeze(new (kinds[info.type] ?? Component)(world, info));
            if (typeof settings.component === "string" && settings.component !== "") {
                mountable.push(info);
                defineProperty(result, info.id, { enumerable: true, get: () => placedControls.get(info.id) ?? made });
            } else {
                result[info.id] = made;
            }
        }
        return freeze(result);
    };
    let controlsTable = buildControls();
    const takeOver = (made, Kind, own) => {
        const names = [];
        for (let proto = own.prototype; proto !== null && proto !== Object.prototype; proto = Object.getPrototypeOf(proto)) {
            for (const name of Object.getOwnPropertyNames(proto).filter(eventName)) {
                if (!names.includes(name) && typeof Kind.prototype[name] === "function" &&
                    Kind.prototype[name] !== own.prototype[name]) {
                    names.push(name);
                }
            }
        }
        hooked.set(made, new Set(names));
        for (const name of names) {
            const hook = Kind.prototype[name];
            const builtIn = own.prototype[name];
            defineProperty(made, name, {
                configurable: true,
                writable: true,
                value: (first, options) => (typeof first === "function" ? builtIn.call(made, first, options)
                                                                         : hook.call(made, first)),
            });
        }
        for (const name of names) {
            const hook = Kind.prototype[name];
            own.prototype[name].call(made, (event) => hook.call(made, event), { consume: false });
        }
    };
    const mountComponents = (entries) => {
        const classes = new Map();
        for (const entry of Array.isArray(entries) ? entries : []) {
            if (Array.isArray(entry)) {
                classes.set(entry[0], { Kind: entry[1], children: Array.isArray(entry[2]) ? entry[2] : [] });
            }
        }
        for (const info of mountable) {
            const settings = parsedSettings(info.props);
            const { Kind, children } = classes.get(settings.component) ?? {};
            if (typeof Kind !== "function" || !(Kind.prototype instanceof Component)) {
                globalThis.log.warn(info.id + ": " + settings.component + " has no default export that extends Component");
                continue;
            }
            const builtIn = builtInOf(Kind);
            const own = info.type === "component" ? undefined : kinds[info.type];
            if (builtIn !== own) {
                globalThis.log.warn(info.id + ": " + settings.component + (builtIn === undefined
                    ? " does not extend " + own.name
                    : " extends " + builtIn.name + "; place it on the picture again"));
                continue;
            }
            const slots = own === undefined ? [] : slotsOf[info.type] ?? [];
            const given = { ...(settings.props ?? {}) };
            for (const [name, binding] of Object.entries(settings.inputs ?? {})) {
                if (!slots.includes(name)) {
                    given[name] = typeof binding === "string" ? binding : binding?.input;
                }
            }
            for (const [name, point] of Object.entries(settings.points ?? {})) {
                given[name] = point;
            }
            for (const [name, spec] of Object.entries(declaredProps(Kind))) {
                if (spec?.kind === "key" && slots.includes(name)) {
                    globalThis.log.warn(info.id + ": the key property " + name + " has the name of a key of " + own.name +
                        " and is left out");
                    given[name] = null;
                }
            }
            try {
                if (own === undefined) {
                    placedControls.set(info.id, new Kind(placed, { id: info.id, name: info.name, enabled: info.enabled,
                        at: settings.at, given, children }));
                    continue;
                }
                const made = new Kind(world, { id: info.id, name: info.name, type: info.type, props: info.props, given,
                    children });
                takeOver(made, Kind, own);
                placedControls.set(info.id, made);
                Promise.resolve().then(() => made.setup());
            } catch (error) {
                globalThis.log.error(info.id + ": " + (error?.message ?? String(error)));
            }
        }
    };
    defineProperty(globalThis, "controls", { ...replaceable, value: controlsTable });
    defineProperty(globalThis, "Component", { ...replaceable, value: Component });
    for (const kind of Object.values(kinds)) {
        defineProperty(globalThis, kind.name, { ...replaceable, value: kind });
    }
    defineProperty(globalThis, "defineInterface", { ...replaceable, value: defineInterface });
    let varsTable = table(host.states(), (info) => new Variable(world, info));
    defineProperty(globalThis, "vars", { ...replaceable, value: varsTable });
    profileTables.rebuild = () => {
        controlsTable = buildControls();
        varsTable = table(host.states(), (info) => new Variable(world, info));
        defineProperty(globalThis, "controls", { ...replaceable, value: controlsTable });
        defineProperty(globalThis, "vars", { ...replaceable, value: varsTable });
    };
    defineProperty(globalThis, "keys", { ...replaceable, value: keysTable });
    defineProperty(globalThis, "globals", {
        ...replaceable,
        value: freeze({
            get(name) {
                return host.global(nameOf(name));
            },
            set(name, value) {
                const json = JSON.stringify(value);
                if (json === undefined) {
                    throw new TypeError("GS-API-006 global values must be JSON values (no functions or undefined)");
                }
                host.setGlobal(nameOf(name), json);
            },
            onChange(name, handler, options) {
                return listen("global", nameOf(name), handler, options, "globals." + name + ".onChange");
            },
        }),
    });
)js",
    R"js(    const jobListeners = new Map();
    const jobOf = (extension, job, args) => {
        const started = host.call("native.jobStart", JSON.stringify({ extension, job, args }));
        const id = String(started.id);
        const listeners = { progress: [], log: [], output: [] };
        jobListeners.set(id, listeners);
        const result = host.request("native.jobResult", JSON.stringify({ id }));
        result.then(() => jobListeners.delete(id), () => jobListeners.delete(id));
        const handle = freeze({
            result,
            onProgress(listener) {
                if (typeof listener === "function") {
                    listeners.progress.push(listener);
                }
                return handle;
            },
            onLog(listener) {
                if (typeof listener === "function") {
                    listeners.log.push(listener);
                }
                return handle;
            },
            onOutput(listener) {
                if (typeof listener === "function") {
                    listeners.output.push(listener);
                }
                return handle;
            },
            async follow(progress, done, failed) {
                if (typeof progress === "function") {
                    listeners.progress.push(progress);
                }
                let value;
                try {
                    value = await result;
                } catch (error) {
                    if (typeof failed !== "function") {
                        throw error;
                    }
                    await failed(error);
                    return;
                }
                if (typeof done === "function") {
                    await done(value);
                }
            },
            cancel() {
                host.call("native.jobCancel", JSON.stringify({ id }));
            },
        });
        return handle;
    };
    const tellJob = (list, first, second) => {
        for (const listener of [...list]) {
            try {
                listener(first, second);
            } catch (error) {
                Promise.reject(error);
            }
        }
    };
    serviceEvents.set("job", (id, value) => {
        const listeners = jobListeners.get(id);
        if (listeners === undefined || value === null || typeof value !== "object") {
            return;
        }
        if (value.kind === "progress") {
            tellJob(listeners.progress, value.fraction, value.text);
        } else if (value.kind === "log") {
            tellJob(listeners.log, value.level, value.text);
        } else if (value.kind === "output") {
            tellJob(listeners.output, value.value);
        }
    });
    serviceEvents.set("extEvent", (topic, value) => reactAll("extEvent:" + topic, { topic, value }));
    const extensionsOf = () => {
        const root = Object.create(null);
        defineProperty(root, "call", { value: serviceCall });
        defineProperty(root, "provide", { value: serviceProvide });
        for (const node of host.nodes()) {
            let level = root;
            let blocked = false;
            for (const part of node.owner) {
                const next = level[part];
                if (next === undefined) {
                    const made = Object.create(null);
                    level[part] = made;
                    level = made;
                } else if (typeof next === "function") {
                    blocked = true;
                    break;
                } else {
                    level = next;
                }
            }
            if (blocked || node.name in level) {
                continue;
            }
            const extension = node.extension;
            const name = node.name;
            const call = node.job
                ? {
                      [name](args = {}) {
                          return jobOf(extension, name, args);
                      },
                  }[name]
                : {
                      [name](...values) {
                          return host.callNode(extension, name, values);
                      },
                  }[name];
            defineProperty(call, "length", { value: node.job ? 1 : node.count });
            level[name] = call;
        }
        const seal = (level) => {
            for (const value of Object.values(level)) {
                if (typeof value !== "function") {
                    seal(value);
                }
            }
            return freeze(level);
        };
        return seal(root);
    };
    defineProperty(globalThis, "ext", { ...replaceable, value: extensionsOf() });
    extensionTables.rebuild = () => {
        defineProperty(globalThis, "ext", { ...replaceable, value: extensionsOf() });
    };
    const base64Letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const base64Values = new Map(Array.from(base64Letters, (letter, index) => [letter, index]));
    const toBase64 = (bytes) => {
        let text = "";
        for (let at = 0; at < bytes.length; at += 3) {
            const rest = bytes.length - at;
            const triple = (bytes[at] << 16) | ((rest > 1 ? bytes[at + 1] : 0) << 8) | (rest > 2 ? bytes[at + 2] : 0);
            text += base64Letters[(triple >> 18) & 63] + base64Letters[(triple >> 12) & 63] +
                (rest > 1 ? base64Letters[(triple >> 6) & 63] : "=") + (rest > 2 ? base64Letters[triple & 63] : "=");
        }
        return text;
    };
    const fromBase64 = (text) => {
        const letters = typeof text === "string" ? text : "";
        const bytes = new Uint8Array(Math.floor((letters.replace(/=+$/, "").length * 3) / 4));
        let buffer = 0;
        let bits = 0;
        let at = 0;
        for (const letter of letters) {
            const value = base64Values.get(letter);
            if (value === undefined) {
                continue;
            }
            buffer = ((buffer << 6) | value) & 0xFFFFFF;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                if (at < bytes.length) {
                    bytes[at] = (buffer >> bits) & 255;
                }
                at += 1;
            }
        }
        return bytes;
    };
    const utf8Of = (text) => {
        const bytes = [];
        for (const character of text) {
            const code = character.codePointAt(0);
            if (code < 0x80) {
                bytes.push(code);
            } else if (code < 0x800) {
                bytes.push(0xC0 | (code >> 6), 0x80 | (code & 63));
            } else if (code < 0x10000) {
                bytes.push(0xE0 | (code >> 12), 0x80 | ((code >> 6) & 63), 0x80 | (code & 63));
            } else {
                bytes.push(0xF0 | (code >> 18), 0x80 | ((code >> 12) & 63), 0x80 | ((code >> 6) & 63), 0x80 | (code & 63));
            }
        }
        return Uint8Array.from(bytes);
    };
    const hardwareBytes = (value, what) => {
        if (value instanceof Uint8Array) {
            return value;
        }
        if (value instanceof ArrayBuffer) {
            return new Uint8Array(value);
        }
        if (ArrayBuffer.isView(value)) {
            return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
        }
        if (Array.isArray(value) && value.every((each) => Number.isInteger(each) && each >= 0 && each <= 255)) {
            return Uint8Array.from(value);
        }
        throw new TypeError("GS-API-085 " + what + " must be a Uint8Array or a list of bytes from 0 to 255");
    };
    const bluetoothBase = "-0000-1000-8000-00805f9b34fb";
    const hardwareUuid = (value, what) => {
        let text = typeof value === "string" ? value : "";
        if (text.length >= 2 && text.startsWith("{") && text.endsWith("}")) {
            text = text.slice(1, -1);
        }
        if (text.length > 2 && /^0[xX]/.test(text)) {
            text = text.slice(2);
        }
        if (/^[0-9A-Fa-f]{4}$/.test(text)) {
            return "0000" + text.toLowerCase() + bluetoothBase;
        }
        if (/^[0-9A-Fa-f]{8}$/.test(text)) {
            return text.toLowerCase() + bluetoothBase;
        }
        if (/^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$/.test(text)) {
            return text.toLowerCase();
        }
        throw new TypeError("GS-API-085 " + what + " must be a Bluetooth UUID such as \"180d\" or " +
            "\"6e400001-b5a3-f393-e0a9-e50e24dcca9e\"");
    };
    const hardwareText = (value, what) => {
        if (typeof value !== "string" || value === "") {
            throw new TypeError("GS-API-085 " + what + " must be a string that is not empty");
        }
        return value;
    };
    const reportIdOf = (value) => {
        if (!Number.isInteger(value) || value < 0 || value > 255) {
            throw new RangeError("GS-API-085 a report id is a whole number from 0 to 255");
        }
        return value;
    };
    const hardwareCall = (service, value) => host.call(service, JSON.stringify(value));
    const hardwareRequest = (service, value) => host.request(service, JSON.stringify(value));
    const hardwareSettle = (run) => {
        try {
            run().catch(() => undefined);
        } catch {
            return;
        }
    };
    const hardwareDevices = new Map();
    const hardwareClaims = new Map();
    const hardwareResourceOf = (value) => {
        if (typeof value !== "string" || value.length > 260 || !/^(serial|ble|hid|usb):[^\u0000-\u001f]+$/i.test(value)) {
            throw new TypeError("GS-API-085 the resource names hardware as serial:COM9, ble:AA:BB:CC:DD:EE:FF, " +
                "hid: and the device path, or usb:vid:pid:serial number");
        }
        return value;
    };
    const releaseClaim = (name) => {
        const entry = hardwareClaims.get(name);
        if (entry?.listener) {
            entry.listener.active = false;
        }
        hardwareClaims.delete(name);
        hardwareCall("hardware.release", { resource: name });
    };
    const deviceStates = new WeakMap();
    const deviceState = (device) => {
        const state = deviceStates.get(device);
        if (state === undefined) {
            throw new TypeError("Illegal invocation");
        }
        return state;
    };
    const openState = (device) => {
        const state = deviceState(device);
        if (!state.open) {
            throw new RangeError("GS-API-084 the device is not open");
        }
        return state;
    };
    const hardwareListen = (set, handler, options, label) => {
        const listener = makeListener(handler, { again: options?.again ?? "queue" }, label);
        set.add(listener);
        const dispose = () => {
            if (!listener.active) {
                return;
            }
            listener.active = false;
            listener.queued.length = 0;
            set.delete(listener);
        };
        own(dispose);
        return disposable(dispose);
    };
    const hardwareEnded = (state, reason, message) => {
        if (!state.open) {
            return;
        }
        state.open = false;
        hardwareDevices.delete(state.handle);
        if (reason !== undefined) {
            for (const listener of [...state.closes]) {
                react(listener, { reason, message });
            }
)js",
    R"js(        }
        for (const listener of state.closes) {
            listener.active = false;
        }
        state.closes.clear();
    };
    class HardwareDevice {
        constructor(token, state) {
            if (token !== world) {
                throw new TypeError("Illegal constructor");
            }
            deviceStates.set(this, state);
        }
        get id() {
            return deviceState(this).id;
        }
        get kind() {
            return deviceState(this).kind;
        }
        get name() {
            return deviceState(this).name;
        }
        get isOpen() {
            return deviceState(this).open;
        }
        close() {
            const state = deviceState(this);
            if (!state.open) {
                return Promise.resolve();
            }
            hardwareEnded(state);
            return hardwareRequest("hardware.close", { handle: state.handle }).then(() => undefined);
        }
        onClose(handler, options) {
            const state = openState(this);
            return hardwareListen(state.closes, handler, options, state.name + ".onClose");
        }
    }
    class SerialDevice extends HardwareDevice {
        write(data) {
            const state = openState(this);
            const bytes = typeof data === "string" ? utf8Of(data) : hardwareBytes(data, "the data");
            return hardwareRequest("hardware.write", { handle: state.handle, data: toBase64(bytes) }).then(() => undefined);
        }
        setSignals(signals) {
            const state = openState(this);
            if (signals === null || typeof signals !== "object") {
                throw new TypeError("GS-API-085 the signals must be an object such as { dtr: true, rts: false }");
            }
            const ask = { handle: state.handle, op: "signals" };
            for (const line of ["dtr", "rts"]) {
                if (signals[line] === undefined) {
                    continue;
                }
                if (typeof signals[line] !== "boolean") {
                    throw new TypeError("GS-API-085 " + line + " must be true or false");
                }
                ask[line] = signals[line];
            }
            return hardwareRequest("hardware.control", ask).then(() => undefined);
        }
        onData(handler, options) {
            const state = openState(this);
            return hardwareListen(state.data, handler, options, state.name + ".onData");
        }
    }
    class HidDevice extends HardwareDevice {
        sendReport(report, data) {
            const state = openState(this);
            return hardwareRequest("hardware.write", { handle: state.handle, report: reportIdOf(report),
                data: toBase64(hardwareBytes(data, "the report")) }).then(() => undefined);
        }
        sendFeature(report, data) {
            const state = openState(this);
            return hardwareRequest("hardware.control", { handle: state.handle, op: "sendFeature",
                report: reportIdOf(report), data: toBase64(hardwareBytes(data, "the report")) }).then(() => undefined);
        }
        getFeature(report) {
            const state = openState(this);
            return hardwareRequest("hardware.control", { handle: state.handle, op: "getFeature", report: reportIdOf(report) })
                .then((answer) => fromBase64(answer?.data));
        }
        onReport(handler, options) {
            const state = openState(this);
            return hardwareListen(state.data, handler, options, state.name + ".onReport");
        }
    }
    class BleDevice extends HardwareDevice {
        services() {
            const state = openState(this);
            const listOf = (value) => (Array.isArray(value) ? value : []);
            return hardwareRequest("hardware.control", { handle: state.handle, op: "services" }).then((answer) =>
                freeze(listOf(answer).map((service) => freeze({
                    uuid: String(service?.uuid ?? ""),
                    characteristics: freeze(listOf(service?.characteristics).map((each) => freeze({
                        uuid: String(each?.uuid ?? ""),
                        properties: freeze(listOf(each?.properties).map(String)),
                    }))),
                }))));
        }
        read(service, characteristic) {
            const state = openState(this);
            return hardwareRequest("hardware.control", { handle: state.handle, op: "read",
                service: hardwareUuid(service, "the service"),
                characteristic: hardwareUuid(characteristic, "the characteristic") })
                .then((answer) => fromBase64(answer?.data));
        }
        write(service, characteristic, data, options) {
            const state = openState(this);
            if (options !== undefined && (options === null || typeof options !== "object")) {
                throw new TypeError("GS-API-085 the options must be an object such as { response: false }");
            }
            const response = options?.response ?? true;
            if (typeof response !== "boolean") {
                throw new TypeError("GS-API-085 response must be true or false");
            }
            return hardwareRequest("hardware.write", { handle: state.handle,
                service: hardwareUuid(service, "the service"),
                characteristic: hardwareUuid(characteristic, "the characteristic"),
                data: toBase64(hardwareBytes(data, "the value")), response }).then(() => undefined);
        }
        subscribe(service, characteristic, handler, options) {
            const state = openState(this);
            const serviceId = hardwareUuid(service, "the service");
            const characteristicId = hardwareUuid(characteristic, "the characteristic");
            const listener = makeListener(handler, { again: options?.again ?? "queue" }, state.name + ".subscribe");
            const key = serviceId + "/" + characteristicId;
            const ask = (on) => hardwareRequest("hardware.control", { handle: state.handle, op: "subscribe",
                service: serviceId, characteristic: characteristicId, on });
            let set = state.notify.get(key);
            const first = set === undefined;
            if (first) {
                set = new Set();
                state.notify.set(key, set);
            }
            set.add(listener);
            const drop = (turnOff) => {
                if (!listener.active) {
                    return;
                }
                listener.active = false;
                listener.queued.length = 0;
                set.delete(listener);
                if (set.size === 0 && state.notify.get(key) === set) {
                    state.notify.delete(key);
                    if (turnOff && state.open) {
                        hardwareSettle(() => ask(false));
                    }
                }
            };
            const dispose = () => drop(true);
            own(dispose);
            const subscription = disposable(dispose);
            return (first ? ask(true) : Promise.resolve()).then(() => subscription, (error) => {
                drop(false);
                throw error;
            });
        }
    }
    for (const kind of [HardwareDevice, SerialDevice, HidDevice, BleDevice]) {
        defineProperty(kind.prototype, Symbol.toStringTag, { value: kind.name });
        freeze(kind.prototype);
    }
    serviceEvents.set("hardware", (handle, value) => {
        if (value?.type === "claimWanted") {
            const entry = hardwareClaims.get(String(value.resource ?? ""));
            if (entry?.listener) {
                react(entry.listener, { by: String(value.by ?? "") });
            }
            return;
        }
        const state = hardwareDevices.get(handle);
        if (state === undefined || value === null || typeof value !== "object") {
            return;
        }
        if (value.type === "data") {
            for (const listener of [...state.data]) {
                react(listener, { data: fromBase64(value.data) });
            }
        } else if (value.type === "report") {
            for (const listener of [...state.data]) {
                react(listener, { report: value.report, data: fromBase64(value.data) });
            }
        } else if (value.type === "notify") {
            for (const listener of [...(state.notify.get(value.service + "/" + value.characteristic) ?? [])]) {
                react(listener, { service: value.service, characteristic: value.characteristic,
                    data: fromBase64(value.data) });
            }
        } else if (value.type === "closed") {
            hardwareEnded(state, String(value.reason ?? "failed"), String(value.message ?? ""));
        }
    });
    const hardwareKinds = ["serial", "hid", "ble"];
    const hardwareInfos = (list) => freeze((Array.isArray(list) ? list : []).map((entry) => {
        const info = { id: String(entry?.id ?? ""), kind: String(entry?.kind ?? ""), name: String(entry?.name ?? "") };
        if (typeof entry?.present === "boolean") {
            info.present = entry.present;
        }
        return freeze(info);
    }));
    const hardwareMade = (info) => {
        const state = { handle: String(info?.handle ?? ""), id: String(info?.id ?? ""), kind: String(info?.kind ?? ""),
            name: String(info?.name ?? ""), open: true, data: new Set(), closes: new Set(), notify: new Map() };
        hardwareDevices.set(state.handle, state);
        const Kind = state.kind === "hid" ? HidDevice : state.kind === "ble" ? BleDevice : SerialDevice;
        const device = freeze(new Kind(world, state));
        own(() => {
            if (state.open) {
                hardwareSettle(() => device.close());
            }
        });
        return device;
    };
    const inputSources = new Map();
    const inputKeyOf = (key) => {
        if (typeof key !== "string" || !/^[A-Za-z0-9_-]{1,32}$/.test(key)) {
            throw new TypeError("GS-API-085 a key of an input source is 1 to 32 letters, digits, _ or -");
        }
        return key;
    };
    const axisValueOf = (value, what) => {
        if (!(typeof value === "number" && value >= -1 && value <= 1)) {
            throw new RangeError("GS-API-085 " + what + " must be a number from -1 to 1");
        }
        return value;
    };
    class InputSource {
        #name;
        constructor(token, name) {
            if (token !== world) {
                throw new TypeError("Illegal constructor");
            }
            this.#name = name;
        }
        get name() {
            return this.#name;
        }
        press(key) {
            hardwareCall("hardware.input", { source: this.#name, key: inputKeyOf(key), action: "press" });
        }
        release(key) {
            hardwareCall("hardware.input", { source: this.#name, key: inputKeyOf(key), action: "release" });
        }
        axis(key, x, y) {
            const ask = { source: this.#name, key: inputKeyOf(key), action: "axis", x: axisValueOf(x, "x") };
            if (y !== undefined) {
                ask.y = axisValueOf(y, "y");
            }
            hardwareCall("hardware.input", ask);
        }
    }
    defineProperty(InputSource.prototype, Symbol.toStringTag, { value: "InputSource" });
    freeze(InputSource.prototype);
    const hardwareLibrary = {
        "hardware.request": function request(options) {
            if (options === null || typeof options !== "object" || !hardwareKinds.includes(options.kind)) {
                throw new TypeError("GS-API-085 hardware.request() needs { kind: \"serial\" | \"hid\" | \"ble\" }");
            }
            if (options.filters !== undefined && !Array.isArray(options.filters)) {
                throw new TypeError("GS-API-085 filters must be a list of objects");
            }
            return hardwareRequest("hardware.request", { kind: options.kind, filters: options.filters ?? [] })
                .then(hardwareInfos);
        },
        "hardware.list": function list() {
            return hardwareInfos(hardwareCall("hardware.list", {}));
        },
        "hardware.open": function open(id, options) {
            const wanted = hardwareText(id, "the device id");
            if (options !== undefined && options !== null && typeof options !== "object") {
                throw new TypeError("GS-API-085 the options must be an object");
            }
            return hardwareRequest("hardware.open", { id: wanted, options: options ?? {} }).then(hardwareMade);
        },
        "hardware.forget": function forget(id) {
            return hardwareRequest("hardware.forget", { id: hardwareText(id, "the device id") }).then(() => undefined);
        },
        "hardware.claim": function claim(resource, options) {
            const name = hardwareResourceOf(resource);
            if (options !== undefined && options !== null && typeof options !== "object") {
                throw new TypeError("GS-API-085 the options must be an object");
            }
            const onWanted = options?.onWanted;
            if (onWanted !== undefined && typeof onWanted !== "function") {
                throw new TypeError("GS-API-008 the handler must be a function");
            }
            return hardwareRequest("hardware.claim", { resource: name, wait: options?.wait === true,
                yields: onWanted !== undefined }).then((claimed) => {
                if (claimed !== true) {
                    return false;
                }
                const entry = { listener: null };
                if (onWanted !== undefined) {
                    entry.listener = makeListener(async ({ by }) => {
                        if ((await onWanted(by)) === true && hardwareClaims.get(name) === entry) {
                            releaseClaim(name);
                        }
                    }, { again: "ignore" }, "hardware.claim");
                }
                const previous = hardwareClaims.get(name);
                if (previous?.listener) {
                    previous.listener.active = false;
                }
                hardwareClaims.set(name, entry);
                own(() => {
                    if (hardwareClaims.get(name) === entry) {
                        releaseClaim(name);
                    }
                });
                return true;
            });
        },
        "hardware.release": function release(resource) {
            releaseClaim(hardwareResourceOf(resource));
        },
        "input.source": function source(name) {
            if (typeof name !== "string" || !/^[a-z0-9-]{1,32}$/.test(name)) {
                throw new TypeError("GS-API-085 an input source is named with 1 to 32 lowercase letters, digits or -");
            }
            let made = inputSources.get(name);
            if (made === undefined) {
                made = freeze(new InputSource(world, name));
                inputSources.set(name, made);
            }
            return made;
        },
    };
    const intervalOf = (value) => {
        if (!(typeof value === "number" && value >= 1 && value <= 86400000)) {
            throw new RangeError("GS-API-003 the interval must be from 1 to 86400000 milliseconds");
        }
        return value;
    };
    const timed = (ms, handler, options, label, repeat) => {
        const listener = makeListener(handler, options, label);
        const ticker = host.spawn(async ({ signal }) => {
            do {
                await host.sleep(ms);
                react(listener, {});
            } while (repeat && !signal.aborted);
        }, label, {});
        const dispose = () => {
            if (!listener.active) {
                return;
            }
            listener.active = false;
            host.cancel(ticker);
            stopAll(listener);
        };
        own(dispose);
        return disposable(dispose);
    };
    const lookOptions = (options, waitMs, gapMs) => {
)js",
    R"js(        if (options !== undefined && (options === null || typeof options !== "object")) {
            throw new TypeError("GS-API-011 the options must be an object");
        }
        const { intervalMs = gapMs, again, ...find } = options ?? {};
        if (!(typeof intervalMs === "number" && intervalMs >= 50 && intervalMs <= 60000)) {
            throw new RangeError("GS-API-003 intervalMs must be from 50 to 60000 milliseconds");
        }
        if (waitMs === undefined) {
            return { intervalMs, find };
        }
        const { timeoutMs = waitMs, ...rest } = find;
        if (!(typeof timeoutMs === "number" && timeoutMs >= 1 && timeoutMs <= 600000)) {
            throw new RangeError("GS-API-003 timeoutMs must be from 1 to 600000 milliseconds");
        }
        return { timeoutMs, intervalMs, find: rest };
    };
    const lookBefore = (image, find, deadline) =>
        globalThis.vision.find(image, { ...find, timeoutMs: Math.min(10000, Math.max(1, Math.ceil(deadline - now()))) });
    const gapBefore = (intervalMs, deadline) => Math.max(1, Math.min(intervalMs, Math.ceil(deadline - now())));
    const timeoutError = (ms, what) =>
        new DOMException("GS-API-030 " + what + " did not come true within " + ms + " ms", "TimeoutError");
    const started = (each) => (typeof each === "function" ? Promise.resolve().then(each) : each);
    const once = new Set();
    const dones = new Map();
    const timers = new Map();
    const gates = new Map();
    const flips = new Map();
    const multiGates = new Map();
    const appCall = (request, value) => host.app(request, JSON.stringify(value ?? null));
    let changeWatches = 0;
    const capturedImage = (made) => {
        const image = {
            region: freeze({ x: made.x, y: made.y, width: made.width, height: made.height }),
            width: made.pixelWidth,
            height: made.pixelHeight,
            save(name) {
                return host.saveCapture(image, name);
            },
        };
        defineProperty(image, "capture", { value: made.capture });
        defineProperty(image, Symbol.toStringTag, { value: "CapturedImage" });
        return freeze(image);
    };
    const transitionMark = Symbol("transition");
    const callerSite = () => {
        for (const line of String(new Error().stack ?? "").split("\n")) {
            const found = /\(([^()]*):(\d+):\d+\)\s*$/.exec(line);
            if (found !== null && !found[1].startsWith("<") && found[1] !== "gamescrcpy") {
                return { file: found[1], line: Number(found[2]) };
            }
        }
        return { file: "", line: 0 };
    };
    const propOf = (kind, value, options, of) => {
        const result = { kind, value };
        if (typeof options?.label === "string") {
            result.label = options.label;
        }
        if (of !== undefined) {
            result.of = of;
        }
        return freeze(result);
    };
    const paramOf = (kind, value, options) => {
        const result = { kind, value };
        if (typeof options?.label === "string") {
            result.label = options.label;
        }
        if (kind === "number" && typeof options?.min === "number") {
            result.min = options.min;
        }
        if (kind === "number" && typeof options?.max === "number") {
            result.max = options.max;
        }
        return freeze(result);
    };
    const itemsOf = (value, what) => {
        if (!Array.isArray(value)) {
            throw new TypeError("GS-API-011 " + what + " must be a list");
        }
        return value;
    };
    const placeOf = (value, most) => {
        if (!Number.isInteger(value) || value < 0 || value > most) {
            throw new RangeError("GS-API-017 the position must be a whole number from 0 to " + most);
        }
        return value;
    };
    const mapOf = (value, what) => {
        if (!(value instanceof Map)) {
            throw new TypeError("GS-API-011 " + what + " must be a dictionary");
        }
        return value;
    };
    const setOf = (value, what) => {
        if (!(value instanceof Set)) {
            throw new TypeError("GS-API-011 " + what + " must be a set");
        }
        return value;
    };
    const textOf = (value, what) => {
        if (typeof value !== "string") {
            throw new TypeError("GS-API-011 " + what + " must be text");
        }
        return value;
    };
    const sorted = (list) => {
        list.sort((left, right) => {
            if (typeof left === "number" && typeof right === "number") {
                return left - right;
            }
            if (typeof left === "boolean" && typeof right === "boolean") {
                return left === right ? 0 : left ? -1 : 1;
            }
            const one = String(left);
            const two = String(right);
            return one < two ? -1 : one > two ? 1 : 0;
        });
        return list;
    };
    const utf8Text = (bytes) => {
        let text = "";
        for (let at = 0; at < bytes.length;) {
            const first = bytes[at];
            const size = first < 0x80 ? 1 : first >= 0xF8 ? 0 : first >= 0xF0 ? 4 : first >= 0xE0 ? 3 : first >= 0xC0 ? 2 : 0;
            let code = size === 1 ? first : first & (0x7F >> size);
            let good = size > 0 && at + size <= bytes.length;
            for (let index = 1; good && index < size; ++index) {
                good = (bytes[at + index] & 0xC0) === 0x80;
                code = (code << 6) | (bytes[at + index] & 63);
            }
            if (!good || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
                text += "\uFFFD";
                at += 1;
                continue;
            }
            text += String.fromCodePoint(code);
            at += size;
        }
        return text;
    };
    const netServers = new Map();
    const netRequest = (value) => {
        const bytes = fromBase64(value.body);
        let text;
        const read = () => {
            if (text === undefined) {
                text = utf8Text(bytes);
            }
            return text;
        };
        return freeze({
            method: String(value.method ?? ""),
            path: String(value.path ?? ""),
            query: freeze({ ...(value.query ?? {}) }),
            headers: freeze({ ...(value.headers ?? {}) }),
            bytes,
            get text() {
                return read();
            },
            json() {
                return JSON.parse(read());
            },
        });
    };
    const netAnswer = (value) => {
        if (value === undefined || value === null) {
            return { status: 204, headers: {}, body: "" };
        }
        if (typeof value === "string") {
            return { status: 200, headers: { "content-type": "text/plain; charset=utf-8" }, body: toBase64(utf8Of(value)) };
        }
        if (typeof value !== "object") {
            throw new TypeError("GS-API-101 a handler answers with an object, a text or nothing");
        }
        const status = value.status ?? 200;
        if (!Number.isInteger(status) || status < 100 || status > 599) {
            throw new RangeError("GS-API-101 the status of an answer is a whole number from 100 to 599");
        }
        const headers = {};
        for (const [name, given] of Object.entries(value.headers ?? {})) {
            headers[name] = String(given);
        }
        const type = (kind) => {
            if (!Object.keys(headers).some((name) => name.toLowerCase() === "content-type")) {
                headers["content-type"] = kind;
            }
        };
        let bytes = new Uint8Array(0);
        if (value.json !== undefined) {
            bytes = utf8Of(JSON.stringify(value.json));
            type("application/json; charset=utf-8");
        } else if (value.bytes !== undefined) {
            bytes = hardwareBytes(value.bytes, "the bytes of an answer");
            type("application/octet-stream");
        } else if (value.text !== undefined) {
            bytes = utf8Of(String(value.text));
            type("text/plain; charset=utf-8");
        }
        return { status, headers, body: toBase64(bytes) };
    };
    class NetServer {
        #state;
        constructor(token, state) {
            if (token !== world) {
                throw new TypeError("Illegal constructor");
            }
            this.#state = state;
        }
        get port() {
            return this.#state.port;
        }
        get isOpen() {
            return this.#state.open;
        }
        close() {
            const state = this.#state;
            if (!state.open) {
                return;
            }
            state.open = false;
            state.listener.active = false;
            netServers.delete(state.handle);
            host.call("net.close", JSON.stringify({ handle: state.handle }));
        }
    }
    defineProperty(NetServer.prototype, Symbol.toStringTag, { value: "NetServer" });
    freeze(NetServer.prototype);
    serviceEvents.set("net", (handle, value) => {
        const state = netServers.get(handle);
        if (state === undefined || value === null || typeof value !== "object") {
            return;
        }
        if (value.type === "closed") {
            state.open = false;
            state.listener.active = false;
            netServers.delete(handle);
        } else if (value.type === "request") {
            react(state.listener, { id: Number(value.request), request: netRequest(value) });
        }
    });
    const netLibrary = {
        "net.listen": function listen(port, handler, options) {
            if (!Number.isInteger(port) || port < 1024 || port > 65535) {
                throw new RangeError("GS-API-101 the port is a whole number from 1024 to 65535");
            }
            if (typeof handler !== "function") {
                throw new TypeError("GS-API-008 the handler must be a function");
            }
            if (options !== undefined && options !== null && typeof options !== "object") {
                throw new TypeError("GS-API-101 the options must be an object");
            }
            const origins = options?.origins ?? [];
            if (!Array.isArray(origins) || origins.length > 16 || !origins.every((each) => typeof each === "string")) {
                throw new TypeError("GS-API-101 origins is a list of at most 16 texts such as \"https://example.com\"");
            }
            return host.request("net.listen", JSON.stringify({ port, origins })).then((made) => {
                const state = { handle: String(made?.handle ?? ""), port: Number(made?.port ?? port), open: true,
                    listener: null };
                state.listener = makeListener(async ({ id, request }) => {
                    const send = (answer) => host.call("net.respond", JSON.stringify({ handle: state.handle,
                        request: id, ...answer }));
                    let answer;
                    try {
                        answer = netAnswer(await handler(request));
                    } catch (error) {
                        send({ status: 500, headers: {}, body: "" });
                        throw error;
                    }
                    send(answer);
                }, { again: "parallel" }, "net.listen(" + port + ")");
                netServers.set(state.handle, state);
                const server = freeze(new NetServer(world, state));
                own(() => server.close());
                return server;
            });
        },
    };
)js",
    R"js(    const speechListeners = new Map();
    const speechAsk = (options, fields) => {
        if (options !== undefined && options !== null && typeof options !== "object") {
            throw new TypeError("GS-API-111 the options must be an object");
        }
        const ask = {};
        for (const [key, kind, low, high] of fields) {
            const value = options?.[key];
            if (value === undefined) {
                continue;
            }
            if (typeof value !== kind) {
                throw new TypeError("GS-API-111 " + key + " must be a " + kind);
            }
            if (kind === "number" && !(value >= low && value <= high)) {
                throw new RangeError("GS-API-111 " + key + " is from " + low + " to " + high);
            }
            ask[key] = value;
        }
        return ask;
    };
    class SpeechListener {
        #state;
        constructor(token, state) {
            if (token !== world) {
                throw new TypeError("Illegal constructor");
            }
            this.#state = state;
        }
        get isOpen() {
            return this.#state.open;
        }
        close() {
            const state = this.#state;
            if (!state.open) {
                return;
            }
            state.open = false;
            state.listener.active = false;
            speechListeners.delete(state.handle);
            host.call("speech.close", JSON.stringify({ handle: state.handle }));
        }
    }
    defineProperty(SpeechListener.prototype, Symbol.toStringTag, { value: "SpeechListener" });
    freeze(SpeechListener.prototype);
    serviceEvents.set("speech", (handle, value) => {
        const state = speechListeners.get(handle);
        if (state === undefined || value === null || typeof value !== "object") {
            return;
        }
        if (value.type === "closed") {
            state.open = false;
            state.listener.active = false;
            speechListeners.delete(handle);
            if (value.reason !== undefined) {
                globalThis.log.warn("speech.listen stopped listening (" + value.reason + ")" +
                    (value.problem ? ": " + value.problem : ""));
            }
        } else if (value.type === "heard") {
            react(state.listener, { phrase: value.phrase ?? null, text: String(value.text ?? ""),
                confidence: Number(value.confidence ?? 0) });
        }
    });
    const speechLibrary = {
        "speech.say": function say(text, options) {
            if (typeof text !== "string" || text.trim() === "" || text.length > 1000) {
                throw new RangeError("GS-API-111 the text to read is 1 to 1000 characters");
            }
            const ask = { text, ...speechAsk(options, [["engine", "string"], ["voice", "string"], ["rate", "number", 0.5, 2],
                ["volume", "number", 0, 1], ["interrupt", "boolean"]]) };
            return host.request("speech.say", JSON.stringify(ask)).then((finished) => finished === true);
        },
        "speech.stop": function stop() {
            host.call("speech.stop", "{}");
        },
        "speech.engines": function engines() {
            return freeze(host.call("speech.engines", "{}").map((engine) =>
                freeze({ ...engine, languages: freeze([...(engine.languages ?? [])]) })));
        },
        "speech.voices": function voices(options) {
            const ask = speechAsk(options, [["engine", "string"]]);
            return host.request("speech.voices", JSON.stringify(ask)).then((list) =>
                freeze(list.map((voice) => freeze({ ...voice }))));
        },
        "speech.listen": function listen(phrases, handler, options) {
            if (phrases !== null && (!Array.isArray(phrases) || phrases.length < 1 || phrases.length > 256 ||
                !phrases.every((phrase) => typeof phrase === "string" && phrase.trim() !== "" && phrase.trim().length <= 64))) {
                throw new TypeError("GS-API-111 listen for 1 to 256 commands of 1 to 64 characters each, or null");
            }
            if (typeof handler !== "function") {
                throw new TypeError("GS-API-008 the handler must be a function");
            }
            const ask = { phrases, ...speechAsk(options, [["engine", "string"], ["language", "string"],
                ["confidence", "number", 0, 1]]) };
            return host.request("speech.listen", JSON.stringify(ask)).then((made) => {
                const state = { handle: String(made?.handle ?? ""), open: true, listener: null };
                state.listener = makeListener(handler, { again: "queue" }, "speech.listen");
                speechListeners.set(state.handle, state);
                const listener = freeze(new SpeechListener(world, state));
                own(() => listener.close());
                return listener;
            });
        },
    };
)js",
    R"js(    const serviceIdPattern = /^[a-z0-9]+(-[a-z0-9]+)*(\.[a-z0-9]+(-[a-z0-9]+)*)+$/;
    const serviceNamePattern = /^[a-z][A-Za-z0-9]{0,63}$/;
    const serviceHandlers = new Map();
    const serviceTasks = new Map();
    const serviceJson = (value, what) => {
        let json;
        try {
            json = JSON.stringify(value === undefined ? null : value);
        } catch (error) {
            throw new TypeError("GS-API-148 " + what + " must be JSON values: " + (error?.message ?? String(error)));
        }
        if (json === undefined) {
            throw new TypeError("GS-API-148 " + what + " must be JSON values (no functions or undefined)");
        }
        return json;
    };
    function serviceCall(service, args) {
        const slash = typeof service === "string" ? service.lastIndexOf("/") : -1;
        if (slash < 1 || slash > 64 || !serviceIdPattern.test(service.slice(0, slash)) ||
            !serviceNamePattern.test(service.slice(slash + 1))) {
            throw new TypeError("GS-API-146 the service is named \"<extension id>/<service>\", such as " +
                "\"alice.tools/add\"");
        }
        return host.request("ext.call", JSON.stringify({ service, args: serviceJson(args, "the arguments"),
            limit: host.limits() }));
    }
    function serviceProvide(name, handler) {
        if (typeof name !== "string" || !serviceNamePattern.test(name)) {
            throw new TypeError("GS-API-145 the name of a service is 1 to 64 letters and digits that start with a " +
                "lowercase letter, as in contributes.services");
        }
        if (typeof handler !== "function") {
            throw new TypeError("GS-API-008 the handler must be a function");
        }
        host.call("ext.provide", JSON.stringify({ name }));
        const previous = serviceHandlers.get(name);
        if (previous !== undefined) {
            previous.active = false;
        }
        const entry = { handler, active: true };
        serviceHandlers.set(name, entry);
        const dispose = () => {
            if (!entry.active) {
                return;
            }
            entry.active = false;
            serviceHandlers.delete(name);
            host.call("ext.withdraw", JSON.stringify({ name }));
        };
        own(dispose);
        return disposable(dispose);
    }
    defineProperty(serviceCall, "name", { value: "call" });
    defineProperty(serviceProvide, "name", { value: "provide" });
    const serviceSettle = (id, failed, text, output) => host.call(failed ? "ext.fail" : "ext.answer",
        JSON.stringify(failed ? { call: id, message: String(text).slice(0, 1000), output } : { call: id, result: text }));
    serviceEvents.set("extService", (id, value) => {
        if (value === null || typeof value !== "object") {
            return;
        }
        if (value.type === "cancel") {
            const task = serviceTasks.get(id);
            serviceTasks.delete(id);
            if (task !== undefined) {
                host.cancel(task);
            }
            return;
        }
        if (value.type !== "call") {
            return;
        }
        const entry = serviceHandlers.get(value.name);
        if (entry === undefined) {
            serviceSettle(id, true, "GS-API-146 " + String(value.name) + " is not provided now", false);
            return;
        }
        const permissions = freeze(Array.isArray(value.permissions)
            ? value.permissions.filter((each) => typeof each === "string") : []);
        const caller = typeof value.caller === "string" ? value.caller : "";
        let settled = false;
        const task = host.spawn(async ({ signal }) => {
            host.limit(JSON.stringify(permissions));
            let result;
            try {
                result = await entry.handler(value.args, freeze({ caller, permissions, signal }));
            } catch (error) {
                settled = true;
                serviceTasks.delete(id);
                serviceSettle(id, true, error?.message ?? String(error), false);
                throw error;
            }
            settled = true;
            serviceTasks.delete(id);
            let json;
            try {
                json = serviceJson(result, "the result");
            } catch (error) {
                serviceSettle(id, true, error.message, true);
                throw error;
            }
            serviceSettle(id, false, json, false);
        }, "ext.provide(" + String(value.name) + ")", {});
        if (task === 0) {
            serviceSettle(id, true, "GS-API-149 the logic that answers it is stopping", false);
        } else if (!settled) {
            serviceTasks.set(id, task);
        }
    });
    const servicesLibrary = {};
)js",
    R"js(    const library = {
        ...hardwareLibrary,
        ...netLibrary,
        ...speechLibrary,
        ...servicesLibrary,
        every(intervalMs, handler, options) {
            return timed(intervalOf(intervalMs), handler, options, "every(" + intervalMs + ")", true);
        },
        after(delayMs, handler, options) {
            return timed(intervalOf(delayMs), handler, options, "after(" + delayMs + ")", false);
        },
        waitUntil(condition, options) {
            if (typeof condition !== "function") {
                throw new TypeError("GS-API-008 the condition must be a function");
            }
            const { timeoutMs } = untilOptions(options);
            const deadline = now() + timeoutMs;
            return (async () => {
                for (;;) {
                    const value = await condition();
                    if (value) {
                        return value;
                    }
                    if (now() >= deadline) {
                        throw timeoutError(timeoutMs, "the condition");
                    }
                    await nextFrame();
                }
            })();
        },
        retry(times, action) {
            const most = countOf(times, 0, 10);
            if (typeof action !== "function") {
                throw new TypeError("GS-API-008 the action must be a function");
            }
            return (async () => {
                for (let tries = 0; ; tries += 1) {
                    try {
                        return await action(tries);
                    } catch (error) {
                        if (tries >= most || error?.name === "AbortError") {
                            throw error;
                        }
                    }
                }
            })();
        },
        withTimeout(timeoutMs, action) {
            const ms = durationOf(timeoutMs, 0, 600000);
            if (typeof action !== "function") {
                throw new TypeError("GS-API-008 the action must be a function");
            }
            const signal = new AbortSignal(secret);
            const timer = host.spawn(async () => {
                await host.sleep(ms);
                fire(signal, timeoutError(ms, "the action"));
            }, "withTimeout", {});
            const expired = new Promise((resolve, reject) => {
                signal.addEventListener("abort", () => reject(signal.reason));
            });
            return (async () => {
                try {
                    return await Promise.race([Promise.resolve().then(() => action({ signal })), expired]);
                } finally {
                    host.cancel(timer);
                }
            })();
        },
        parallel(...work) {
            return Promise.all(work.map(started));
        },
        sequence(...work) {
            return (async () => {
                const done = [];
                for (const each of work) {
                    done.push(await started(each));
                }
                return done;
            })();
        },
        async multiGate(name, times, loop, random, ...work) {
            const key = nameOf(name);
            const total = countOf(times, 1, 1000);
            const wrap = loop === true;
            const mixed = random === true;
            let entry = multiGates.get(key);
            if (entry === undefined || entry.total !== total || entry.wrap !== wrap || entry.mixed !== mixed) {
                entry = { total, wrap, mixed, next: 0, order: null };
                multiGates.set(key, entry);
            }
            let index = total;
            if (entry.next < total) {
                index = entry.next;
                if (mixed) {
                    if (entry.order === null) {
                        entry.order = [];
                        for (let n = 0; n < total; n += 1) {
                            entry.order.push(n);
                        }
                        for (let n = total - 1; n > 0; n -= 1) {
                            const pick = Math.floor(Math.random() * (n + 1));
                            const swap = entry.order[n];
                            entry.order[n] = entry.order[pick];
                            entry.order[pick] = swap;
                        }
                    }
                    index = entry.order[entry.next];
                }
                entry.next = wrap && entry.next + 1 >= total ? 0 : Math.min(entry.next + 1, total);
                if (entry.next === 0) {
                    entry.order = null;
                }
            }
            const each = work[index];
            return each === undefined ? undefined : Promise.resolve().then(each);
        },
        async machine(states, options) {
            if (states === null || typeof states !== "object") {
                throw new TypeError("GS-API-008 the states must be an object of functions");
            }
            const names = Object.keys(states);
            if (names.length === 0 || names.some((name) => typeof states[name] !== "function")) {
                throw new TypeError("GS-API-008 every state must be a function");
            }
            let state = typeof options?.start === "string" ? options.start : names[0];
            if (!names.includes(state)) {
                throw new RangeError("GS-API-002 the start state " + state + " is not one of the states");
            }
            const site = callerSite();
            const name = typeof options?.name === "string" ? options.name : "";
            const report = (current, reason) =>
                appCall("machineState", { file: site.file, line: site.line, name, state: current, reason });
            const to = freeze(Object.fromEntries(names.map((next) => [next, (reason) =>
                freeze({ [transitionMark]: true, state: next, reason: reason === undefined ? "" : String(reason) })])));
            let reason = "";
            try {
                for (;;) {
                    report(state, reason);
                    const next = await states[state].call(states, to);
                    if (next === undefined || next === null) {
                        return;
                    }
                    if (next?.[transitionMark] !== true) {
                        throw new TypeError("GS-API-008 a state returns to.<state>(reason) or nothing");
                    }
                    state = next.state;
                    reason = next.reason;
                }
            } finally {
                report("", "");
            }
        },
        race(...work) {
            return Promise.race(work.map(started));
        },
        doOnce(name, action) {
            if (typeof action !== "function") {
                throw new TypeError("GS-API-008 the action must be a function");
            }
            if (once.has(nameOf(name))) {
                return undefined;
            }
            once.add(name);
            return action();
        },
        resetOnce(name) {
            once.delete(nameOf(name));
        },
        doN(name, times) {
            const key = nameOf(name);
            const most = countOf(times, 1, 1000000);
            const used = dones.get(key) ?? 0;
            if (used >= most) {
                return false;
            }
            dones.set(key, used + 1);
            return true;
        },
        resetDoN(name) {
            dones.delete(nameOf(name));
        },
        gate(name, startClosed) {
            let entry = gates.get(nameOf(name));
            if (entry === undefined) {
                entry = freeze(new Gate(world, !startClosed));
                gates.set(name, entry);
            }
            return entry;
        },
        flipFlop(name) {
            const next = !(flips.get(nameOf(name)) ?? false);
            flips.set(name, next);
            return next ? "A" : "B";
        },
        throttle(intervalMs, action) {
            const ms = intervalOf(intervalMs);
            if (typeof action !== "function") {
                throw new TypeError("GS-API-008 the action must be a function");
            }
            let last = -Infinity;
            return (...parts) => {
                const at = now();
                if (at - last < ms) {
                    return undefined;
                }
                last = at;
                return action(...parts);
            };
        },
        debounce(waitMs, action) {
            const ms = intervalOf(waitMs);
            if (typeof action !== "function") {
                throw new TypeError("GS-API-008 the action must be a function");
            }
            let timer = 0;
            return (...parts) => {
                if (timer !== 0) {
                    host.cancel(timer);
                }
                timer = host.spawn(async () => {
                    await host.sleep(ms);
                    timer = 0;
                    await action(...parts);
                }, "debounce", {});
            };
        },
        stopwatch() {
            return freeze(new Stopwatch(world));
        },
        point(x, y) {
            return freeze({ x: ratioOf(x, "x"), y: ratioOf(y, "y") });
        },
        "math.clamp": (value, low, high) => Math.min(Math.max(numberOf(value, "value"), numberOf(low, "low")),
            numberOf(high, "high")),
        "math.lerp": (from, to, t) => numberOf(from, "from") + (numberOf(to, "to") - from) * numberOf(t, "t"),
        "math.random": (low, high) => {
            const from = low === undefined ? 0 : numberOf(low, "low");
            const to = high === undefined ? 1 : numberOf(high, "high");
            return from + Math.random() * (to - from);
        },
        "math.randomInt": (low, high) => {
            const from = Math.ceil(numberOf(low, "low"));
            const to = Math.floor(numberOf(high, "high"));
            return from + Math.floor(Math.random() * (to - from + 1));
        },
        "math.randomIn": (region) => {
            const [x, y] = pointOf(region?.x, region?.y);
            const width = numberOf(region.width, "width");
            const height = numberOf(region.height, "height");
            return freeze({ x: x + Math.random() * width, y: y + Math.random() * height });
        },
        "math.distance": (from, to) =>
            Math.hypot(numberOf(to?.x, "to.x") - numberOf(from?.x, "from.x"), numberOf(to?.y, "to.y") - numberOf(from?.y, "from.y")),
        "math.angle": (from, to) =>
            degreesOf({ x: numberOf(to?.x, "to.x") - numberOf(from?.x, "from.x"), y: numberOf(to?.y, "to.y") - numberOf(from?.y, "from.y") }) ?? 0,
        "math.round": (value) => Math.round(numberOf(value, "value")),
        "math.floor": (value) => Math.floor(numberOf(value, "value")),
        "math.ceil": (value) => Math.ceil(numberOf(value, "value")),
        "math.abs": (value) => Math.abs(numberOf(value, "value")),
        "math.min": (a, b) => Math.min(numberOf(a, "a"), numberOf(b, "b")),
        "math.max": (a, b) => Math.max(numberOf(a, "a"), numberOf(b, "b")),
)js",
    R"js(        "list.add": (items, value) => {
            itemsOf(items, "the list").push(value);
        },
        "list.insert": (items, index, value) => {
            const list = itemsOf(items, "the list");
            list.splice(placeOf(index, list.length), 0, value);
        },
        "list.removeAt": (items, index) => {
            const list = itemsOf(items, "the list");
            return list.splice(placeOf(index, list.length - 1), 1)[0];
        },
        "list.remove": (items, value) => {
            const list = itemsOf(items, "the list");
            const at = list.indexOf(value);
            if (at < 0) {
                return false;
            }
            list.splice(at, 1);
            return true;
        },
        "list.indexOf": (items, value) => itemsOf(items, "the list").indexOf(value),
        "list.at": (items, index) => itemsOf(items, "the list")[index],
        "list.setAt": (items, index, value) => {
            const list = itemsOf(items, "the list");
            list[placeOf(index, list.length - 1)] = value;
        },
        "list.size": (items) => itemsOf(items, "the list").length,
        "list.has": (items, value) => itemsOf(items, "the list").includes(value),
        "list.sort": (items) => {
            sorted(itemsOf(items, "the list"));
        },
        "list.shuffle": (items) => {
            const list = itemsOf(items, "the list");
            for (let at = list.length - 1; at > 0; at -= 1) {
                const pick = Math.floor(Math.random() * (at + 1));
                const swap = list[at];
                list[at] = list[pick];
                list[pick] = swap;
            }
        },
        "list.clear": (items) => {
            itemsOf(items, "the list").length = 0;
        },
        "dict.make": () => new Map(),
        "dict.set": (map, key, value) => {
            mapOf(map, "the dictionary").set(key, value);
        },
        "dict.get": (map, key) => mapOf(map, "the dictionary").get(key),
        "dict.has": (map, key) => mapOf(map, "the dictionary").has(key),
        "dict.remove": (map, key) => mapOf(map, "the dictionary").delete(key),
        "dict.keys": (map) => Array.from(mapOf(map, "the dictionary").keys()),
        "dict.values": (map) => Array.from(mapOf(map, "the dictionary").values()),
        "dict.size": (map) => mapOf(map, "the dictionary").size,
        "dict.clear": (map) => {
            mapOf(map, "the dictionary").clear();
        },
        "set.make": () => new Set(),
        "set.add": (items, value) => {
            setOf(items, "the set").add(value);
        },
        "set.has": (items, value) => setOf(items, "the set").has(value),
        "set.remove": (items, value) => setOf(items, "the set").delete(value),
        "set.values": (items) => Array.from(setOf(items, "the set").values()),
        "set.size": (items) => setOf(items, "the set").size,
        "set.clear": (items) => {
            setOf(items, "the set").clear();
        },
        "text.join": (parts, separator) =>
            itemsOf(parts, "the parts").join(separator === undefined ? "" : textOf(separator, "the separator")),
        "text.format": (template, ...values) =>
            textOf(template, "the template").replace(/\{(\d+)\}/g, (whole, number) => {
                const pick = Number(number);
                return pick < values.length ? String(values[pick]) : whole;
            }),
        "text.find": (value, part) => textOf(value, "the text").indexOf(textOf(part, "what to find")),
        "text.has": (value, part) => textOf(value, "the text").includes(textOf(part, "what to find")),
        "text.split": (value, separator) => textOf(value, "the text").split(textOf(separator, "the separator")),
        "text.replace": (value, part, instead) =>
            textOf(value, "the text").replace(textOf(part, "what to find"), () => textOf(instead, "the new text")),
        "text.upper": (value) => textOf(value, "the text").toUpperCase(),
        "text.lower": (value) => textOf(value, "the text").toLowerCase(),
        "text.trim": (value) => textOf(value, "the text").trim(),
        "text.length": (value) => textOf(value, "the text").length,
)js",
    R"js(        "touch.tap": function tap(first, second, third) {
            const [x, y] = pointOf(first, second);
            const ms = first !== null && typeof first === "object" ? second ?? third : third;
            return act(() => host.tap(x, y, ms));
        },
        "touch.longPress": function longPress(first, second, third) {
            const [x, y] = pointOf(first, second);
            const given = first !== null && typeof first === "object" ? second ?? third : third;
            return act(() => host.tap(x, y, given === undefined ? 800 : given));
        },
        "touch.swipe": function swipe(first, second, third, fourth, fifth) {
            const objects = first !== null && typeof first === "object";
            const spaced = objects && second === undefined && third !== undefined;
            const [fromX, fromY] = pointOf(first, second);
            const [toX, toY] = spaced || !objects ? pointOf(third, fourth) : pointOf(second);
            const ms = objects && !spaced ? third : fifth;
            return act(() => host.swipe(fromX, fromY, toX, toY, ms));
        },
        "touch.down": function down(first, second) {
            const [x, y] = pointOf(first, second);
            const finger = freeze(new Finger(world, host.fingerDown(x, y)));
            own(() => finger.up());
            return finger;
        },
        "touch.pinch": function pinch(center, from, to, durationMs) {
            const [x, y] = pointOf(center);
            const spread = (value) => {
                if (!(typeof value === "number" && value >= 0 && value <= 1)) {
                    throw new RangeError("GS-API-002 the distance between the fingers must be from 0 to 1");
                }
                return value / 2;
            };
            const start = spread(from);
            const end = spread(to);
            const ms = durationOf(durationMs, 300);
            const edge = (value) => Math.min(1, Math.max(0, value));
            return act(async () => {
                const left = freeze(new Finger(world, host.fingerDown(edge(x - start), y)));
                const right = freeze(new Finger(world, host.fingerDown(edge(x + start), y)));
                own(() => {
                    left.up();
                    right.up();
                });
)js",
    R"js(                await Promise.all([left.move(edge(x - end), y, ms), right.move(edge(x + end), y, ms)]);
                left.up();
                right.up();
            });
        },
        "touch.releaseAll": function releaseAll() {
            host.liftAll();
        },
        "mapping.get": function get(id) {
            return typeof id === "string" && Object.hasOwn(controlsTable, id) ? controlsTable[id] : null;
        },
        "mapping.all": function all(type) {
            const list = Object.values(controlsTable).filter((control) => control !== null);
            if (type === undefined) {
                return list;
            }
            if (type instanceof Interface || (typeof type === "function" && type.prototype instanceof Component)) {
                return list.filter((control) => fits(control, type));
            }
            if (typeof type !== "string" || !Object.hasOwn(kinds, type)) {
                throw new RangeError("GS-API-017 the type must be one of " + Object.keys(kinds).join(", ") +
                    ", a component class or an interface");
            }
            return list.filter((control) => control.type === type);
        },
        "mapping.on": () => appCall("keymap", true),
        "mapping.off": () => appCall("keymap", false),
        "mapping.toggle": () => appCall("keymap", "toggle"),
        "mapping.isOn": () => appCall("keymap", null),
        "hints.show": () => appCall("hints", { visible: true }),
        "hints.hide": () => appCall("hints", { visible: false }),
        "hints.toggle": () => appCall("hints", { visible: "toggle" }),
        "hints.setOpacity": (opacity) => {
            if (!(typeof opacity === "number" && opacity >= 0 && opacity <= 1)) {
                throw new RangeError("GS-API-017 the opacity must be from 0 to 1");
            }
            return appCall("hints", { opacity });
        },
        "profile.current": () => appCall("profile", null),
        "profile.switchTo": (name) => appCall("profile", { to: nameOf(name) }),
        "profile.next": () => appCall("profile", { step: 1 }),
        "profile.previous": () => appCall("profile", { step: -1 }),
        "device.launch": (name, options) => appCall("launch", { package: nameOf(name), restart: Boolean(options?.restart) }),
        "device.screenOff": () => appCall("screen", false),
        "device.screenOn": () => appCall("screen", true),
        "device.foregroundApp": () => appCall("foreground", null),
        "device.onForegroundApp": (handler, options) =>
            listen("app", "foreground", handler, options, "device.onForegroundApp"),
        "device.onRotate": (handler, options) => listen("app", "rotation", handler, options, "device.onRotate"),
        "device.connected": () => host.connected(),
        "device.onDisconnect": (handler, options) =>
            listen("app", "disconnect", handler, options, "device.onDisconnect"),
        "device.onReconnect": (handler, options) => listen("app", "reconnect", handler, options, "device.onReconnect"),
        "device.onInputBox": (handler, options) => listen("app", "inputBox", handler, options, "device.onInputBox"),
        "device.rotate": (orientation) => {
            const mode = ["auto", "portrait", "landscape"].indexOf(orientation);
            if (mode < 0) {
                throw new RangeError("GS-API-017 the orientation must be \"auto\", \"portrait\" or \"landscape\"");
            }
            return appCall("rotate", mode);
        },
        "device.notifications": () => host.call("panel.show", JSON.stringify({ kind: "notifications" })),
        "device.quickSettings": () => host.call("panel.show", JSON.stringify({ kind: "settings" })),
        "device.collapsePanels": () => host.call("panel.show", JSON.stringify({ kind: "collapse" })),
        "window.focused": () => appCall("focused", null) === true,
        "window.onFocus": (handler, options) => listen("app", "focus", handler, options, "window.onFocus"),
        "window.onBlur": (handler, options) => listen("app", "blur", handler, options, "window.onBlur"),
        "events.on": (topic, handler, options) => {
            if (typeof topic !== "string") {
                throw new TypeError("GS-API-068 the topic must be text");
            }
            const colon = topic.indexOf(":");
            const from = colon < 0 ? "" : topic.slice(0, colon);
            if (!/^[a-z0-9.-]{1,64}$/.test(colon < 0 ? topic : topic.slice(colon + 1)) ||
                (colon >= 0 && (from.length > 64 || !serviceIdPattern.test(from)))) {
                throw new RangeError("GS-API-068 the topic must be 1 to 64 lowercase letters, digits, dots and dashes, " +
                    "alone or after the id of an extension this one depends on and a colon");
            }
            return listen("extEvent", topic, handler, options, "events.on(" + topic + ")");
        },
        "ui.progress": (title, value) => {
            if (title === null || title === undefined) {
                appCall("progress", null);
                return;
            }
            if (!(typeof value === "number" && value >= 0 && value <= 1)) {
                throw new RangeError("GS-API-017 the progress must be from 0 to 1");
            }
            appCall("progress", { title: String(title).slice(0, 80), value });
        },
        "ui.notify": (title, text) =>
            appCall("notify", { title: String(title ?? "").slice(0, 64), text: String(text ?? "").slice(0, 256) }) ===
            true,
        "log.time": (name) => {
            timers.set(nameOf(name), now());
        },
        "log.timeEnd": (name) => {
            const started = timers.get(nameOf(name));
            if (started === undefined) {
                globalThis.log.warn(name + ": log.time() was not called with this name");
                return null;
            }
            timers.delete(name);
            const ms = Math.round(now() - started);
            globalThis.log.info(name + ": " + ms + " ms");
            return ms;
        },
        "vision.waitFor": (image, options) => {
            const { timeoutMs, intervalMs, find } = lookOptions(options, 10000, 250);
            const deadline = now() + timeoutMs;
            const first = lookBefore(image, find, deadline);
            return (async () => {
                let found = await first;
                while (found === null && now() < deadline) {
                    await host.sleep(gapBefore(intervalMs, deadline));
                    found = await lookBefore(image, find, deadline);
                }
                return found;
            })();
        },
        "vision.waitGone": (image, options) => {
            const { timeoutMs, intervalMs, find } = lookOptions(options, 10000, 250);
            const deadline = now() + timeoutMs;
            const first = lookBefore(image, find, deadline);
            return (async () => {
                let found = await first;
                while (found !== null && now() < deadline) {
                    await host.sleep(gapBefore(intervalMs, deadline));
                    found = await lookBefore(image, find, deadline);
                }
                return found === null;
            })();
        },
        "vision.findAndTap": (image, options) => {
            const look = globalThis.vision.find(image, options);
            return (async () => {
                const found = await look;
                if (found !== null) {
                    await host.tap(found.x, found.y, undefined);
                }
                return found;
            })();
        },
        "vision.watch": (image, onAppear, onGone, options) => {
            const appear = makeListener(onAppear, options, "vision.watch");
            const gone = onGone === undefined || onGone === null ? null : makeListener(onGone, options, "vision.watch");
            const { intervalMs, find } = lookOptions(options, undefined, 500);
            let look = globalThis.vision.find(image, find);
            let shown = false;
            const looker = host.spawn(async ({ signal }) => {
                for (;;) {
                    const found = await look;
                    if (found !== null && !shown) {
                        shown = true;
                        react(appear, found);
                    } else if (found === null && shown) {
                        shown = false;
                        if (gone !== null) {
                            react(gone, {});
                        }
                    }
                    await host.sleep(intervalMs);
                    if (signal.aborted) {
                        return;
                    }
                    look = globalThis.vision.find(image, find);
                }
            }, "vision.watch", {});
            const dispose = () => {
                if (!appear.active) {
                    return;
                }
                appear.active = false;
                host.cancel(looker);
                stopAll(appear);
                if (gone !== null) {
                    gone.active = false;
                    stopAll(gone);
                }
            };
            own(dispose);
            return disposable(dispose);
        },
        "vision.capture": (region, options) =>
            host.capture(region, options).then((made) => (made === null ? null : capturedImage(made))),
        "vision.onChange": (region, handler, options) => {
            const listener = makeListener(handler, options, "vision.onChange");
            const { intervalMs, find } = lookOptions(options, undefined, 500);
            const key = "onChange " + ++changeWatches;
            const look = () => host.changed(key, region, { threshold: find.threshold });
            let pending = look();
            const looker = host.spawn(async ({ signal }) => {
                for (;;) {
                    if (await pending) {
                        react(listener, {});
                    }
                    await host.sleep(intervalMs);
                    if (signal.aborted) {
                        return;
                    }
                    pending = look();
                }
            }, "vision.onChange", {});
            const dispose = () => {
                if (!listener.active) {
                    return;
                }
                listener.active = false;
                host.cancel(looker);
                stopAll(listener);
            };
            own(dispose);
            return disposable(dispose);
        },
        "param.number": (value, options) => {
            if (!(typeof value === "number" && Number.isFinite(value))) {
                throw new TypeError("GS-API-034 param.number needs a finite number");
            }
            return paramOf("number", value, options);
        },
        "param.text": (value, options) => {
            if (typeof value !== "string") {
                throw new TypeError("GS-API-034 param.text needs a string");
            }
            return paramOf("text", value, options);
        },
        "prop.key": (value, options) => {
            if (value !== undefined && value !== null && typeof value !== "string") {
                throw new TypeError("GS-API-034 prop.key needs a key name such as \"F\"");
            }
            return propOf("key", value ?? null, options);
        },
        "prop.position": (value, options) => {
            if (value !== undefined && value !== null) {
                if (Array.isArray(value)) {
                    pointOf(value[0], value[1]);
                } else {
                    pointOf(value);
                }
            }
            return propOf("position", value ?? null, options);
        },
        "prop.number": (value, options) => {
            if (!(typeof value === "number" && Number.isFinite(value))) {
                throw new TypeError("GS-API-034 prop.number needs a finite number");
            }
            return propOf("number", value, options);
        },
        "prop.text": (value, options) => {
            if (typeof value !== "string") {
                throw new TypeError("GS-API-034 prop.text needs a string");
            }
            return propOf("text", value, options);
        },
        "prop.toggle": (value, options) => {
            if (typeof value !== "boolean") {
                throw new TypeError("GS-API-034 prop.toggle needs true or false");
            }
            return propOf("toggle", value, options);
        },
        "prop.component": (value, options) => {
            if (value instanceof Interface || (typeof value === "function" &&
                (value === Component || value.prototype instanceof Component))) {
                return propOf("component", null, options, value);
            }
            if (value !== undefined && value !== null && typeof value !== "string") {
                throw new TypeError("GS-API-034 prop.component needs a component id, a component class or an interface");
            }
            return propOf("component", value ?? null, options);
        },
        "param.toggle": (value, options) => {
            if (typeof value !== "boolean") {
                throw new TypeError("GS-API-034 param.toggle needs true or false");
            }
            return paramOf("toggle", value, options);
        },
    };

    class Task {
        #params;

        constructor() {
            const declared = this.constructor.params ?? {};
            const given = appCall("taskParams", null);
            const values = Object.create(null);
            for (const [name, spec] of Object.entries(declared)) {
                const fallback = spec !== null && typeof spec === "object" && "value" in spec ? spec.value : spec;
                const chosen = given !== null && typeof given === "object" ? given[name] : undefined;
                values[name] = chosen !== undefined && typeof chosen === typeof fallback ? chosen : fallback;
            }
            this.#params = freeze(values);
        }

        get params() {
            return this.#params;
        }

        progress(text) {
            appCall("taskProgress", String(text));
        }

        async run() {
            throw new TypeError("GS-API-035 a task needs its own run()");
        }
    }
    defineProperty(Task.prototype, Symbol.toStringTag, { value: "Task" });
    freeze(Task.prototype);
    defineProperty(globalThis, "Task", { ...replaceable, value: Task });
    defineProperty(globalThis, "machine", { ...replaceable, value: library.machine });

    const drawings = new Map();
    const drawn = (id, durationMs) => {
        if (drawings.size >= 64) {
)js",
    R"js(            throw new RangeError("GS-API-010 at most 64 drawings at a time; dispose the ones you no longer need");
        }
        let timer = 0;
        const dispose = () => {
            if (!drawings.has(id)) {
                return;
            }
            drawings.delete(id);
            if (timer !== 0) {
                const running = timer;
                timer = 0;
                host.cancel(running);
            }
            host.erase(id);
        };
        drawings.set(id, dispose);
        own(dispose);
        if (durationMs > 0) {
            timer = host.spawn(async () => {
                await host.sleep(durationMs);
                timer = 0;
                dispose();
            }, "drawing", {});
        }
        return disposable(dispose);
    };

    const mcpHandlers = new Map();
    const mcpCalls = new Map();
    const mcpAnswer = (call, ok, value) => {
        let text;
        try {
            text = JSON.stringify(ok ? { call, ok: true, value: value === undefined ? null : value }
                : { call, ok: false, error: String(value) });
        } catch {
            text = JSON.stringify({ call, ok: false, error: "GS-API-074 the result of the tool is not a JSON value" });
        }
        try {
            host.call("mcp.result", text);
        } catch (error) {
            Promise.reject(error);
        }
    };
    serviceEvents.set("mcp", (id, value) => {
        const call = String(id);
        const asked = value !== null && typeof value === "object" ? value : {};
        if (asked.cancel === true) {
            const running = mcpCalls.get(call);
            if (running !== undefined) {
                mcpCalls.delete(call);
                host.cancel(running);
            }
            return;
        }
        const handler = mcpHandlers.get(asked.tool);
        if (handler === undefined) {
            mcpAnswer(call, false, "GS-API-074 the tool " + String(asked.tool) + " is not offered any more");
            return;
        }
        const input = asked.arguments !== null && typeof asked.arguments === "object" ? asked.arguments : {};
        const running = host.spawn(async (event) => {
            try {
                const result = await handler(input, event);
                if (mcpCalls.delete(call)) {
                    mcpAnswer(call, true, result);
                }
            } catch (error) {
                if (mcpCalls.delete(call)) {
                    mcpAnswer(call, false, error?.message ?? error);
                }
            }
        }, "mcp." + asked.tool, {});
        if (running === 0) {
            mcpAnswer(call, false, "GS-API-074 the tool could not start");
            return;
        }
        mcpCalls.set(call, running);
    });
    library["mcp.tool"] = function tool(name, spec, handler) {
        if (typeof name !== "string" || !/^[a-z][a-z0-9_]{0,31}$/.test(name)) {
            throw new TypeError("GS-API-074 the tool name must be a lowercase letter, then up to 31 lowercase letters, "
                + "digits and underscores");
        }
        if (spec === null || typeof spec !== "object" || typeof spec.description !== "string"
            || spec.description.trim() === "") {
            throw new TypeError("GS-API-074 the tool needs a description");
        }
        if (typeof handler !== "function") {
            throw new TypeError("GS-API-008 the handler must be a function");
        }
        const input = spec.input === undefined ? { type: "object" } : spec.input;
        if (input === null || typeof input !== "object" || Array.isArray(input)) {
            throw new TypeError("GS-API-074 the input schema must be an object");
        }
        host.call("mcp.register", JSON.stringify({ name, description: spec.description, input }));
        mcpHandlers.set(name, handler);
        let active = true;
        const dispose = () => {
            if (!active) {
                return;
            }
            active = false;
            if (mcpHandlers.get(name) !== handler) {
                return;
            }
            mcpHandlers.delete(name);
            try {
                host.call("mcp.unregister", JSON.stringify({ name }));
            } catch (error) {
                Promise.reject(error);
            }
        };
        own(dispose);
        return disposable(dispose);
    };
    const uiEventTypes = ["click", "change", "submit", "select", "open", "close"];
    const uiSend = (service, message) => {
        let json;
        try {
            json = JSON.stringify(message);
        } catch {
            throw new TypeError("GS-API-075 what a panel shows must be plain data that JSON can hold");
        }
        return host.call(service, json);
    };
    const uiListen = (key, type, handler, options, label, types) => {
        if (!types.includes(type)) {
            throw new RangeError("GS-API-017 the event must be one of " + types.join(", "));
        }
        return listen("ui", key + ":" + type, handler, options, label);
    };
    serviceEvents.set("ui", (id, value) => {
        if (value === null || typeof value !== "object" || typeof value.type !== "string") {
            return;
        }
        const event = { id: typeof value.id === "string" ? value.id : "",
            value: value.value === undefined ? null : value.value };
        if (value.target !== null && typeof value.target === "object") {
            event.target = freeze({ kind: String(value.target.kind ?? ""), id: String(value.target.id ?? ""),
                name: String(value.target.name ?? "") });
        }
        const key = value.type === "click" && event.id === "" ? "toolbar:" + id : id;
        reactAll("ui:" + key + ":" + value.type, event);
    });
    const uiPanels = new Map();
    library["ui.panel"] = (name) => {
        const panel = nameOf(name);
        if (!uiPanels.has(panel)) {
            const made = {
                render(tree) {
                    if (tree === undefined) {
                        throw new TypeError("GS-API-075 render() needs a tree; pass null to empty the panel");
                    }
                    uiSend("ui.render", { panel, tree });
                },
                on(type, handler, options) {
                    return uiListen(panel, type, handler, options, "ui.panel(" + panel + ").on", uiEventTypes);
                },
                close() {
                    uiSend("ui.close", { panel });
                },
            };
            defineProperty(made, Symbol.toStringTag, { value: "UiPanel" });
            uiPanels.set(panel, freeze(made));
        }
        return uiPanels.get(panel);
    };
    const uiMenu = function menu(items) {
        if (!Array.isArray(items)) {
            throw new TypeError("GS-API-075 the menu items must be a list");
        }
        uiSend("ui.menu", { items });
    };
    defineProperty(uiMenu, "on", {
        value: function on(type, handler, options) {
            return uiListen("menu", type, handler, options, "ui.menu.on", ["select"]);
        },
    });
    library["ui.menu"] = uiMenu;
    const uiStatuses = new Map();
    library["ui.status"] = (name) => {
        const widget = nameOf(name);
        if (!uiStatuses.has(widget)) {
            const made = {
                render(tree) {
                    if (tree === undefined) {
                        throw new TypeError("GS-API-075 render() needs a tree; pass null to take the widget away");
                    }
                    uiSend("ui.status", { name: widget, tree });
                },
            };
            defineProperty(made, Symbol.toStringTag, { value: "UiStatus" });
            uiStatuses.set(widget, freeze(made));
        }
        return uiStatuses.get(widget);
    };
    const uiToolbars = new Map();
    library["ui.toolbar"] = (name) => {
        const button = nameOf(name);
        if (!uiToolbars.has(button)) {
            const made = {
                set(state) {
                    if (state === undefined) {
                        throw new TypeError("GS-API-075 set() needs what the button shows; pass null to show what "
                            + "the manifest declares");
                    }
                    uiSend("ui.toolbar", { name: button, state });
                },
                on(type, handler, options) {
                    return uiListen("toolbar:" + button, type, handler, options, "ui.toolbar(" + button + ").on",
                        ["click"]);
                },
            };
            defineProperty(made, Symbol.toStringTag, { value: "UiToolbar" });
            uiToolbars.set(button, freeze(made));
        }
        return uiToolbars.get(button);
    };
    const uiNamedMenu = (service, tag, label) => {
        const made = new Map();
        return (name) => {
            const menu = nameOf(name);
            if (!made.has(menu)) {
                const group = {
                    set(items) {
                        if (items !== null && !Array.isArray(items)) {
                            throw new TypeError("GS-API-075 the menu items must be a list, or null to take them away");
                        }
                        uiSend(service, { name: menu, items });
                    },
                    on(type, handler, options) {
                        return uiListen(menu, type, handler, options, label + "(" + menu + ").on", ["choose"]);
                    },
                };
                defineProperty(group, Symbol.toStringTag, { value: tag });
                made.set(menu, freeze(group));
            }
            return made.get(menu);
        };
    };
    library["ui.contextMenu"] = uiNamedMenu("ui.contextMenu", "UiContextMenu", "ui.contextMenu");
    library["ui.tray"] = uiNamedMenu("ui.tray", "UiTray", "ui.tray");
    const aiPicture = (picture) => {
        if (picture === undefined || picture === false) {
            return { picture: false, region: null };
        }
        if (picture === true) {
            return { picture: true, region: null };
        }
        if (typeof picture === "string") {
            const named = globalThis.res.region(picture);
            return { picture: true, region: { x: named.x, y: named.y, width: named.width, height: named.height } };
        }
        if (picture === null || typeof picture !== "object") {
            throw new TypeError("GS-API-011 picture must be true, false, a region or the name of a region");
        }
        const region = { x: ratioOf(picture.x, "x"), y: ratioOf(picture.y, "y"), width: ratioOf(picture.width, "width"),
            height: ratioOf(picture.height, "height") };
        if (region.x + region.width > 1 + 1e-9 || region.y + region.height > 1 + 1e-9) {
            throw new RangeError("GS-API-002 the region must be an object inside the picture");
        }
        return { picture: true, region };
    };
    library["ai.ask"] = (question, options) => {
        if (typeof question !== "string" || question.trim() === "" || question.length > 4000) {
            throw new TypeError("GS-API-011 the question must be text of 1 to 4000 characters that are not all spaces");
        }
        if (options !== undefined && (options === null || typeof options !== "object")) {
            throw new TypeError("GS-API-011 the options must be an object");
        }
        const timeoutMs = options?.timeoutMs === undefined ? 60000 : options.timeoutMs;
        if (!(typeof timeoutMs === "number" && timeoutMs >= 1000 && timeoutMs <= 120000)) {
            throw new RangeError("GS-API-003 timeoutMs must be from 1000 to 120000 milliseconds");
        }
        const { picture, region } = aiPicture(options?.picture);
        return host.request("ai.ask", JSON.stringify({ question, picture, region, timeoutMs }))
            .then((answer) => (typeof answer === "string" ? answer : null));
    };
    library["hints.reveal"] = (on) => appCall("hints", { reveal: Boolean(on) });

    return freeze({
        task: (extra) => freeze({ ...extra, signal: new AbortSignal(secret) }),
        abort: (signal) => fire(signal, cancelled()),
        cancelled,
        dispatch,
        ended,
        drawn,
        action: actionOf,
        library: freeze(library),
        mount: mountComponents,
        startTask: async (context) => {
            await new context.taskClass().run(freeze({ signal: context.signal }));
        },
        release: (id) => {
            const binding = bindings.get(id);
            if (binding !== undefined) {
                unbind(binding, false);
            }
        },
    });
});
)js",
};

[[nodiscard]] inline const std::string& prelude()
{
    static const std::string joined = [] {
        std::string text;
        for (const std::string_view part : kPreludeParts) {
            text += part;
        }
        return text;
    }();
    return joined;
}

}
