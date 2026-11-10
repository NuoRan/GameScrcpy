controls.jump.onPress(async () => {
    const start = await vision.find("startButon");
    log.info("seen", start !== null);
});
