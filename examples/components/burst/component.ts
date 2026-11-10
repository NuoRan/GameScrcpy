export default class Burst extends Component {
    static props = {
        key: prop.key("F", { label: "Key" }),
        times: prop.number(3, { label: "Times", min: 1, max: 20 }),
        gapMs: prop.number(80, { label: "Gap (ms)", min: 20, max: 1000 }),
    };

    setup() {
        this.props.key.onPress(async () => {
            const at = this.position;
            if (at === null) {
                return;
            }
            for (let tap = 0; tap < this.props.times; tap++) {
                if (tap > 0) {
                    await sleep(this.props.gapMs);
                }
                await touch.tap(at);
            }
        });
    }
}
