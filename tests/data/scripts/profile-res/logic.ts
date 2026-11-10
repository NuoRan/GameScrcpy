controls.jump.onPress(async () => {
    const start = await vision.find("startButton", { region: "bottomRight" });
    const coins = await vision.findAll("res/coin.png", { max: 4, region: res.region("bottomRight") });
    const banner = await vision.findColor("banner");
    const gold = await vision.findColor("#F5C542", { tolerance: 0.2 });
    const entry = res.point("活动入口");
    const drag = res.path("dragMap");
    log.info("seen", start === null, coins.length, banner === null, gold === null, entry.x, drag.points.length);
});
