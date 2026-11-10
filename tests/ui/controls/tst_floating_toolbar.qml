import QtQuick
import QtTest
import GameScrcpy.Theme
import GameScrcpy.Controls

Item {
    id: root

    width: 640
    height: 480

    FloatingToolbar {
        id: toolbar

        x: (root.width - width) / 2
        y: Theme.space.lg

        ToolbarButton {
            id: back

            iconName: "arrow-left"
            text: "Back"
            shortcut: "Esc"
        }
        ToolbarSeparator {}
        ToolbarButton {
            id: mute

            iconName: "volume-2"
            text: "Mute"
            checkable: true
        }
        ToolbarButton {
            id: unavailable

            iconName: "camera"
            text: "Screenshot"
            enabled: false
        }
        ToolbarButton {
            id: diagnostics

            iconName: "activity"
            text: "Diagnostics"
        }
    }

    StatusCapsule {
        id: capsule

        x: root.width - width - Theme.space.lg
        y: root.height - height - Theme.space.lg
    }

    SignalSpy {
        id: backClicks

        target: back
        signalName: "clicked"
    }
    SignalSpy {
        id: unavailableClicks

        target: unavailable
        signalName: "clicked"
    }
    SignalSpy {
        id: diagnosticsClicks

        target: diagnostics
        signalName: "clicked"
    }

    TestCase {
        name: "FloatingToolbar"
        when: windowShown

        function moveAway() {
            mouseMove(root, Theme.space.xs, root.height - 1)
        }

        function showToolbar() {
            toolbar.reveal()
            tryCompare(toolbar, "opacity", 1)
        }

        function init() {
            failOnWarning(/.?/)
            moveAway()
            root.forceActiveFocus()
            toolbar.pinned = false
            toolbar.hideDelay = Theme.motion.toolbarHideDelay
            toolbar.conceal()
            tryVerify(() => !toolbar.visible)
            mute.checked = false
            diagnostics.selected = false
            backClicks.clear()
            unavailableClicks.clear()
            diagnosticsClicks.clear()
            capsule.framesPerSecond = -1
            capsule.latencyMs = -1
            capsule.transport = ""
            capsule.profileName = ""
            capsule.cameraLocked = false
            capsule.problem = ""
        }

        function test_capsuleMarksTheLockedCamera() {
            const mark = findChild(capsule, "lockMark")
            verify(!capsule.visible)
            capsule.cameraLocked = true
            verify(capsule.visible)
            verify(mark.visible)
            compare(findChild(capsule, "lockText").text, "Camera locked")
            compare(capsule.Accessible.name, "Camera locked")
            verify(!findChild(capsule, "text").visible)
            capsule.transport = "USB"
            verify(findChild(capsule, "text").visible)
            compare(capsule.Accessible.name, "Camera locked · USB")
            capsule.cameraLocked = false
            verify(!mark.visible)
            verify(capsule.visible)
        }

        function test_capsuleLeadsWithAProblem() {
            const mark = findChild(capsule, "problemMark")
            verify(!capsule.visible)
            verify(!mark.visible)
            capsule.problem = "Delay is high"
            verify(capsule.visible)
            verify(mark.visible)
            compare(findChild(capsule, "problemText").text, "Delay is high")
            compare(capsule.Accessible.name, "Delay is high")
            capsule.latencyMs = 96
            capsule.transport = "WiFi"
            compare(capsule.Accessible.name, "Delay is high · 96 ms · WiFi")
            capsule.problem = ""
            verify(!mark.visible)
            verify(capsule.visible)
        }

        function test_capsuleNamesTheProfileFirst() {
            const text = findChild(capsule, "text")
            capsule.profileName = "Shooter"
            verify(capsule.visible)
            compare(text.text, "Shooter")
            capsule.framesPerSecond = 60
            capsule.transport = "USB"
            compare(text.text, "Shooter · 60 fps · USB")
        }

        function test_compactButtonIsNarrowAndStaysRound() {
            const chevron = createTemporaryQmlObject(
                "import GameScrcpy.Controls\nToolbarButton { compact: true; iconName: \"chevron-down\"; text: \"Options\" }", root)
            compare(chevron.implicitWidth, Theme.size.toolbarChevron)
            compare(chevron.implicitHeight, Theme.size.toolbarButton)
            compare(chevron.background.radius, Theme.size.toolbarChevron / 2)
            compare(back.background.radius, Theme.size.toolbarButton / 2)
        }

        function test_keyHintGrowsWithItsTextAndShowsItsTag() {
            const hint = createTemporaryQmlObject("import GameScrcpy.Controls\nKeyHint { text: \"W\" }", root)
            compare(hint.implicitHeight, Theme.size.keyHint)
            compare(hint.implicitWidth, Theme.size.keyHint)
            verify(!findChild(hint, "tag").visible)
            const wide = hint.implicitWidth
            hint.text = "Space"
            tryVerify(() => hint.implicitWidth > wide)
            hint.small = true
            compare(hint.implicitHeight, Theme.size.keyHintSmall)
            hint.tagIcon = "refresh-cw"
            verify(findChild(hint, "tag").visible)
            compare(hint.Accessible.name, "Space")
        }

        function test_keyHintShapes_data() {
            return [
                { tag: "tap", shape: "tap", mark: "" },
                { tag: "long press", shape: "longPress", mark: "holdArc" },
                { tag: "turbo", shape: "turbo", mark: "echo" },
                { tag: "camera", shape: "camera", mark: "reticle" },
                { tag: "free look", shape: "freeLook", mark: "lens" },
                { tag: "skill", shape: "aim", mark: "castRing" },
                { tag: "zoom in", shape: "zoomIn", mark: "chevrons" },
                { tag: "zoom out", shape: "zoomOut", mark: "chevrons" }
            ]
        }

        function test_keyHintShapes(data) {
            const hint = createTemporaryQmlObject("import GameScrcpy.Controls\nKeyHint { text: \"Q\"; shape: \"" + data.shape + "\" }", root)
            compare(hint.implicitHeight, Theme.size.keyHintRound)
            if (data.shape === "freeLook") {
                compare(hint.implicitWidth, 2 * Theme.size.keyHintRound)
                compare(hint.border.width, 0)
            } else {
                compare(hint.implicitWidth, Theme.size.keyHintRound)
                compare(hint.radius, hint.height / 2)
            }
            for (const mark of ["holdArc", "echo", "echoFar", "reticle", "lens", "castRing", "chevrons"]) {
                const shown = mark === data.mark || (data.mark === "echo" && mark === "echoFar")
                compare(findChild(hint, mark).visible, shown, mark)
            }
            hint.text = "Ctrl+Shift+F"
            tryVerify(() => hint.implicitWidth > Theme.size.keyHintRound)
        }

        function test_keyHintKeepsTheSquareKeyByDefault() {
            const hint = createTemporaryQmlObject("import GameScrcpy.Controls\nKeyHint { text: \"W\"; small: true }", root)
            compare(hint.shape, "key")
            compare(hint.radius, Theme.radius.md)
            compare(hint.implicitHeight, Theme.size.keyHintSmall)
            verify(!findChild(hint, "mouseGlyph").visible)
        }

        function test_keyHintDrawsMouseButtons_data() {
            return [
                { tag: "left", button: "left", parts: ["pressedHalf"] },
                { tag: "right", button: "right", parts: ["pressedHalf"] },
                { tag: "middle", button: "middle", parts: ["wheel"] },
                { tag: "wheel up", button: "wheelUp", parts: ["wheel", "wheelArrow"] },
                { tag: "wheel left", button: "wheelLeft", parts: ["wheel", "wheelArrow"] },
                { tag: "back", button: "back", parts: ["sideButton"] }
            ]
        }

        function test_keyHintDrawsMouseButtons(data) {
            const hint = createTemporaryQmlObject("import GameScrcpy.Controls\nKeyHint { shape: \"tap\"; mouseButton: \"" + data.button + "\" }", root)
            const glyph = findChild(hint, "mouseGlyph")
            verify(glyph.visible)
            compare(hint.implicitWidth, Theme.size.keyHintRound)
            for (const part of ["pressedHalf", "wheel", "wheelArrow", "sideButton"])
                compare(findChild(glyph, part).visible, data.parts.indexOf(part) >= 0, part)
            hint.text = "Shift+"
            tryVerify(() => hint.implicitWidth > Theme.size.keyHintRound)
        }

        function test_mappingPreviewPlacesItsMarkers() {
            const preview = createTemporaryQmlObject("import GameScrcpy.Controls\nMappingPreview {}", root)
            compare(preview.implicitWidth, Theme.size.previewLong)
            compare(preview.implicitHeight, Theme.size.previewShort)
            preview.markers = [{ type: "tap", x: 0.5, y: 0.25, rx: 0, ry: 0 },
                               { type: "wasd", x: 0.2, y: 0.8, rx: 0.1, ry: 0.15 }]
            const marker = findChild(preview, "marker")
            verify(marker !== null)
            compare(marker.x, preview.width / 2)
            compare(marker.y, preview.height / 4)
            preview.portrait = true
            compare(preview.implicitWidth, Theme.size.previewPortrait)
        }

        function test_edgeGlowFadesInWhileActive() {
            const glow = createTemporaryQmlObject("import GameScrcpy.Controls\nEdgeGlow { width: 200; height: 100 }", root)
            verify(!glow.visible)
            glow.active = true
            tryCompare(glow, "opacity", 1)
            compare(findChild(glow, "edge").border.width, Theme.size.edgeLine)
            glow.active = false
            tryVerify(() => !glow.visible)
        }

        function test_flatToolbarDrawsNoSurface() {
            const flat = createTemporaryQmlObject(
                "import GameScrcpy.Controls\nFloatingToolbar { flat: true; ToolbarButton { iconName: \"pin\"; text: \"Pin\" } }", root)
            flat.reveal()
            tryCompare(flat, "opacity", 1)
            verify(!findChild(flat, "face").visible)
            flat.flat = false
            verify(findChild(flat, "face").visible)
            compare(flat.implicitHeight, Theme.size.toolbar)
        }

        function test_toolbarStartsHidden() {
            const fresh = createTemporaryQmlObject("import GameScrcpy.Controls\nFloatingToolbar {}", root)
            verify(!fresh.shown)
            verify(!fresh.visible)
            compare(fresh.opacity, 0)
        }

        function test_revealShowsAndConcealHides() {
            toolbar.reveal()
            verify(toolbar.shown)
            tryVerify(() => toolbar.visible)
            tryCompare(toolbar, "opacity", 1)
            toolbar.conceal()
            verify(!toolbar.shown)
            tryVerify(() => !toolbar.visible)
        }

        function test_pinnedToolbarIgnoresConcealUntilUnpinned() {
            toolbar.hideDelay = 20
            toolbar.pinned = true
            toolbar.reveal()
            toolbar.conceal()
            verify(toolbar.shown)
            wait(100)
            verify(toolbar.shown)
            toolbar.pinned = false
            tryCompare(toolbar, "shown", false)
            tryVerify(() => !toolbar.visible)
        }

        function test_toolbarHidesByItselfAfterTheDelay() {
            toolbar.hideDelay = 50
            toolbar.reveal()
            verify(toolbar.shown)
            tryCompare(toolbar, "shown", false)
            tryCompare(toolbar, "visible", false)
        }

        function test_hoverKeepsTheToolbarShown() {
            toolbar.reveal()
            tryVerify(() => toolbar.visible)
            tryVerify(() => {
                if (!toolbar.hovered)
                    mouseMove(toolbar, toolbar.width / 2, toolbar.height / 2)
                return toolbar.hovered
            })
            toolbar.hideDelay = 50
            toolbar.conceal()
            wait(150)
            verify(toolbar.shown)
            moveAway()
            tryVerify(() => !toolbar.hovered)
            tryCompare(toolbar, "shown", false)
            tryVerify(() => !toolbar.visible)
        }

        function test_checkableButtonTogglesOnClick() {
            showToolbar()
            mouseClick(mute)
            verify(mute.checked)
            mouseClick(mute)
            verify(!mute.checked)
        }

        function test_selectedButtonLeavesTheStateToItsOwner() {
            showToolbar()
            verify(Qt.colorEqual(diagnostics.background.color, "transparent"))
            diagnostics.selected = true
            verify(Qt.colorEqual(diagnostics.background.color, Theme.colors.accentSubtle))
            mouseClick(diagnostics)
            compare(diagnosticsClicks.count, 1)
            verify(diagnostics.selected)
            verify(!diagnostics.checkable)
            verify(!diagnostics.checked)
            diagnostics.selected = false
            mouseClick(diagnostics)
            compare(diagnosticsClicks.count, 2)
            verify(!diagnostics.selected)
            verify(!diagnostics.checked)
        }

        function test_disabledButtonIgnoresClicks() {
            showToolbar()
            mouseClick(back)
            compare(backClicks.count, 1)
            mouseClick(unavailable)
            compare(unavailableClicks.count, 0)
        }

        function test_buttonTakesKeyboardFocusOnlyByTab() {
            showToolbar()
            mouseClick(back)
            verify(!back.activeFocus)
            back.forceActiveFocus(Qt.TabFocusReason)
            verify(back.visualFocus)
            keyClick(Qt.Key_Space)
            compare(backClicks.count, 2)
        }

        function test_toolTipNamesTheShortcut() {
            compare(findChild(back, "toolTip").text, "Back (Esc)")
            compare(findChild(mute, "toolTip").text, "Mute")
        }

        function test_capsuleShowsOnlyTheKnownValues() {
            const text = findChild(capsule, "text")
            verify(!capsule.visible)
            capsule.framesPerSecond = 59.6
            compare(text.text, "60 fps")
            verify(capsule.visible)
            capsule.transport = "USB"
            compare(text.text, "60 fps · USB")
            capsule.latencyMs = 12.4
            compare(text.text, "60 fps · 12 ms · USB")
            capsule.framesPerSecond = -1
            compare(text.text, "12 ms · USB")
            capsule.latencyMs = 0
            capsule.transport = ""
            compare(text.text, "0 ms")
            capsule.latencyMs = -1
            compare(text.text, "")
            verify(!capsule.visible)
        }

        function test_capsuleToastShowsItsIconInARoundBadge() {
            const toast = createTemporaryQmlObject(
                "import GameScrcpy.Controls\nToast { capsule: true; iconName: \"keyboard\" }", root)
            toast.show("Rest the pointer at the top to show the toolbar", "info")
            tryVerify(() => toast.opened)
            const badge = findChild(toast.contentItem, "iconBadge")
            verify(badge.visible)
            compare(badge.width, badge.height)
            compare(toast.background.radius, toast.height / 2)
            toast.iconName = ""
            verify(!badge.visible)
            compare(badge.width, 0)
            toast.close()
            tryVerify(() => !toast.visible)
        }

        function test_plainToastHasNoBadge() {
            const toast = createTemporaryQmlObject(
                "import GameScrcpy.Controls\nToast { iconName: \"keyboard\" }", root)
            toast.show("Saved", "ok")
            tryVerify(() => toast.opened)
            verify(!findChild(toast.contentItem, "iconBadge").visible)
            compare(toast.background.radius, Theme.radius.lg)
            toast.close()
            tryVerify(() => !toast.visible)
        }

        function test_longToastStaysInsideANarrowWindow() {
            const narrow = createTemporaryQmlObject("import QtQuick\nItem { width: 300; height: 200 }", root)
            const toast = createTemporaryQmlObject(
                "import GameScrcpy.Controls\nToast { capsule: true; iconName: \"triangle-alert\" }", root)
            toast.parent = narrow
            toast.show("Logic error: logic.ts line 3: Error: something went wrong far away from the start", "info")
            tryVerify(() => toast.opened)
            tryVerify(() => toast.width <= narrow.width - 2 * Theme.space.sm)
            verify(toast.x >= Theme.space.sm)
            verify(findChild(toast.contentItem, "message").truncated)
            narrow.width = 1000
            tryCompare(toast, "width", Theme.size.toastMaxWidth)
            toast.close()
            tryVerify(() => !toast.visible)
        }

        function test_buildsInEitherTheme_data() {
            return [
                { tag: "FloatingToolbar", qml: 'FloatingToolbar { ToolbarButton { iconName: "house"; text: "Home" } ToolbarSeparator {} ToolbarButton { iconName: "maximize-2"; text: "Full screen"; checkable: true; checked: true } }' },
                { tag: "ToolbarButton", qml: 'ToolbarButton { iconName: "rotate-cw"; text: "Rotate"; shortcut: "Ctrl R" }' },
                { tag: "ToolbarSeparator", qml: 'ToolbarSeparator {}' },
                { tag: "StatusCapsule", qml: 'StatusCapsule { framesPerSecond: 60; latencyMs: 12; transport: "USB"; profileName: "Shooter"; cameraLocked: true; problem: "Delay is high" }' },
                { tag: "KeyHint", qml: 'KeyHint { text: "L"; iconName: "mouse"; tagIcon: "history" }' },
                { tag: "turbo KeyHint", qml: 'KeyHint { shape: "turbo"; mouseButton: "left" }' },
                { tag: "modified mouse KeyHint", qml: 'KeyHint { small: true; text: "Shift+"; mouseButton: "wheelDown" }' },
                { tag: "long press KeyHint", qml: 'KeyHint { shape: "longPress"; text: "Ctrl+E" }' },
                { tag: "camera KeyHint", qml: 'KeyHint { shape: "camera"; text: "Tab" }' },
                { tag: "free look KeyHint", qml: 'KeyHint { shape: "freeLook"; text: "Alt" }' },
                { tag: "skill KeyHint", qml: 'KeyHint { shape: "aim"; text: "1" }' },
                { tag: "zoom KeyHint", qml: 'KeyHint { shape: "zoomOut"; small: true; text: "Wheel ↓" }' },
                { tag: "MappingPreview", qml: 'MappingPreview { markers: [{ type: "camera", x: 0.5, y: 0.5, rx: 0, ry: 0 }] }' }
            ]
        }

        function test_buildsInEitherTheme(data) {
            const control = createTemporaryQmlObject("import QtQuick\nimport GameScrcpy.Controls\n" + data.qml, root)
            verify(control.implicitWidth > 0)
            verify(control.implicitHeight > 0)
            Theme.dark = false
            Theme.reducedTransparency = true
            Theme.dark = true
            Theme.reducedTransparency = false
        }
    }
}
