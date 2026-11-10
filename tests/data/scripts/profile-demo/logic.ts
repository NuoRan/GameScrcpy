controls.jump.onPress(async () => {
    log.info("jump pressed while aiming:", vars.aiming.value);
    await controls.fire.trigger();
});

controls["dash-move"].whileHeld(async ({ signal }) => {
    while (!signal.aborted) {
        await touch.tap(0.8, 0.7, 30);
        await sleep(70);
    }
});

vars.aiming.onChange(({ value }) => {
    controls.fire.enabled = !value;
});

export async function start(): Promise<void> {
    vars.aiming.value = true;
    log.info("random", Math.random(), "date", new Date().toISOString());
}
