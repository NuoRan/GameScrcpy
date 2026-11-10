function placesIn(text: string): PicturePoint[] {
    const places: PicturePoint[] = [];
    for (const part of text.split(";")) {
        if (part.trim() === "") {
            continue;
        }
        const values = part.split(",").map((value) => Number(value.trim()));
        if (values.length !== 2 || !values.every((value) => value >= 0 && value <= 100)) {
            throw new Error(`"${part.trim()}" is not a place: write x,y in percent of the picture, such as 50,30`);
        }
        places.push(point(values[0] / 100, values[1] / 100));
    }
    return places;
}

export default class TapInOrder extends Task {
    static params = {
        places: param.text("35,32; 40,61; 24,70", { label: "Places as x,y in percent, split by ;" }),
        gapMs: param.number(1000, { label: "Wait between taps (ms)", min: 0, max: 600000 }),
        times: param.number(1, { label: "Times to go round", min: 1, max: 1000 }),
    };

    async run({ signal }: TaskContext) {
        const places = placesIn(this.params.places);
        if (places.length === 0) {
            log.warn("There is no place to tap: list them under Places");
            return;
        }
        for (let round = 1; round <= this.params.times && !signal.aborted; round++) {
            this.progress(`${round} / ${this.params.times}`);
            for (const [index, place] of places.entries()) {
                if (round > 1 || index > 0) {
                    await sleep(this.params.gapMs);
                }
                await touch.tap(place);
                log.info(`Round ${round}: tapped place ${index + 1} at ${Math.round(place.x * 100)}%, ${Math.round(place.y * 100)}%`);
            }
        }
    }
}
