export async function open(): Promise<void> {
    await touch.tap(0.8, 0.9);
    await sleep(1500);
    const playing = await vision.find("res/playing.png");
    if (playing) {
        await touch.tap(playing.x, playing.y);
    }
}

export async function collect(): Promise<void> {
    for (;;) {
        await touch.swipe(0.5, 0.8, 0.5, 0.2);
        await sleep(500); if ((await vision.findAll("res/coin.png")).length === 0) break;
    }
}

export async function calm(): Promise<void> {
    await sleep(100);
    log.info("vision.find(\"res/coin.png\") only in a string");
    await sleep(100);
    await device.pressKey("back");
    const coin = await vision.find("res/coin.png");
    log.info(coin !== null);
}
