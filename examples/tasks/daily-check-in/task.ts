export default class DailyCheckIn extends Task {
    static params = {
        app: param.text("com.android.settings", { label: "App to open (package name)" }),
        button: param.text("res/check-in.png", { label: "Picture of the check-in button" }),
        done: param.text("res/done.png", { label: "Picture shown once checked in" }),
        waitMs: param.number(30000, { label: "Wait for each picture up to (ms)", min: 1000, max: 600000 }),
    };

    async run() {
        const seconds = this.params.waitMs / 1000;
        if (this.params.app !== "") {
            log.info(`Opening ${this.params.app}`);
            device.launch(this.params.app);
        }
        const button = await vision.waitFor(this.params.button, { timeoutMs: this.params.waitMs });
        if (button === null) {
            throw new Error(`The check-in button did not appear within ${seconds} s`);
        }
        await touch.tap(button);
        log.info("Tapped the check-in button");
        const done = await vision.waitFor(this.params.done, { timeoutMs: this.params.waitMs });
        if (done === null) {
            throw new Error(`The check-in button was tapped, but the picture for done did not appear within ${seconds} s`);
        }
        log.info("Checked in");
    }
}
