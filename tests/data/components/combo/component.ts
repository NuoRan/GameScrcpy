export default class MyComponent extends Component {
    static props = {
        key: prop.key("F", { label: "Key" }),
    };

    setup() {
        this.props.key.onPress(async () => {
            const at = this.position;
            if (at !== null) {
                await touch.tap(at);
            }
        });
    }
}
