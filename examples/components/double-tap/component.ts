export default class DoubleTap extends Component {
    static props = {
        key: prop.key("F", { label: "Key" }),
        gapMs: prop.number(80, { label: "Gap (ms)", min: 20, max: 1000 }),
    };

    setup() {
        this.props.key.onPress(async () => {
            const at = this.position;
            if (at === null) {
                return;
            }
            await touch.tap(at);
            await sleep(this.props.gapMs);
            await touch.tap(at);
        });
    }
}
