import QuickHold from "../quick-hold/component.ts";

export default class QuickHoldPlus extends QuickHold {
    onPressed() {
        super.onPressed();
        log.info(this.name);
    }
}
