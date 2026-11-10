import { controls, log, point, vision } from "gamescrcpy";

controls.jump.onPress(async () => {
    const color = await vision.pickColor(point(0.5, 0.5));
    log.info("picked", color === null ? "nothing" : "a color");
});
