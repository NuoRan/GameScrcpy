const page = ui.panel("welcome");
const options = ui.panel("options");
const savedTip = store.get("tip");

let tip = savedTip !== false;
let visits = 0;

function drawPage(): void {
    const children: UiNode[] = [
        { type: "text", style: "heading", text: "Tour and help sample" },
        {
            type: "text",
            format: "markdown",
            text: "This page belongs to the sample extension **example.tour-and-help**. Its manifest declares a guided " +
                "tour and two help articles; its app script only draws this page and a card in Settings, so that the " +
                "tour has something to point at.",
        },
        { type: "steps", steps: ["Install", "Turn on", "Take the tour", "Read the help"], current: 3 },
        { type: "keyValue", rows: [{ key: "Times this page was opened", value: visits }] },
    ];
    if (tip) {
        children.push({
            type: "text",
            tone: "accent",
            text: "Tip: Details on the extension's card has a button that plays its tour again, and the help page " +
                "lists its articles under Extensions.",
        });
    }
    page.render({ type: "column", children });
}

function drawOptions(): void {
    options.render({
        type: "column",
        children: [
            { type: "toggle", id: "tip", title: "Show a tip on the sample's page", value: tip },
            { type: "text", style: "caption", tone: "muted", text: "The tour lights up this card in its fourth step." },
        ],
    });
}

page.on("open", () => {
    visits += 1;
    drawPage();
});

options.on("change", ({ id, value }) => {
    if (id === "tip" && typeof value === "boolean") {
        tip = value;
        store.set("tip", tip);
        drawPage();
        drawOptions();
    }
});

drawPage();
drawOptions();
