export default class TapInTurn extends Component {
    static props = {
        key: prop.key("F", { label: "Key" }),
        second: prop.position(undefined, { label: "Second place" }),
        third: prop.position(undefined, { label: "Third place" }),
    };

    next = 0;

    setup() {
        this.props.key.onPress(async () => {
            const places = [this.position, this.props.second, this.props.third].filter((place) => place !== null);
            await touch.tap(places[this.next % places.length]);
            this.next += 1;
        });
    }
}
