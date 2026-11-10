.pragma library

function html(raw, colors, mono, fill) {
    if (!raw)
        return ""
    return String(raw)
        .replace(/%T1%/g, String(colors.textPrimary))
        .replace(/%T3%/g, String(colors.textTertiary))
        .replace(/%CODEBG%/g, String(fill))
        .replace(/%CODE%/g, String(colors.textPrimary))
        .replace(/%KEYBG%/g, String(fill))
        .replace(/%MONO%/g, mono)
        .replace(/%LINK%/g, String(colors.accentText))
        .replace(/%KW%/g, String(colors.accentText))
        .replace(/%STR%/g, String(colors.successText))
        .replace(/%NUM%/g, String(colors.warningText))
        .replace(/%COM%/g, String(colors.textTertiary))
        .replace(/%TY%/g, String(colors.infoText))
}

function escaped(text) {
    return String(text).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;")
}

function part(text, colors) {
    const strong = "<span style=\"color:" + String(colors.textPrimary) + ";font-weight:500\">"
    const quiet = "<span style=\"color:" + String(colors.textTertiary) + "\">&nbsp;›&nbsp;</span>"
    return String(text).split(" > ").map(step => strong + escaped(step.trim()) + "</span>").join(quiet)
}

function quoted(text, colors) {
    const strong = "<span style=\"color:" + String(colors.textPrimary) + ";font-weight:500\">"
    return String(text).split(/(“[^”]*”)/).map(piece => {
        if (piece.length > 1 && piece.startsWith("“") && piece.endsWith("”"))
            return strong + escaped(piece) + "</span>"
        return escaped(piece)
    }).join("")
}

function sentence(template, parts, colors) {
    let text = colors ? quoted(template, colors) : String(template)
    parts.forEach((value, index) => {
        const shown = colors ? part(value, colors) : String(value)
        text = text.replace("%" + (index + 1), () => shown)
    })
    return text
}
