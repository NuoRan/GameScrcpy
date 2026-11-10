export default class ShowAllHints extends Component {
    static props = {
        key: prop.key(undefined, { label: "Key" }),
    };

    setup() {
        this.props.key?.onPress(() => {
            if (this.enabled) {
                hints.reveal(true);
            }
        });
        this.props.key?.onRelease(() => {
            hints.reveal(false);
        });
        this.onDisabled(() => {
            hints.reveal(false);
        });
    }
}
