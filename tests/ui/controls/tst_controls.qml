import QtQuick
import QtTest
import GameScrcpy.Theme
import GameScrcpy.Controls

Item {
    id: root

    width: 640
    height: 1000

    Column {
        spacing: Theme.space.sm

        Button {
            id: button

            text: "Connect"
        }
        Button {
            id: disabledButton

            text: "Unavailable"
            enabled: false
        }
        Switch {
            id: toggle
        }
        Checkbox {
            id: check

            tristate: true
        }
        SegmentedControl {
            id: segmented

            model: ["One", "Two", "Three"]
        }
        Tabs {
            id: tabs

            model: ["Components", { text: "Keys", count: 7 }]
        }
        TextField {
            id: field
        }
        SearchField {
            id: search
        }
        Select {
            id: select

            model: ["One", "Two", "Three"]
        }
        Slider {
            id: slider

            from: 0
            to: 10
            stepSize: 1
        }
        Button {
            id: tipButton

            iconName: "settings-2"
            toolTip: "Settings"
        }
        Button {
            id: menuButton

            text: "Menu"

            Menu {
                id: menu

                MenuItem {
                    text: "First"
                }
                MenuItem {
                    id: secondItem

                    text: "Second"
                }
            }
        }
        NavRail {
            id: rail

            height: 3 * Theme.size.navItemHeight + 4 * Theme.space.lg
            model: [
                { text: "One", icon: "house" },
                { text: "Two", icon: "monitor" },
                { text: "Three", icon: "settings-2" }
            ]
        }
        SettingsRow {
            id: settingsRow

            width: 480
            title: "Setting"

            Switch {
                id: rowSwitch
            }
        }
        InlineAlert {
            id: alert

            width: 480
            tone: "warn"
            title: "adb is not running"
            detail: "Install the Android platform tools."
            actionText: "Retry"
        }
    }

    SignalSpy {
        id: alertActions

        target: alert
        signalName: "actionTriggered"
    }

    PageHost {
        id: pages

        x: root.width - width
        width: 200
        height: 100

        Item {
            id: pageOne
        }
        Item {
            id: pageTwo
        }
        Item {
            id: pageThree
        }
    }

    CodeField {
        id: code

        x: root.width - width
        y: pages.height + Theme.space.lg
        description: "Pairing code"
    }

    SignalSpy {
        id: codeCompletions

        target: code
        signalName: "completed"
    }

    Toast {
        id: quickToast

        duration: 200
    }

    Toast {
        id: actionToast

        duration: 200
        text: "Disconnected"
        actionText: "Reconnect"
    }

    Dialog {
        id: dialog

        title: "Confirm"
    }

    SignalSpy {
        id: triggers

        target: secondItem
        signalName: "triggered"
    }
    SignalSpy {
        id: railActivations

        target: rail
        signalName: "activated"
    }
    SignalSpy {
        id: resets

        target: settingsRow
        signalName: "resetRequested"
    }
    SignalSpy {
        id: toastActions

        target: actionToast
        signalName: "actionTriggered"
    }
    SignalSpy {
        id: accepts

        target: dialog
        signalName: "accepted"
    }
    SignalSpy {
        id: alternatives

        target: dialog
        signalName: "alternativeChosen"
    }
    SignalSpy {
        id: rejects

        target: dialog
        signalName: "rejected"
    }

    SignalSpy {
        id: clicks

        target: button
        signalName: "clicked"
    }
    SignalSpy {
        id: disabledClicks

        target: disabledButton
        signalName: "clicked"
    }
    SignalSpy {
        id: toggles

        target: toggle
        signalName: "toggled"
    }
    SignalSpy {
        id: activations

        target: segmented
        signalName: "activated"
    }

    TestCase {
        name: "Controls"
        when: windowShown

        function init() {
            failOnWarning(/.?/)
            clicks.clear()
            disabledClicks.clear()
            toggles.clear()
            activations.clear()
            toggle.checked = false
            check.checkState = Qt.Unchecked
            segmented.currentIndex = 0
            triggers.clear()
            field.text = ""
            field.errorText = ""
            search.text = ""
            select.currentIndex = 0
            slider.to = 10
            slider.value = 5
            rail.currentIndex = 0
            settingsRow.overridden = true
            for (const spy of [railActivations, resets, toastActions, accepts, rejects, alertActions, codeCompletions])
                spy.clear()
            code.text = ""
            alert.actionEnabled = true
            root.forceActiveFocus()
        }

        function test_buttonClicksOnPointerAndSpace() {
            mouseClick(button)
            compare(clicks.count, 1)
            button.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Space)
            compare(clicks.count, 2)
        }

        function test_disabledButtonIgnoresClicks() {
            mouseClick(disabledButton)
            compare(disabledClicks.count, 0)
        }

        function test_focusRingShowsForKeyboardFocusOnly() {
            mouseClick(button)
            verify(button.activeFocus)
            verify(!button.visualFocus)
            root.forceActiveFocus()
            button.forceActiveFocus(Qt.TabFocusReason)
            verify(button.visualFocus)
        }

        function test_tabSkipsDisabledControls() {
            button.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Tab)
            verify(toggle.activeFocus)
            verify(toggle.visualFocus)
        }

        function test_switchTogglesOnClickAndSpace() {
            mouseClick(toggle)
            verify(toggle.checked)
            toggle.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Space)
            verify(!toggle.checked)
            compare(toggles.count, 2)
        }

        function test_checkboxCyclesThroughPartiallyChecked() {
            mouseClick(check)
            compare(check.checkState, Qt.PartiallyChecked)
            mouseClick(check)
            compare(check.checkState, Qt.Checked)
            mouseClick(check)
            compare(check.checkState, Qt.Unchecked)
        }

        function test_segmentedFollowsArrowKeysAndClicks() {
            segmented.forceActiveFocus(Qt.TabFocusReason)
            verify(segmented.visualFocus)
            keyClick(Qt.Key_Right)
            compare(segmented.currentIndex, 1)
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Right)
            compare(segmented.currentIndex, 2)
            compare(activations.count, 2)
            mouseClick(segmented, Theme.space.md, segmented.height / 2)
            compare(segmented.currentIndex, 0)
            compare(activations.count, 3)
            compare(activations.signalArguments[2][0], 0)
        }

        function test_tabsSwitchByClickAndArrowKeysAndShowACount() {
            tabs.currentIndex = 0
            const indicator = findChild(tabs, "tabIndicator")
            const row = tabs.contentItem.children[0]
            tryVerify(() => indicator.width === row.children[0].width)
            mouseClick(row.children[1])
            compare(tabs.currentIndex, 1)
            tryVerify(() => indicator.parent.x === row.children[1].x)
            tryVerify(() => indicator.width === row.children[1].width)
            const count = findChild(row.children[1], "tabCount")
            verify(count.visible)
            verify(!findChild(row.children[0], "tabCount").visible)
            tabs.forceActiveFocus(Qt.TabFocusReason)
            verify(tabs.visualFocus)
            keyClick(Qt.Key_Left)
            compare(tabs.currentIndex, 0)
            keyClick(Qt.Key_Left)
            compare(tabs.currentIndex, 0)
        }

        function test_segmentedThumbFollowsAReplacedModel() {
            segmented.currentIndex = 2
            segmented.model = ["Uno", "Dos", "Three, written longer"]
            const thumb = segmented.contentItem.children[0]
            const row = segmented.contentItem.children[1]
            let third = null
            for (let i = 0; i < row.children.length; ++i) {
                if (row.children[i].text === "Three, written longer")
                    third = row.children[i]
            }
            verify(third !== null)
            tryVerify(() => thumb.visible)
            tryCompare(thumb, "x", third.x)
            tryCompare(thumb, "width", third.width)
            segmented.model = ["One", "Two", "Three"]
        }

        function test_textFieldTakesTypingAndShowsItsError() {
            mouseClick(field)
            verify(field.activeFocus)
            keyClick("a")
            keyClick("b")
            compare(field.text, "ab")
            compare(field.implicitHeight, Theme.size.control)
            field.errorText = "Not an address"
            verify(field.implicitHeight > Theme.size.control)
        }

        function test_codeFieldTakesDigitsAndCompletes() {
            mouseClick(code)
            verify(code.activeFocus)
            for (const key of ["4", "8", "a", " ", "2", "9", "1"])
                keyClick(key)
            compare(code.text, "48291")
            verify(!code.complete)
            compare(codeCompletions.count, 0)
            keyClick(Qt.Key_Backspace)
            compare(code.text, "4829")
            keyClick("1")
            keyClick("7")
            compare(code.text, "482917")
            verify(code.complete)
            compare(codeCompletions.count, 1)
            compare(codeCompletions.signalArguments[0][0], "482917")
            keyClick("5")
            compare(code.text, "482917")
            compare(codeCompletions.count, 1)
        }

        function test_searchFieldClearButtonEmptiesIt() {
            search.text = "bitrate"
            const clear = findChild(search, "clear")
            verify(clear.visible)
            mouseClick(clear)
            compare(search.text, "")
            verify(!clear.visible)
        }

        function test_selectOpensAndPicksWithKeys() {
            select.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Space)
            tryVerify(() => select.popup.opened)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(select.currentIndex, 1)
            compare(select.displayText, "Two")
            tryVerify(() => !select.popup.visible)
        }

        function test_sliderKeepsItsValueWhenItsKnobIsClicked() {
            slider.to = 100
            for (const value of [20, 80]) {
                slider.value = value
                const handle = slider.handle
                const center = handle.mapToItem(slider, handle.width / 2, handle.height / 2)
                mouseClick(slider, center.x, center.y)
                compare(slider.value, value)
            }
        }

        function test_sliderStepsWithArrowKeys() {
            slider.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Right)
            compare(slider.value, 6)
            compare(slider.valueText, "6")
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Left)
            compare(slider.value, 4)
        }

        function test_toolTipShowsWhileHovered() {
            const tip = findChild(tipButton, "toolTip")
            verify(!tip.visible)
            tryVerify(() => {
                if (!tipButton.hovered)
                    mouseMove(tipButton)
                return tip.opened
            })
            mouseMove(root, root.width - 1, root.height - 1)
            tryVerify(() => !tip.visible)
        }

        function test_menuTriggersWithKeys() {
            menu.open()
            tryVerify(() => menu.opened)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            compare(triggers.count, 1)
            tryVerify(() => !menu.visible)
        }

        function test_menuWidensToItsWidestItem() {
            const wide = createTemporaryQmlObject(
                'import GameScrcpy.Controls\nMenu { MenuItem { text: "Show the window of this device"; shortcut: "Ctrl Shift Enter" } }',
                root)
            const item = wide.itemAt(0)
            verify(item.implicitWidth > Theme.size.menuMinWidth)
            verify(wide.implicitWidth >= item.implicitWidth + wide.leftPadding + wide.rightPadding)
        }

        function test_menuItemDescriptionAddsALine() {
            const menu = createTemporaryQmlObject(
                'import GameScrcpy.Controls\nMenu { MenuItem { text: "Launch" } MenuItem { text: "Launch"; description: "On the phone screen" } }',
                root)
            compare(menu.itemAt(0).implicitHeight, Theme.size.menuItem)
            compare(menu.itemAt(1).implicitHeight, Theme.size.menuItem + Theme.type.caption.lineHeight)
            verify(!findChild(menu.itemAt(0), "description").visible)
        }

        function test_joinedButtonsSquareTheInnerCorners() {
            const start = createTemporaryQmlObject('import GameScrcpy.Controls\nButton { joined: "start"; text: "Launch" }', root)
            compare(start.background.topRightRadius, 0)
            compare(start.background.bottomRightRadius, 0)
            compare(start.background.topLeftRadius, Theme.radius.sm)
            const end = createTemporaryQmlObject('import GameScrcpy.Controls\nButton { joined: "end"; iconName: "chevron-down" }', root)
            compare(end.background.topLeftRadius, 0)
            compare(end.background.bottomRightRadius, Theme.radius.sm)
        }

        function test_navRailFollowsClicksAndArrowKeys() {
            const second = rail.topPadding + Theme.size.navItemHeight + Theme.space.xs + Theme.size.navItemHeight / 2
            mouseClick(rail, rail.width / 2, second)
            compare(rail.currentIndex, 1)
            rail.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Down)
            compare(rail.currentIndex, 2)
            keyClick(Qt.Key_Down)
            compare(rail.currentIndex, 2)
            compare(railActivations.count, 2)
        }

        function test_navRailIndicatorFollowsAReplacedModel() {
            const pages = rail.model
            rail.currentIndex = 1
            rail.model = pages.map(page => ({ text: page.text + " page", icon: page.icon }))
            const top = rail.contentItem.children[0]
            const indicator = rail.contentItem.children[1]
            let second = null
            for (let i = 0; i < top.children.length; ++i) {
                if (top.children[i].text === "Two page")
                    second = top.children[i]
            }
            verify(second !== null)
            tryVerify(() => indicator.visible)
            tryCompare(indicator, "y", top.y + second.y + (second.height - indicator.height) / 2)
            rail.model = pages
            rail.currentIndex = 0
        }

        function test_settingsRowResetsWhileOverridden() {
            const reset = findChild(settingsRow, "reset")
            verify(reset.visible)
            mouseClick(reset)
            compare(resets.count, 1)
            settingsRow.overridden = false
            verify(!reset.visible)
        }

        function test_settingsRowResetByKeyboardHandsFocusToTheControl() {
            const reset = findChild(settingsRow, "reset")
            reset.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Space)
            compare(resets.count, 1)
            verify(rowSwitch.activeFocus)
        }

        function test_settingsRowMarksTheSearchedWords() {
            const title = findChild(settingsRow, "title")
            compare(title.textFormat, Text.PlainText)
            settingsRow.title = "Frame rate & <limit>"
            settingsRow.highlight = "RATE lim"
            compare(title.textFormat, Text.StyledText)
            verify(title.text.indexOf(">rate</font>") >= 0)
            verify(title.text.indexOf(" &amp; &lt;<font") >= 0)
            verify(title.text.indexOf(">lim</font>it&gt;") >= 0)
            settingsRow.highlight = ""
            compare(title.text, "Frame rate & <limit>")
            settingsRow.title = "Setting"
        }

        function test_settingsRowKeepsItsExplanationBehindInfo() {
            const info = findChild(settingsRow, "info")
            const description = findChild(settingsRow, "description")
            const note = findChild(settingsRow, "note")
            verify(!info.visible)
            settingsRow.description = "How many frames per second"
            verify(info.visible)
            verify(!description.visible)
            compare(info.info, "How many frames per second")
            compare(info.title, "Setting")
            compare(settingsRow.Accessible.description, "How many frames per second")
            settingsRow.highlight = "frames"
            verify(!info.visible)
            verify(description.visible)
            verify(description.text.indexOf(">frames</font>") >= 0)
            settingsRow.highlight = ""
            verify(info.visible)
            verify(!note.visible)
            settingsRow.note = "Global default: 60"
            verify(note.visible)
            compare(note.text, "Global default: 60")
            settingsRow.note = ""
            settingsRow.description = ""
        }

        function test_settingsRowShowsItsKeysAndNestsUnderTheRowAbove() {
            const keys = findChild(settingsRow, "keys")
            const line = findChild(settingsRow, "nestLine")
            const title = findChild(settingsRow, "title")
            verify(!keys.visible)
            settingsRow.keys = ["Ctrl", "F7"]
            verify(keys.visible)
            compare(keys.children.length >= 2, true)
            verify(!line.visible)
            const plainX = title.mapToItem(settingsRow, 0, 0).x
            settingsRow.nested = true
            verify(line.visible)
            compare(title.mapToItem(settingsRow, 0, 0).x, plainX + Theme.space.xl)
            settingsRow.nested = false
            settingsRow.keys = []
            verify(!keys.visible)
        }

        function test_settingsRowIsAtLeastOneInlineRowTall() {
            verify(settingsRow.height >= Theme.size.settingsRow)
        }

        function test_infoOpensOnHoverAndStaysOnceClicked() {
            const info = createTemporaryQmlObject('import GameScrcpy.Controls\nInfoButton { x: 40; y: 40; title: "Frame rate"; info: "How many frames"; delay: 50 }', root)
            compare(info.Accessible.description, "How many frames")
            mouseMove(info, info.width / 2, info.height / 2)
            verify(!info.shown)
            tryVerify(() => info.shown)
            compare(findChild(info, "bubbleTitle").text, "Frame rate")
            compare(findChild(info, "bubbleText").text, "How many frames")
            mouseMove(root, 1, 1)
            tryVerify(() => !info.shown)
            verify(!info.pinned)

            mouseClick(info)
            verify(info.pinned)
            verify(info.shown)
            mouseMove(root, 1, 1)
            wait(info.delay * 2)
            verify(info.shown)
            mouseClick(info)
            verify(!info.pinned)
            tryVerify(() => !info.shown)
        }

        function test_infoLeavesTheHoverAloneWhenThePointerPassesBy() {
            const info = createTemporaryQmlObject('import GameScrcpy.Controls\nInfoButton { x: 40; y: 40; title: "A"; info: "B"; delay: 200 }', root)
            mouseMove(info, info.width / 2, info.height / 2)
            mouseMove(root, 1, 1)
            wait(info.delay * 2)
            verify(!info.shown)
        }

        function test_infoWorksFromTheKeyboardAndClosesOnEscape() {
            const info = createTemporaryQmlObject('import GameScrcpy.Controls\nInfoButton { x: 40; y: 40; title: "A"; info: "B" }', root)
            info.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Return)
            verify(info.pinned)
            verify(info.shown)
            keyClick(Qt.Key_Space)
            verify(!info.pinned)
            tryVerify(() => !info.shown)
            keyClick(Qt.Key_Space)
            verify(info.shown)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !info.shown)
            verify(!info.pinned)
            verify(info.activeFocus)
        }

        function test_numberScaleMapsLinearValues() {
            const numbers = createTemporaryQmlObject('import GameScrcpy.Controls\nNumberScale { minimum: 0; maximum: 8192; from: 480; to: 3200; step: 40 }', root)
            compare(numbers.position(480), 0)
            compare(numbers.position(3200), 1)
            fuzzyCompare(numbers.position(1920), 0.5294, 0.001)
            compare(numbers.position(4000), 1)
            compare(numbers.valueAt(0.53), 1920)
            compare(numbers.valueAt(1), 3200)
            compare(numbers.stepped(1700, 1), 1740)
            compare(numbers.stepped(8180, 1), 8192)
            compare(numbers.stepped(10, -1), 0)
        }

        function test_numberScaleMapsLogarithmicValues() {
            const numbers = createTemporaryQmlObject('import GameScrcpy.Controls\nNumberScale { minimum: 0.5; maximum: 100; from: 1; to: 100; decimals: 1; logarithmic: true }', root)
            compare(numbers.position(1), 0)
            fuzzyCompare(numbers.position(10), 0.5, 1e-9)
            fuzzyCompare(numbers.position(8), 0.4515, 0.001)
            compare(numbers.valueAt(0.5), 10)
            compare(numbers.valueAt(0.45), 7.9)
            compare(numbers.valueAt(0.6), 16)
            compare(numbers.stepped(8, 1), 9)
            compare(numbers.stepped(12, 1), 13)
            compare(numbers.stepped(12, -1), 11)
            compare(numbers.stepped(100, 1), 100)
            compare(numbers.stepped(0.5, -1), 0.5)
        }

        function test_numberScaleReadsTypedNumbers() {
            const numbers = createTemporaryQmlObject('import GameScrcpy.Controls\nNumberScale { decimals: 1 }', root)
            compare(numbers.parse(" 12.34 "), 12.3)
            compare(numbers.parse("\uFF11\uFF12\uFF0C\uFF15"), 12.5)
            verify(Number.isNaN(numbers.parse("")))
            verify(Number.isNaN(numbers.parse("abc")))
            compare(numbers.text(8), "8.0")
            verify(numbers.accepts(100))
            verify(!numbers.accepts(100.1))
        }

        function numberField(): var {
            const field = createTemporaryQmlObject('import GameScrcpy.Controls\nNumberField { x: 40; y: 120; unit: "fps"; value: 60; numbers: NumberScale { minimum: 0; maximum: 240 } }', root)
            const spy = createTemporaryQmlObject('import QtTest\nSignalSpy { signalName: "submitted" }', root)
            spy.target = field
            return { field: field, spy: spy }
        }

        function test_numberFieldSubmitsTypedNumbersInRange() {
            const { field, spy } = numberField()
            compare(field.text, "60")
            field.forceActiveFocus(Qt.TabFocusReason)
            field.selectAll()
            keyClick(Qt.Key_9)
            keyClick(Qt.Key_0)
            keyClick(Qt.Key_Return)
            compare(spy.count, 1)
            compare(spy.signalArguments[0][0], 90)
            compare(field.errorText, "")
        }

        function test_numberFieldRefusesNumbersOutOfRangeUntilEscape() {
            const { field, spy } = numberField()
            field.forceActiveFocus(Qt.TabFocusReason)
            field.selectAll()
            keyClick(Qt.Key_3)
            keyClick(Qt.Key_0)
            keyClick(Qt.Key_0)
            keyClick(Qt.Key_Return)
            compare(spy.count, 0)
            compare(field.errorText, "Enter 0 to 240 fps")
            keyClick(Qt.Key_Escape)
            compare(field.errorText, "")
            compare(field.text, "60")
            compare(spy.count, 0)
        }

        function test_numberFieldNudgesWithArrowKeys() {
            const { field, spy } = numberField()
            field.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Up)
            keyClick(Qt.Key_Down)
            compare(spy.count, 2)
            compare(spy.signalArguments[0][0], 61)
            compare(spy.signalArguments[1][0], 59)
        }

        function test_numberFieldShowsAWordWhileAutomatic() {
            const { field, spy } = numberField()
            const unit = findChild(field, "unit")
            verify(unit.visible)
            field.automatic = true
            field.word = "Auto"
            compare(field.text, "Auto")
            verify(!unit.visible)
            compare(field.color, Theme.colors.textSecondary)
            field.forceActiveFocus(Qt.TabFocusReason)
            compare(field.text, "")
            root.forceActiveFocus()
            compare(field.text, "Auto")
            compare(spy.count, 0)
        }

        function test_numberFieldPrefixSitsBeforeTheNumber() {
            const field = createTemporaryQmlObject('import GameScrcpy.Controls\nNumberField { x: 40; y: 120; width: 120; prefix: "X"; unit: "%"; value: 76; numbers: NumberScale { decimals: 1 } }', root)
            const prefix = findChild(field, "prefix")
            verify(prefix.visible)
            compare(prefix.text, "X")
            compare(prefix.color, Theme.colors.textTertiary)
            compare(prefix.x, field.edge)
            verify(field.leftPadding >= prefix.x + prefix.width)
            compare(field.text, "76.0")
            field.prefix = ""
            verify(!prefix.visible)
            compare(field.leftPadding, field.edge)
        }

        function tickLabel(slider: var, text: string): var {
            for (const child of slider.children) {
                for (const grand of child.children) {
                    if (grand.objectName === "tick" && grand.text === text)
                        return grand
                }
            }
            return null
        }

        function test_sliderTickLabelsJumpWithoutMovingTheKnob() {
            const ticked = createTemporaryQmlObject('import GameScrcpy.Controls\nSlider { x: 20; y: 200; width: 300; value: 0.2; showValue: false; ticks: [{ position: 0, text: "1", current: false }, { position: 0.5, text: "10", current: true }, { position: 1, text: "100", current: false }] }', root)
            const spy = createTemporaryQmlObject('import QtTest\nSignalSpy { signalName: "tickActivated" }', root)
            spy.target = ticked
            compare(ticked.implicitHeight, Theme.size.sliderTicked)
            const first = tickLabel(ticked, "1")
            const middle = tickLabel(ticked, "10")
            const last = tickLabel(ticked, "100")
            compare(middle.font.weight, Font.DemiBold)
            compare(first.font.weight, Font.Normal)
            compare(first.mapToItem(ticked, 0, 0).x, ticked.leftPadding)
            fuzzyCompare(last.mapToItem(ticked, last.width, 0).x, ticked.leftPadding + ticked.availableWidth, 0.5)
            mouseClick(middle)
            compare(spy.count, 1)
            compare(spy.signalArguments[0][0], 1)
            compare(ticked.value, 0.2)
        }

        function test_sliderShowsItsAutomaticStateAndADragBubble() {
            const plain = createTemporaryQmlObject('import GameScrcpy.Controls\nSlider { x: 20; y: 260; width: 300; value: 0.5; showValue: false; valueText: "50 %" }', root)
            const knob = findChild(plain, "knob")
            const fill = findChild(plain, "fill")
            const bubble = findChild(plain, "bubble")
            compare(fill.color, Theme.colors.accent)
            plain.automatic = true
            compare(fill.color, Theme.colors.textDisabled)
            compare(knob.border.width, Theme.size.focusRing)
            plain.automatic = false
            verify(!bubble.visible)
            const center = knob.mapToItem(plain, knob.width / 2, knob.height / 2)
            mousePress(plain, center.x, center.y)
            verify(bubble.visible)
            mouseRelease(plain, center.x, center.y)
            verify(!bubble.visible)
        }

        function test_settingsRowStacksWhatGoesBelow() {
            const stacked = createTemporaryQmlObject('import QtQuick\nimport GameScrcpy.Controls\nSettingsRow {\nwidth: 480\ntitle: "Bit rate"\nNumberField { objectName: "field"; value: 8 }\nbelow: [Rectangle { objectName: "wide"; width: parent ? parent.width : 0; height: 40 }]\n}', root)
            verify(stacked.stacked)
            verify(!settingsRow.stacked)
            const field = findChild(stacked, "field")
            const wide = findChild(stacked, "wide")
            compare(wide.width, 480 - 2 * stacked.inset)
            verify(wide.mapToItem(stacked, 0, 0).y >= field.mapToItem(stacked, 0, field.height).y)
            compare(stacked.implicitHeight, stacked.headHeight + Theme.space.sm + 40 + 2 * Theme.space.md)
        }

        function test_filterChipShowsItsCountAndSelection() {
            const chip = createTemporaryQmlObject('import GameScrcpy.Controls\nFilterChip { x: 40; y: 320; text: "All"; count: 12 }', root)
            const spy = createTemporaryQmlObject('import QtTest\nSignalSpy { signalName: "clicked" }', root)
            spy.target = chip
            const count = findChild(chip, "count")
            compare(count.text, "12")
            verify(count.visible)
            compare(chip.Accessible.name, "All 12")
            mouseClick(chip)
            compare(spy.count, 1)
            compare(chip.background.color, Qt.rgba(0, 0, 0, 0))
            chip.selected = true
            compare(chip.background.color, Theme.colors.accentSubtle)
            chip.count = -1
            verify(!count.visible)
            compare(chip.Accessible.name, "All")
        }

        function test_tileTakesItsToneFromTheAccentHues() {
            const tile = createTemporaryQmlObject('import GameScrcpy.Controls\nTile { iconName: "monitor"; tone: "teal" }', root)
            const teal = Qt.color(Theme.accentChoices.teal)
            fuzzyCompare(tile.color.r, teal.r, 0.01)
            fuzzyCompare(tile.color.g, teal.g, 0.01)
            fuzzyCompare(tile.color.b, teal.b, 0.01)
            verify(tile.color.a > 0 && tile.color.a < 0.5)
            compare(Theme.toneInk("teal"), teal)
            Theme.dark = false
            compare(Theme.toneInk("teal"), Qt.color(Theme.accentTextChoices.teal))
            Theme.dark = true
            tile.tone = "nothing"
            compare(tile.color, Theme.colors.statePress)
            compare(Theme.toneInk("nothing"), Theme.colors.textSecondary)
        }

        function test_toastClosesByItselfUnlessItHasAnAction() {
            quickToast.show("Saved", "ok")
            tryVerify(() => quickToast.opened)
            tryVerify(() => !quickToast.visible)
            actionToast.open()
            tryVerify(() => actionToast.opened)
            wait(actionToast.duration * 2)
            verify(actionToast.visible)
            mouseClick(findChild(actionToast.contentItem, "action"))
            compare(toastActions.count, 1)
            tryVerify(() => !actionToast.visible)
        }

        function test_dialogRejectsOnEscapeAndAcceptsOnItsButton() {
            dialog.open()
            tryVerify(() => dialog.opened)
            keyClick(Qt.Key_Escape)
            tryVerify(() => !dialog.visible)
            compare(rejects.count, 1)
            dialog.open()
            tryVerify(() => dialog.opened)
            mouseClick(findChild(dialog.footer, "accept"))
            compare(accepts.count, 1)
            tryVerify(() => !dialog.visible)
        }

        function test_dialogOffersAThirdChoiceOnlyWhenNamed() {
            verify(!findChild(dialog.footer, "alternative").visible)
            dialog.alternativeText = "Don't save"
            dialog.open()
            tryVerify(() => dialog.opened)
            const alternative = findChild(dialog.footer, "alternative")
            verify(alternative.visible)
            mouseClick(alternative)
            compare(alternatives.count, 1)
            tryVerify(() => !dialog.visible)
            dialog.alternativeText = ""
        }

        function test_pageHostShowsOnePageAtATime() {
            verify(pageOne.visible)
            verify(!pageTwo.visible && !pageThree.visible)
            compare(pageTwo.width, pages.width)
            compare(pageTwo.height, pages.height)
            pages.currentIndex = 1
            tryVerify(() => pageTwo.visible && pageTwo.opacity === 1 && pageTwo.y === 0)
            verify(!pageOne.visible)
            pages.currentIndex = 2
            pages.currentIndex = 0
            tryVerify(() => pageOne.visible && pageOne.opacity === 1 && pageOne.y === 0)
            verify(!pageTwo.visible && !pageThree.visible)
        }

        function test_contentPanelBuildsInEitherTheme() {
            const panel = createTemporaryQmlObject(
                "import QtQuick\nimport GameScrcpy.Controls\nContentPanel { width: 300; height: 200; Item {} }", root)
            verify(panel.topLeftRadius > 0)
            Theme.dark = false
            Theme.dark = true
        }

        function test_inlineAlertActsOnlyWhenEnabled() {
            const action = findChild(alert, "action")
            verify(action.visible)
            mouseClick(action)
            compare(alertActions.count, 1)
            alert.actionEnabled = false
            mouseClick(action)
            compare(alertActions.count, 1)
        }

        function test_menuItemOfAChoiceMarksTheChosenOne() {
            const menu = createTemporaryQmlObject(
                'import GameScrcpy.Controls\nMenu { MenuItem { choice: true; chosen: true; text: "Portrait" } MenuItem { choice: true; text: "Landscape" } }',
                root)
            compare(menu.itemAt(0).shownIcon, "check")
            compare(menu.itemAt(1).shownIcon, "")
            menu.itemAt(1).chosen = true
            compare(menu.itemAt(1).shownIcon, "check")
        }

        function test_connectionStepperPhases() {
            const stepper = createTemporaryQmlObject("import GameScrcpy.Controls\nConnectionStepper { current: 2 }", root)
            compare([0, 1, 2, 3, 4].map(i => stepper.phaseOf(i)), ["done", "done", "current", "pending", "pending"])
            stepper.failed = true
            compare(stepper.phaseOf(2), "failed")
            stepper.failed = false
            stepper.current = stepper.steps.length
            verify([0, 1, 2, 3, 4].every(i => stepper.phaseOf(i) === "done"))
        }

        function test_workbenchLeavesTheCanvasBetweenItsBars() {
            const bench = createTemporaryQmlObject(
                "import QtQuick\nimport GameScrcpy.Controls\nWorkbench { width: 1000; height: 700 }", root)
            compare(bench.canvasRect.x, 0)
            compare(bench.canvasRect.y, Theme.size.workbenchBar)
            compare(bench.canvasRect.width, 1000 - Theme.size.workbenchSidebar)
            compare(bench.canvasRect.height, 700 - Theme.size.workbenchBar)
            verify(!findChild(bench, "workbenchBottom").visible)
            bench.bottomOpen = true
            compare(bench.canvasRect.height, 700 - Theme.size.workbenchBar - Theme.size.workbenchBottom)
            verify(findChild(bench, "workbenchBottom").visible)
            const tools = findChild(bench, "workbenchTools")
            tryVerify(() => tools.shown)
            compare(findChild(bench, "workbenchSidebar").width, Theme.size.workbenchSidebar)
        }

        function test_workbenchToolsStepAsideOnRequest() {
            const bench = createTemporaryQmlObject(
                "import QtQuick\nimport GameScrcpy.Controls\nWorkbench { width: 1000; height: 700; "
                + "toolsContent: [ToolbarButton { iconName: \"plus\"; text: \"Zoom in\" }] }", root)
            const tools = findChild(bench, "workbenchTools")
            tryVerify(() => tools.visible)
            bench.toolsShown = false
            verify(!tools.visible)
            bench.toolsShown = true
            verify(tools.visible)
            verify(tools.shown)
        }

        function test_workbenchLeftPanelShiftsTheCanvasOnlyWhenOpen() {
            const bench = createTemporaryQmlObject(
                "import QtQuick\nimport GameScrcpy.Controls\nWorkbench {\n    width: 1000; height: 700; bottomOpen: true\n"
                + "    toolsContent: [ToolbarButton { iconName: \"plus\"; text: \"Zoom in\" }]\n"
                + "    leftContent: [Item { objectName: \"files\"; anchors.fill: parent }]\n}", root)
            const left = findChild(bench, "workbenchLeft")
            const bottom = findChild(bench, "workbenchBottom")
            const tools = findChild(bench, "workbenchTools")
            tryVerify(() => tools.visible)
            verify(!left.visible)
            compare(bench.canvasRect.x, 0)
            compare(bench.canvasRect.width, 1000 - Theme.size.workbenchSidebar)
            compare(bottom.x, 0)
            compare(bottom.width, 1000 - Theme.size.workbenchSidebar)
            compare(tools.x, (1000 - Theme.size.workbenchSidebar - tools.width) / 2)

            bench.leftOpen = true
            verify(left.visible)
            compare(left.width, Theme.size.workbenchLeft)
            compare(left.y, Theme.size.workbenchBar)
            compare(left.height, 700 - Theme.size.workbenchBar)
            compare(findChild(bench, "files").width, Theme.size.workbenchLeft - Theme.size.stroke)
            compare(bench.canvasRect.x, Theme.size.workbenchLeft)
            compare(bench.canvasRect.y, Theme.size.workbenchBar)
            compare(bench.canvasRect.width, 1000 - Theme.size.workbenchSidebar - Theme.size.workbenchLeft)
            compare(bench.canvasRect.height, 700 - Theme.size.workbenchBar - Theme.size.workbenchBottom)
            compare(bottom.x, Theme.size.workbenchLeft)
            compare(bottom.width, bench.canvasRect.width)
            compare(tools.x, Theme.size.workbenchLeft + (bench.canvasRect.width - tools.width) / 2)
            compare(findChild(bench, "workbenchSidebar").width, Theme.size.workbenchSidebar)

            bench.leftOpen = false
            verify(!left.visible)
            compare(bench.canvasRect.x, 0)
            compare(bottom.x, 0)
        }

        function test_everyControlBuildsInEitherTheme_data() {
            return [
                { tag: "Button", qml: 'Button { tone: "primary"; iconName: "plus"; text: "Add" }' },
                { tag: "joined Buttons", qml: 'Row { Button { tone: "primary"; joined: "start"; text: "Launch" } Button { tone: "primary"; joined: "end"; iconName: "chevron-down" } }' },
                { tag: "icon Button", qml: 'Button { tone: "ghost"; iconName: "x"; description: "Close" }' },
                { tag: "Switch", qml: 'Switch { text: "On"; checked: true }' },
                { tag: "Checkbox", qml: 'Checkbox { text: "Some"; tristate: true; checkState: Qt.PartiallyChecked }' },
                { tag: "SegmentedControl", qml: 'SegmentedControl { model: [{ text: "A", icon: "monitor" }, { icon: "x", description: "Close" }] }' },
                { tag: "Tabs", qml: 'Tabs { model: ["A", { text: "B", count: 3 }] }' },
                { tag: "StatusPill", qml: 'StatusPill { tone: "live"; text: "Live" }' },
                { tag: "Keycap", qml: 'Keycap { text: "Space" }' },
                { tag: "large Keycap", qml: 'Keycap { large: true; text: "Right button"; iconName: "mouse" }' },
                { tag: "KeyCaptureField", qml: 'KeyCaptureField { keys: [{ text: "Ctrl" }, { text: "F" }]; conflictText: "Also bound" }' },
                { tag: "empty KeyCaptureField", qml: 'KeyCaptureField { clearable: false }' },
                { tag: "TextField", qml: 'TextField { text: "8"; suffix: "Mbps"; errorText: "Too high"; mono: true }' },
                { tag: "CodeField", qml: 'CodeField { text: "482"; description: "Pairing code" }' },
                { tag: "SearchField", qml: 'SearchField { placeholderText: "Search"; shortcut: "Ctrl K" }' },
                { tag: "Select", qml: 'Select { model: [{ name: "A" }, { name: "B" }]; textRole: "name" }' },
                { tag: "Slider", qml: 'Slider { value: 0.5; valueText: "50%" }' },
                { tag: "ticked Slider", qml: 'Slider { automatic: true; showValue: false; ticks: [{ position: 0, text: "1", current: true }] }' },
                { tag: "NumberField", qml: 'NumberField { value: 8; unit: "Mb/s"; numbers: NumberScale { decimals: 1 } }' },
                { tag: "prefixed NumberField", qml: 'NumberField { value: 76; prefix: "X"; unit: "%"; numbers: NumberScale { decimals: 1 } }' },
                { tag: "Menu", qml: 'Menu { MenuItem { text: "A"; iconName: "camera"; shortcut: "Ctrl S" } MenuSeparator {} MenuItem { text: "B"; checkable: true; danger: true } }' },
                { tag: "MenuItem with description", qml: 'Menu { MenuItem { iconName: "play"; text: "Launch"; description: "On the phone screen" } }' },
                { tag: "Tooltip", qml: 'Tooltip { text: "Settings"; shortcut: "Ctrl ," }' },
                { tag: "Card", qml: 'Card { active: true; Item { implicitWidth: 100; implicitHeight: 40 } }' },
                { tag: "SettingsRow", qml: 'SettingsRow { title: "A"; description: "B"; note: "C"; overridden: true; Switch {} }' },
                { tag: "InfoButton", qml: 'InfoButton { title: "A"; info: "B" }' },
                { tag: "Tile", qml: 'Tile { iconName: "monitor"; tone: "orange" }' },
                { tag: "large Tile", qml: 'Tile { iconName: "crosshair"; large: true }' },
                { tag: "bare TextField", qml: 'TextField { text: "Fire"; bare: true; dense: true }' },
                { tag: "FilterChip", qml: 'FilterChip { text: "All"; count: 3; iconName: "gamepad-2"; selected: true }' },
                { tag: "NavRail", qml: 'NavRail { model: [{ text: "A", icon: "house" }]; NavItem { text: "Help"; iconName: "circle-help" } }' },
                { tag: "Toast", qml: 'Toast { text: "Saved"; tone: "ok"; actionText: "Undo" }' },
                { tag: "ConnectionStepper", qml: 'ConnectionStepper { current: 1; failed: true }' },
                { tag: "Dialog", qml: 'Dialog { title: "Forget?"; danger: true }' },
                { tag: "InlineAlert", qml: 'InlineAlert { tone: "bad"; title: "Failed"; detail: "Why"; actionText: "Retry" }' }
            ]
        }

        function test_everyControlBuildsInEitherTheme(data) {
            const control = createTemporaryQmlObject("import QtQuick\nimport GameScrcpy.Controls\n" + data.qml, root)
            verify(control.implicitWidth > 0)
            verify(control.implicitHeight > 0)
            Theme.dark = false
            Theme.accentName = "lime"
            Theme.dark = true
            Theme.accentName = "ember"
        }
    }

    TestCase {
        name: "Icon"
        when: windowShown

        function test_unknownNameWarns() {
            ignoreWarning(/no icon named nonexistent/)
            createTemporaryQmlObject('import GameScrcpy.Icons\nIcon { name: "nonexistent" }', root)
        }
    }
}
