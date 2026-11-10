const Fire = defineInterface<{ fire(): void }>("fire");

export default class Volley extends Component {
    static implements = [Fire];
    static props = { key: prop.key("V"), target: prop.component(Skill) };

    cancel = this.child(Tap);
    skills = [this.child(Skill), this.child(Skill)];

    fire() {
        for (const skill of this.skills) {
            skill.cast();
        }
    }

    setup() {
        this.props.key.onPress(() => {
            for (const each of Fire.all()) {
                each.fire();
            }
        });
        this.cancel.onPress(() => this.skills[0].cancel());
    }
}
