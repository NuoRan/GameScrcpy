pragma ComponentBehavior: Bound

import QtQuick
import QtQml.Models
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons
import GameScrcpy.App

Item {
    id: page

    property string current
    property string section
    property bool apiOpen: false
    readonly property bool searching: HelpLibrary.query.trim() !== ""
    readonly property bool reading: page.current !== ""
    readonly property var shown: page.visible && page.reading ? HelpLibrary.article(page.current, HelpLibrary.language) : ({})
    readonly property var homeData: page.visible && !page.reading ? HelpLibrary.home(HelpLibrary.language) : ({})
    readonly property bool inReference: page.current === "api" || page.current.startsWith("api/")
    readonly property var nav: {
        const contents = HelpLibrary.contents
        if (page.searching)
            return contents
        const list = [{ kind: "home" }]
        let namespaces = 0
        for (const entry of contents) {
            if (entry.kind === "reference" && entry.slug !== "api") {
                namespaces += 1
                if (page.apiOpen || page.inReference)
                    list.push(entry)
                continue
            }
            list.push(entry)
            if (entry.kind === "article" && entry.slug === page.current) {
                for (const part of page.shown.sections ?? [])
                    list.push({ kind: "toc", anchor: part.anchor, title: part.title })
            }
        }
        if (namespaces > 0 && !page.apiOpen && !page.inReference)
            list.push({ kind: "more", count: namespaces })
        return list
    }

    signal guideRequested()
    signal tourRequested(string key)

    function open(link: string) {
        const at = link.indexOf("#")
        const slug = at >= 0 ? link.slice(0, at) : link
        const anchor = at >= 0 ? link.slice(at + 1) : ""
        if (slug !== "" && !HelpLibrary.contains(slug))
            return
        page.current = slug
        page.section = ""
        content.contentX = 0
        content.contentY = 0
        if (slug.startsWith("api"))
            page.apiOpen = true
        if (anchor !== "")
            Qt.callLater(page.reveal, anchor)
    }

    function reveal(anchor: string) {
        const item = articleView.target(anchor)
        if (item === null)
            return
        page.section = anchor
        Qt.callLater(page.scrollTo, item)
    }

    function scrollTo(item: Item) {
        const point = item.mapToItem(column, 0, 0)
        const room = Math.max(0, content.contentHeight - content.height)
        content.contentY = Math.max(0, Math.min(room, column.y + point.y - Theme.space.md))
    }

    function follow(link: string) {
        if (link.startsWith("help:"))
            page.open(link.slice(5))
        else if (link === "action:guide")
            page.guideRequested()
        else if (link.startsWith("action:tour:"))
            page.tourRequested(link.slice(12))
        else if (link.startsWith("https:") || link.startsWith("http:"))
            Qt.openUrlExternally(link)
    }

    function track() {
        const sections = page.shown.sections ?? []
        if (sections.length === 0 || articleView.faq)
            return
        let found = sections[0].anchor
        const line = content.contentY + content.height * 0.3
        for (const part of sections) {
            const item = articleView.find(part.anchor)
            if (item === null)
                continue
            if (item.mapToItem(column, 0, 0).y + column.y <= line)
                found = part.anchor
        }
        if (content.contentY >= content.contentHeight - content.height - Theme.space.xs && content.contentY > 0)
            found = sections[sections.length - 1].anchor
        page.section = found
    }

    function focusSearch() {
        search.forceActiveFocus(Qt.ShortcutFocusReason)
    }

    component ContentsEntry: T.AbstractButton {
        id: navItem

        property string iconName
        property string tone
        property bool current: false
        property bool mono: false
        property bool code: navItem.mono
        property bool lead: false

        implicitWidth: implicitContentWidth + leftPadding + rightPadding
        implicitHeight: Math.max(navItem.mono ? Theme.size.listItem - Theme.space.xs : Theme.size.listItem + Theme.space.xxs,
                                 implicitContentHeight + topPadding + bottomPadding)
        leftPadding: navItem.mono ? Theme.size.tile + Theme.space.lg + Theme.space.xxs : Theme.space.sm
        rightPadding: Theme.space.sm
        topPadding: Theme.space.xs + Theme.space.xxs
        bottomPadding: Theme.space.xs + Theme.space.xxs
        spacing: Theme.space.sm + Theme.space.xxs
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        font.pixelSize: navItem.mono ? Theme.type.caption.size : Theme.type.label.size
        font.weight: navItem.current ? Font.DemiBold : Font.Normal
        font.family: navItem.code ? Theme.type.monoFamily : Application.font.family

        Accessible.role: Accessible.PageTab
        Accessible.selected: navItem.current

        contentItem: Row {
            spacing: navItem.spacing

            Item {
                anchors.verticalCenter: parent.verticalCenter
                visible: !navItem.mono
                width: Theme.size.tile - Theme.space.xs
                height: width

                Tile {
                    anchors.centerIn: parent
                    visible: !navItem.lead
                    iconName: navItem.iconName
                    tone: navItem.tone
                    scale: 0.86
                }

                Icon {
                    anchors.centerIn: parent
                    visible: navItem.lead
                    name: navItem.iconName
                    size: Theme.size.iconSmall
                    color: navItem.current ? Theme.colors.accentText : Theme.colors.textTertiary
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: navItem.availableWidth - (navItem.mono ? 0 : Theme.size.tile - Theme.space.xs + navItem.spacing)
                textFormat: Text.PlainText
                text: navItem.text
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                font: navItem.font
                color: navItem.current || navItem.hovered ? Theme.colors.textPrimary : Theme.colors.textSecondary
            }
        }

        background: Rectangle {
            id: navFace

            radius: Theme.radius.md
            color: navItem.current ? Theme.colors.statePress : "transparent"

            StateLayer {
                anchors.fill: parent
                radius: navFace.radius
                hovered: navItem.hovered && !navItem.current
                pressed: navItem.down
            }

            FocusRing {
                visible: navItem.visualFocus
                baseRadius: navFace.radius
            }
        }
    }

    component TocItem: T.AbstractButton {
        id: tocItem

        property bool current: false
        property bool hit: false

        implicitHeight: Theme.size.helpTocRow
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus

        Accessible.role: Accessible.Link
        Accessible.name: tocItem.text

        contentItem: Item {
            Rectangle {
                x: Theme.space.lg + Theme.space.xxs
                width: tocItem.current ? Theme.size.focusRing : Theme.size.stroke
                height: parent.height
                color: tocItem.current ? Theme.colors.accent : Theme.colors.strokeDefault
            }

            Row {
                x: Theme.space.x3l + Theme.space.xs
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - x - Theme.space.sm
                spacing: Theme.space.xs

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: tocItem.hit
                    name: "search"
                    size: Theme.size.keyHintTag - Theme.space.xxs
                    color: Theme.colors.textTertiary
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - (tocItem.hit ? Theme.size.keyHintTag : 0)
                    textFormat: Text.PlainText
                    text: tocItem.text
                    elide: Text.ElideRight
                    color: tocItem.current || tocItem.hovered ? Theme.colors.textPrimary : Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size + 0.5
                    font.weight: tocItem.current ? Font.Medium : Font.Normal
                }
            }
        }

        background: FocusRing {
            visible: tocItem.visualFocus
            baseRadius: Theme.radius.sm
        }
    }

    Item {
        id: sideNav

        width: Theme.size.sideNav
        anchors.top: parent.top
        anchors.bottom: parent.bottom

        SearchField {
            id: search

            objectName: "helpSearch"
            x: Theme.space.md + Theme.space.xxs
            y: Theme.space.xl
            width: parent.width - x - Theme.space.md
            placeholderText: qsTr("Search help")
            text: HelpLibrary.query
            Keys.onEscapePressed: HelpLibrary.query = ""
            onTextEdited: HelpLibrary.query = text
            onAccepted: {
                const first = HelpLibrary.contents.find(entry => entry.slug !== undefined || entry.of !== undefined)
                if (first !== undefined)
                    page.open(first.kind === "section" ? first.of + "#" + first.anchor : first.slug)
            }
        }

        ListView {
            id: contentsList

            objectName: "helpContents"
            anchors.top: search.bottom
            anchors.topMargin: Theme.space.sm
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.space.lg
            x: search.x
            width: search.width
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: page.nav

            delegate: DelegateChooser {
                role: "kind"

                DelegateChoice {
                    roleValue: "home"

                    ContentsEntry {
                        objectName: "helpHome"
                        width: ListView.view.width
                        lead: true
                        iconName: "house"
                        text: qsTr("Home")
                        current: !page.reading
                        onClicked: page.open("")
                    }
                }
                DelegateChoice {
                    roleValue: "group"

                    Text {
                        required property var modelData

                        width: ListView.view.width
                        leftPadding: Theme.space.sm + Theme.space.xxs
                        topPadding: Theme.space.lg
                        bottomPadding: Theme.space.sm - Theme.space.xxs
                        textFormat: Text.PlainText
                        text: modelData.label ?? ""
                        elide: Text.ElideRight
                        color: Theme.colors.textTertiary
                        font.pixelSize: Theme.type.micro.size + 0.5
                        font.weight: Font.DemiBold
                        font.letterSpacing: 0.3
                    }
                }
                DelegateChoice {
                    roleValue: "article"

                    ContentsEntry {
                        required property var modelData

                        objectName: "helpEntry"
                        width: ListView.view.width
                        text: modelData.title ?? ""
                        iconName: modelData.icon ?? ""
                        tone: modelData.tile ?? ""
                        current: modelData.slug === page.current
                        onClicked: page.open(modelData.slug)
                    }
                }
                DelegateChoice {
                    roleValue: "reference"

                    ContentsEntry {
                        required property var modelData
                        readonly property bool overview: modelData.slug === "api"

                        objectName: "helpEntry"
                        width: ListView.view.width
                        mono: !overview
                        code: !overview && /^[A-Za-z_$][\w$]*$/.test(modelData.title ?? "")
                        text: overview ? qsTr("API overview") : modelData.title ?? ""
                        iconName: "terminal"
                        tone: "gray"
                        current: modelData.slug === page.current
                        onClicked: page.open(modelData.slug)
                    }
                }
                DelegateChoice {
                    roleValue: "toc"

                    TocItem {
                        required property var modelData

                        objectName: "helpToc"
                        width: ListView.view.width
                        text: modelData.title ?? ""
                        current: modelData.anchor === page.section
                        onClicked: page.reveal(modelData.anchor)
                    }
                }
                DelegateChoice {
                    roleValue: "section"

                    TocItem {
                        required property var modelData

                        objectName: "helpHit"
                        width: ListView.view.width
                        hit: true
                        text: modelData.title ?? ""
                        onClicked: page.open(modelData.of + "#" + modelData.anchor)
                    }
                }
                DelegateChoice {
                    roleValue: "more"

                    T.AbstractButton {
                        id: moreItem

                        required property var modelData

                        objectName: "helpMoreApi"
                        width: ListView.view.width
                        implicitHeight: Theme.size.helpTocRow + Theme.space.xxs
                        hoverEnabled: true
                        onClicked: page.apiOpen = true

                        Accessible.role: Accessible.Button
                        Accessible.name: moreText.text

                        contentItem: Row {
                            leftPadding: Theme.size.tile + Theme.space.lg + Theme.space.xxs
                            spacing: Theme.space.xs

                            Text {
                                id: moreText

                                anchors.verticalCenter: parent.verticalCenter
                                textFormat: Text.PlainText
                                text: qsTr("%n namespace(s)", "", moreItem.modelData.count ?? 0)
                                color: moreItem.hovered ? Theme.colors.textPrimary : Theme.colors.textTertiary
                                font.pixelSize: Theme.type.caption.size
                            }

                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "chevron-down"
                                size: Theme.size.keyHintTag - Theme.space.xxs
                                color: Theme.colors.textTertiary
                            }
                        }

                        background: FocusRing {
                            visible: moreItem.visualFocus
                            baseRadius: Theme.radius.sm
                        }
                    }
                }
            }
        }

        Text {
            objectName: "helpNoMatch"
            anchors.top: search.bottom
            anchors.topMargin: Theme.space.lg
            x: search.x + Theme.space.sm + Theme.space.xxs
            width: search.width - Theme.space.sm - Theme.space.xxs
            visible: page.searching && contentsList.count === 0
            textFormat: Text.PlainText
            text: qsTr("No article matches “%1”.").arg(HelpLibrary.query.trim())
            wrapMode: Text.Wrap
            color: Theme.colors.textSecondary
            font.pixelSize: Theme.type.label.size
        }

        Rectangle {
            anchors.right: parent.right
            width: Theme.size.stroke
            height: parent.height
            color: Theme.colors.strokeSubtle
        }
    }

    Flickable {
        id: content

        anchors.left: sideNav.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        contentWidth: width
        contentHeight: column.y + column.implicitHeight + Theme.space.x6l
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        onContentYChanged: page.track()
        onContentHeightChanged: Qt.callLater(page.track)

        Column {
            id: column

            x: Math.max(Theme.space.xxl, (content.width - width) / 2)
            y: Theme.space.x3l - Theme.space.xxs
            width: Math.min(content.width - 2 * (Theme.space.x4l + Theme.space.xs), Theme.size.readableWidth)

            HelpHome {
                id: homeView

                objectName: "helpHomeView"
                visible: !page.reading
                width: parent.width
                homeData: page.homeData
                onLinkActivated: link => page.follow(link)
            }

            HelpArticle {
                id: articleView

                objectName: "helpArticleView"
                visible: page.reading
                width: Math.min(parent.width, Theme.size.helpArticle)
                shown: page.shown
                onLinkActivated: link => page.follow(link)
            }
        }
    }
}
