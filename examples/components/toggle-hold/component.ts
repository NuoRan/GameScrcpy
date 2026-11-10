export default class ToggleHold extends Component {
    static props = {
        key: prop.key("F", { label: "Key" }),
    };

    pressed = this.event();
    released = this.event();
    holding = false;

    setup() {
        this.props.key.onPress(() => {
            const at = this.position;
            if (at === null) {
                return;
            }
            this.holding = !this.holding;
            if (this.holding) {
                this.finger.down(at);
                this.pressed.emit();
            } else {
                this.finger.up();
                this.released.emit();
            }
        });
    }
}
