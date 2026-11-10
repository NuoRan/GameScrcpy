pragma ComponentBehavior: Bound

import QtQuick
import GameScrcpy.Theme
import GameScrcpy.Controls
import GameScrcpy.App

SettingCard {
    id: card

    property bool copied: false
    property string skillNote: ""

    title: qsTr("AI clients")
    iconName: "sparkles"
    tone: "azure"

    SwitchSetting {
        id: allowed

        objectName: "mcpEnabled"
        path: "mcp.enabled"
        fact: McpServer.problem !== "" ? McpServer.problem
              : McpServer.connected.length > 0 ? qsTr("Connected now: %1").arg(McpServer.connected.join(qsTr(", ", "list separator")))
              : ""
    }

    SettingsRow {
        objectName: "mcpClients"
        width: parent ? parent.width : implicitWidth
        visible: allowed.matched
        divider: true
        title: qsTr("Approved clients")
        description: qsTr("The clients you allowed and asked GameScrcpy to remember. They connect without asking again; "
                          + "removing one disconnects it at once.")
        note: McpServer.clients.length === 0
              ? qsTr("None yet. A client is listed here when you allow it with Remember this client ticked.") : ""
        below: Repeater {
            model: McpServer.clients

            delegate: Item {
                id: client

                required property string modelData

                width: parent ? parent.width : implicitWidth
                height: Math.max(clientName.implicitHeight, remove.implicitHeight)

                Text {
                    id: clientName

                    anchors.left: parent.left
                    anchors.right: remove.left
                    anchors.rightMargin: Theme.space.md
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: client.modelData
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.body.size
                    elide: Text.ElideRight
                }

                Button {
                    id: remove

                    objectName: "mcpRevoke"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    tone: "ghost"
                    small: true
                    iconName: "unlink"
                    text: qsTr("Remove")
                    Accessible.description: client.modelData
                    onClicked: McpServer.revoke(client.modelData)
                }
            }
        }
    }

    SettingsRow {
        objectName: "mcpAllowedNow"
        width: parent ? parent.width : implicitWidth
        visible: allowed.matched && McpServer.allowedNow.length > 0
        divider: true
        title: qsTr("Allowed without asking")
        description: qsTr("Until GameScrcpy closes, these clients run scripts or use tools without asking you each "
                          + "time. Take one back to be asked again.")
        below: Repeater {
            model: McpServer.allowedNow

            delegate: Item {
                id: grant

                required property var modelData

                width: parent ? parent.width : implicitWidth
                height: Math.max(grantText.implicitHeight, takeBack.implicitHeight)

                Text {
                    id: grantText

                    anchors.left: parent.left
                    anchors.right: takeBack.left
                    anchors.rightMargin: Theme.space.md
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                    text: grant.modelData.kind === "tool"
                          ? qsTr("%1 · uses %2").arg(grant.modelData.client).arg(grant.modelData.what)
                          : qsTr("%1 · runs scripts on %2").arg(grant.modelData.client).arg(grant.modelData.what)
                    color: Theme.colors.textPrimary
                    font.pixelSize: Theme.type.body.size
                    elide: Text.ElideRight
                }

                Button {
                    id: takeBack

                    objectName: "mcpEndAllowed"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    tone: "ghost"
                    small: true
                    iconName: "rotate-ccw"
                    text: qsTr("Take back")
                    Accessible.description: grant.modelData.client
                    onClicked: McpServer.endAllowed(grant.modelData.client, grant.modelData.key)
                }
            }
        }
    }

    SettingsRow {
        objectName: "mcpConfiguration"
        width: parent ? parent.width : implicitWidth
        visible: allowed.matched
        divider: true
        title: qsTr("Configuration for AI clients")
        description: qsTr("Copies what an AI client, such as Claude Desktop or Cursor, needs to start GameScrcpy's MCP "
                          + "server. Paste it into the client's MCP settings.")
        note: card.copied ? qsTr("Copied. Paste it into the AI client's MCP settings.") : ""

        Button {
            objectName: "mcpCopy"
            small: true
            iconName: "copy"
            text: qsTr("Copy configuration")
            onClicked: card.copied = McpServer.copyConfiguration()
        }
    }

    SettingsRow {
        objectName: "mcpSkill"
        width: parent ? parent.width : implicitWidth
        visible: allowed.matched
        divider: true
        title: qsTr("Skill for AI coding apps")
        description: qsTr("Installs a short guide that teaches Cursor and Claude Code how to work with GameScrcpy: what "
                          + "to ask you first, which tools to use and how to check their work.")
        note: card.skillNote

        Row {
            spacing: Theme.space.sm

            Button {
                objectName: "mcpInstallSkill"
                small: true
                iconName: "import"
                text: qsTr("Install")
                onClicked: card.skillNote = McpServer.installSkill()
            }

            Button {
                objectName: "mcpCopySkill"
                small: true
                tone: "ghost"
                iconName: "copy"
                text: qsTr("Copy")
                onClicked: card.skillNote = McpServer.copySkill()
                                             ? qsTr("Copied. Paste it into your AI app's skills.") : ""
            }
        }
    }
}
