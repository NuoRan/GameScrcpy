import { sleep, touch } from "gamescrcpy";

export async function tapTwice(x: number, y: number): Promise<void> {
    await touch.tap(x, y);
    await sleep(80);
    await touch.tap(x, y);
}
