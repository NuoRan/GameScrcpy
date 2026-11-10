import MyComponent from "../combo/component.ts";

export default class ComboPlus extends MyComponent {
    setup() {
        super.setup();
        log.info(this.name);
    }
}
