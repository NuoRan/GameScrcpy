controls.fire.bind("enabled", () => !vars.aiming.value);

controls.aim.whileHeld(async ({ signal }) => {
    controls.jump.bind("enabled", () => false);
    while (!signal.aborted) {
        await nextFrame();
    }
});

controls.jump.onPress(async () => {
    await touch.tap(0.8, 0.9).until(() => vars.ready.value, { timeoutMs: 100, retry: 1 });
    await touch.tap(0.5, 0.5);
});

export async function aim(): Promise<void> {
    vars.aiming.value = true;
    await sleep(100);
    vars.aiming.value = false;
}

export async function look(): Promise<void> {
    const found = await device.pressKey("back").until(() => vision.find("res/ready.png"), { timeoutMs: 2000 });
    await touch.tap(found.x, found.y);
    const binding = controls.fire.bind("enabled", () => vars.ready.value);
    await controls.fire.hold(100).until(() => !vars.ready.value);
    binding.dispose();
}
