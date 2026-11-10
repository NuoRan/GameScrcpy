export default class FindAndTap extends Task {
    static params = {
        picture: param.text("res/notifications.png", { label: "Picture" }),
        searchMs: param.number(5000, { label: "Search for (ms)", min: 100, max: 600000 }),
    };

    async run() {
        const picture = this.params.picture;
        const deadline = time.now() + this.params.searchMs;
        do {
            const found = await vision.findAndTap(picture);
            if (found !== null) {
                log.info(`Tapped ${picture} at ${Math.round(found.x * 100)}%, ${Math.round(found.y * 100)}%`);
                return;
            }
            await nextFrame();
        } while (time.now() < deadline);
        log.warn(`${picture} did not show up within ${this.params.searchMs / 1000} s`);
    }
}
