enum Mode { Fast }
const count: number = "three";
export function start(): void {
    log.info(count, Mode.Fast);
}
