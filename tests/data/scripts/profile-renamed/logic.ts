controls.jump.onPress(async () => {
    log.info("still the old name");
    await controls.jump.trigger();
});

controls.fire.onPress(async () => {
    await controls.fire.trigger();
});
