export default class WaitThenTap extends Task {
    static params = {
        picture: param.text("res/network.png", { label: "Picture" }),
        waitMs: param.number(10000, { label: "Wait up to (ms)", min: 100, max: 600000 }),
        tapPicture: param.toggle(true, { label: "Tap where the picture is" }),
        atX: param.number(50, { label: "Otherwise tap at x (%)", min: 0, max: 100 }),
        atY: param.number(50, { label: "Otherwise tap at y (%)", min: 0, max: 100 }),
    };

    async run() {
        const picture = this.params.picture;
        const seconds = this.params.waitMs / 1000;
        log.info(`Waiting up to ${seconds} s for ${picture}`);
        const found = await vision.waitFor(picture, { timeoutMs: this.params.waitMs });
        if (found === null) {
            log.warn(`Gave up: ${picture} did not appear within ${seconds} s`);
            return;
        }
        const target = this.params.tapPicture ? point(found.x, found.y) : point(this.params.atX / 100, this.params.atY / 100);
        await touch.tap(target);
        log.info(`${picture} appeared; tapped at ${Math.round(target.x * 100)}%, ${Math.round(target.y * 100)}%`);
    }
}
