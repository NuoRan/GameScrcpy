import { Component, controls, log, point, prop, touch, ui, vision, window } from "gamescrcpy";
import { tapTwice } from "./helper.ts";

class Toggle extends Component {
    static props = { key: prop.key("F") };

    setup() {
        this.props.key.onPress(() => tapTwice(0.5, 0.5));
    }
}

const toggle: Component = new Toggle();

controls.jump.onPress(async () => {
    const color = await vision.pickColor(point(0.5, 0.25));
    const red = await vision.matchColor(point(0.5, 0.25), "#E53935", { tolerance: 0.2 });
    ui.mark(0.5, 0.25, { label: "probe" });
    await touch.tap(0.5, 0.25);
    log.info("pixel", color, red, window.focused(), toggle.type);
});
