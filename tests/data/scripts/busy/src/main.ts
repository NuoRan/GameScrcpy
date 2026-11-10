export function spin(): void {
    let count = 0;
    while (true) {
        count += 1;
    }
}

export async function poll(): Promise<void> {
    for (;;) {
        await sleep(10);
    }
}
