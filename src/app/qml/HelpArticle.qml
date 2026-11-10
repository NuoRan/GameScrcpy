pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.Icons

Column {
    id: article

    property var shown: ({})
    readonly property string layout: article.shown.layout ?? "guide"
    readonly property bool faq: article.layout === "faq"
    readonly property bool reference: article.layout === "reference"
    readonly property bool codeTitle: article.reference && /^[A-Za-z_$][\w$]*$/.test(article.shown.title ?? "")
    readonly property var functions: (article.shown.blocks ?? []).filter(block => block.kind === "function")
    readonly property var previousEntry: (article.shown.previous?.slug ?? "") !== "" ? article.shown.previous
                                       : article.reference || (article.shown.slug ?? "") === "" ? ({})
                                       : ({ slug: "", home: true, title: qsTr("Help home") })
    readonly property var nextEntry: article.shown.next ?? ({})
    readonly property bool hasNeighbour: (article.previousEntry.slug ?? "") !== "" || article.previousEntry.home === true
                                         || (article.nextEntry.slug ?? "") !== ""

    signal linkActivated(string link)

    function target(anchor: string): Item {
        return article.faq ? faqView.target(anchor) : body.target(anchor)
    }

    function find(anchor: string): Item {
        return article.faq ? null : body.find(anchor)
    }

    component Badge: Rectangle {
        id: badge

        property string iconName
        property string text
        property string tone

        implicitWidth: badgeRow.implicitWidth + 2 * Theme.space.sm
        implicitHeight: Theme.size.badge + Theme.space.xs
        radius: height / 2
        color: badge.tone === "ok" ? Theme.colors.successSubtle : Theme.colors.bgOverlay
        border.width: badge.tone === "ok" ? 0 : Theme.size.stroke
        border.color: Theme.colors.strokeDefault

        Row {
            id: badgeRow

            anchors.centerIn: parent
            spacing: Theme.space.xs + Theme.space.xxs

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: badge.iconName !== ""
                name: badge.iconName
                size: Theme.size.infoIcon - 1
                color: badge.tone === "ok" ? Theme.colors.successText : Theme.colors.textSecondary
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
                text: badge.text
                color: badge.tone === "ok" ? Theme.colors.successText : Theme.colors.textSecondary
                font.pixelSize: Theme.type.caption.size
                font.weight: Font.Medium
            }
        }
    }

    component Neighbour: T.AbstractButton {
        id: neighbour

        property var entry: ({})
        property bool after: false

        visible: (neighbour.entry.slug ?? "") !== "" || neighbour.entry.home === true
        implicitHeight: neighbourColumn.implicitHeight + 2 * Theme.space.md + Theme.space.xxs
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus

        Accessible.role: Accessible.Link
        Accessible.name: neighbourTitle.text

        background: Rectangle {
            id: neighbourFace

            radius: Theme.radius.xl - Theme.space.xxs
            color: "transparent"
            border.width: Theme.size.stroke
            border.color: neighbour.hovered ? Theme.colors.strokeStrong : Theme.colors.strokeDefault

            StateLayer {
                anchors.fill: parent
                radius: neighbourFace.radius
                hovered: neighbour.hovered
                pressed: neighbour.down
            }

            FocusRing {
                visible: neighbour.visualFocus
                baseRadius: neighbourFace.radius
            }
        }

        contentItem: Item {
            Column {
                id: neighbourColumn

                x: Theme.space.lg + Theme.space.xxs
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 2 * x
                spacing: Theme.space.xs

                Text {
                    width: parent.width
                    horizontalAlignment: neighbour.after ? Text.AlignRight : Text.AlignLeft
                    textFormat: Text.PlainText
                    text: neighbour.after ? qsTr("Next") : qsTr("Previous")
                    color: Theme.colors.textTertiary
                    font.pixelSize: Theme.type.caption.size
                }

                Row {
                    anchors.right: neighbour.after ? parent.right : undefined
                    spacing: Theme.space.sm
                    layoutDirection: neighbour.after ? Qt.RightToLeft : Qt.LeftToRight

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: neighbour.after ? "chevron-right" : "arrow-left"
                        size: Theme.size.iconSmall
                        color: Theme.colors.textSecondary
                    }

                    Text {
                        id: neighbourTitle

                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                        text: neighbour.entry.title ?? ""
                        color: Theme.colors.textPrimary
                        font.pixelSize: Theme.type.prose.size
                        font.weight: Font.DemiBold
                    }
                }
            }
        }
    }

    Row {
        spacing: Theme.space.xs + Theme.space.xxs

        Text {
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: article.shown.group ?? ""
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.caption.size
        }

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: "chevron-right"
            size: Theme.size.keyHintTagIcon + Theme.space.xxs
            color: Theme.colors.textTertiary
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            text: article.shown.title ?? ""
            color: Theme.colors.textTertiary
            font.pixelSize: Theme.type.caption.size
        }
    }

    Text {
        objectName: "helpTitle"
        width: parent.width
        topPadding: Theme.space.sm
        bottomPadding: Theme.space.sm + Theme.space.xxs
        textFormat: Text.PlainText
        text: article.shown.title ?? ""
        wrapMode: Text.Wrap
        color: Theme.colors.textPrimary
        font.family: article.codeTitle ? Theme.type.monoFamily : Application.font.family
        font.pixelSize: Theme.type.display.size + (article.codeTitle ? 0 : Theme.space.xxs)
        font.weight: Theme.type.display.weight
        font.letterSpacing: article.codeTitle ? 0 : -0.3
        lineHeight: Theme.type.display.lineHeight + Theme.space.xs
        lineHeightMode: Text.FixedHeight

        Accessible.role: Accessible.Heading
    }

    HelpText {
        objectName: "helpLead"
        visible: html !== ""
        width: Math.min(parent.width, Theme.size.helpProse - Theme.space.xl)
        html: article.shown.lead ?? ""
        font.pixelSize: Theme.type.lead.size
        lineSize: Theme.type.lead.lineHeight
        onLinkActivated: link => article.linkActivated(link)
    }

    Flow {
        width: parent.width
        topPadding: Theme.space.md + Theme.space.xxs
        spacing: Theme.space.sm

        Repeater {
            model: article.shown.meta ?? []

            delegate: Badge {
                required property var modelData

                iconName: modelData.icon ?? ""
                text: modelData.text ?? ""
            }
        }

        Badge {
            visible: !article.reference && !article.faq
            iconName: "timer"
            text: qsTr("About %n min", "", article.shown.minutes ?? 1)
        }

        Badge {
            visible: article.faq
            iconName: "circle-help"
            text: qsTr("%n question(s)", "", faqView.questions.length)
        }

        Badge {
            visible: article.reference && article.functions.length > 0
            iconName: "code-xml"
            text: qsTr("%n function(s)", "", article.functions.length)
        }
    }

    Item {
        width: parent.width
        visible: (article.shown.hero ?? "") !== ""
        height: visible ? heroPanel.height + Theme.space.lg + Theme.space.xxs : 0

        Rectangle {
            id: heroPanel

            readonly property real fit: Math.min(1, (width - 2 * Theme.space.xl) / Math.max(1, heroArt.implicitWidth))

            y: Theme.space.lg + Theme.space.xxs
            width: Math.min(parent.width, Theme.size.helpArticle)
            height: heroArt.implicitHeight * heroPanel.fit + 2 * Theme.space.lg
            radius: Theme.radius.xl
            color: Theme.colors.bgSunken
            border.width: Theme.size.stroke
            border.color: Theme.colors.strokeSubtle
            clip: true

            HelpFigure {
                id: heroArt

                x: (heroPanel.width - implicitWidth * heroPanel.fit) / 2
                y: Theme.space.lg
                width: implicitWidth
                height: implicitHeight
                transformOrigin: Item.TopLeft
                scale: heroPanel.fit
                name: article.shown.hero ?? ""
            }
        }
    }

    HelpBlocks {
        id: body

        objectName: "helpBlocks"
        visible: !article.faq
        width: parent.width
        model: article.faq ? [] : article.shown.blocks ?? []
        baseUrl: article.shown.baseUrl ?? ""
        onLinkActivated: link => article.linkActivated(link)
    }

    HelpFaq {
        id: faqView

        visible: article.faq
        width: parent.width
        categories: article.faq ? article.shown.categories ?? [] : []
        onLinkActivated: link => article.linkActivated(link)
    }

    Item {
        width: Math.min(parent.width, Theme.size.helpArticle)
        visible: article.hasNeighbour
        height: visible ? Theme.space.x5l + Theme.space.xxl + neighbours.height : 0

        Rectangle {
            y: Theme.space.x5l
            width: parent.width
            height: Theme.size.stroke
            color: Theme.colors.strokeSubtle
        }

        Item {
            id: neighbours

            y: Theme.space.x5l + Theme.space.xxl
            width: parent.width
            height: Math.max(previous.implicitHeight, next.implicitHeight)

            Neighbour {
                id: previous

                width: (parent.width - Theme.space.md) / 2
                height: parent.height
                entry: article.previousEntry
                onClicked: article.linkActivated("help:" + (previous.entry.slug ?? ""))
            }

            Neighbour {
                id: next

                x: parent.width - width
                width: (parent.width - Theme.space.md) / 2
                height: parent.height
                after: true
                entry: article.nextEntry
                onClicked: article.linkActivated("help:" + (next.entry.slug ?? ""))
            }
        }
    }
}
