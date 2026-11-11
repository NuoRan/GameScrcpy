.pragma library

const esp32 = "gamescrcpy.esp32"

function capabilities(entry) {
    const found = []
    const add = name => {
        if (found.indexOf(name) < 0)
            found.push(name)
    }
    const native = entry.native ?? {}
    for (const point of entry.points ?? []) {
        if (point !== "native") {
            add(point)
            continue
        }
        if ((native.touchMethods ?? []).length > 0)
            add("hardware")
        if ((native.inputSources ?? []).length > 0)
            add("hardware")
        if ((native.attachments ?? []).length > 0 || (native.behaviors ?? []).length > 0)
            add("component")
        if ((native.nodes ?? []).length > 0)
            add("nodes")
    }
    if ((entry.permissions ?? []).some(permission => permission.name === "hardware"))
        add("hardware")
    return found
}

function lead(entry) {
    const found = capabilities(entry)
    return found.length > 0 ? found[0] : ""
}

function showsSettings(entry, grants) {
    return entry.active === true && (entry.hasSettings === true || grants > 0 || entry.id === esp32)
}

function icon(point) {
    switch (point) {
    case "profile":
        return "layers"
    case "theme":
        return "palette"
    case "filter":
        return "monitor-play"
    case "script":
        return "code-xml"
    case "command":
        return "terminal"
    case "preset":
        return "sliders-horizontal"
    case "hardware":
        return "usb"
    case "nodes":
        return "workflow"
    case "component":
        return "sparkles"
    case "task":
        return "bot"
    case "panel":
        return "panel-top"
    case "help":
        return "circle-help"
    case "tour":
        return "navigation"
    default:
        return "puzzle"
    }
}

function tone(point) {
    switch (point) {
    case "profile":
        return "lime"
    case "theme":
        return "rose"
    case "filter":
        return "azure"
    case "script":
        return "amber"
    case "command":
        return "teal"
    case "hardware":
        return "amber"
    case "nodes":
        return "lime"
    case "component":
        return "orange"
    case "task":
        return "teal"
    case "panel":
        return "azure"
    case "help":
        return "teal"
    case "tour":
        return "orange"
    default:
        return ""
    }
}
