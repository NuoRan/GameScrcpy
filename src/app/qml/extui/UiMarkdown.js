.pragma library

function escaped(text) {
    return String(text).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;")
}

function emphasized(text) {
    return text.replace(/\*\*([^*]+?)\*\*/g, "<b>$1</b>")
        .replace(/__([^_]+?)__/g, "<b>$1</b>")
        .replace(/(^|[^\w*])\*([^*\s](?:[^*]*[^*\s])?)\*(?![\w*])/g, "$1<i>$2</i>")
        .replace(/(^|[^\w_])_([^_\s](?:[^_]*[^_\s])?)_(?![\w_])/g, "$1<i>$2</i>")
}

function inline(text, codeColor) {
    const parts = text.split("`")
    let out = ""
    for (let index = 0; index < parts.length; ++index) {
        if (index % 2 === 0)
            out += emphasized(parts[index])
        else if (index < parts.length - 1)
            out += "<font color=\"" + codeColor + "\">" + parts[index] + "</font>"
        else
            out += "`" + emphasized(parts[index])
    }
    return out
}

function styled(text, codeColor) {
    return escaped(text).replace(/\r/g, "").split("\n").map(line => {
        const bullet = /^\s*[-*+]\s+(.*)$/.exec(line)
        if (bullet !== null)
            return "&nbsp;&nbsp;\u2022&nbsp;" + inline(bullet[1], codeColor)
        const numbered = /^\s*(\d{1,3})[.)]\s+(.*)$/.exec(line)
        if (numbered !== null)
            return "&nbsp;&nbsp;" + numbered[1] + ".&nbsp;" + inline(numbered[2], codeColor)
        return inline(line, codeColor)
    }).join("<br>")
}
