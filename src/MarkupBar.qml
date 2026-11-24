import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Floating macOS Preview-style Markup Toolbar docked at the top of the canvas.
Rectangle {
    id: bar

    property string currentTool: "arrow"
    property color currentColor: "#ff453a"
    property string strokeSize: "m"
    signal closeRequested()

    implicitWidth: row.implicitWidth + 20
    implicitHeight: 42
    radius: Math.min(6, Math.max(theme.radius, 0))
    color: "#eb18181f"
    border.color: "#343440"
    border.width: 1

    MouseArea { anchors.fill: parent }

    readonly property var tools: [
        { id: "rect", icon: "□", tip: qsTr("Rectangle") },
        { id: "oval", icon: "○", tip: qsTr("Oval") },
        { id: "arrow", icon: "↗", tip: qsTr("Arrow") },
        { id: "line", icon: "╱", tip: qsTr("Line") },
        { id: "pen", icon: "✎", tip: qsTr("Freehand Draw") },
        { id: "text", icon: "T", tip: qsTr("Text Callout") },
        { id: "mosaic", icon: "▦", tip: qsTr("Pixelate") },
        { id: "redact", icon: "▬", tip: qsTr("Redact (Blackout)") }
    ]

    readonly property var swatches: ["#ff453a", "#ffd60a", "#30d158", "#0a84ff", "#ffffff", "#111115"]

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 4

        // Drawing tools.
        Repeater {
            model: bar.tools

            Rectangle {
                required property var modelData
                readonly property bool active: bar.currentTool === modelData.id
                width: 30
                height: 28
                radius: Math.min(4, theme.radius)
                color: active ? theme.accent : (tMa.containsMouse ? "#282834" : "transparent")

                Text {
                    anchors.centerIn: parent
                    text: modelData.icon
                    color: active ? theme.accentForeground : "#e2e2ec"
                    font.pixelSize: 14
                    font.weight: active ? Font.Bold : Font.Normal
                }

                ToolTip.visible: tMa.containsMouse
                ToolTip.text: modelData.tip

                MouseArea {
                    id: tMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: bar.currentTool = modelData.id
                }
            }
        }

        Rectangle { width: 1; height: 20; color: "#32323d"; Layout.leftMargin: 3; Layout.rightMargin: 3 }

        // Color swatches (true circles keep fixed radius).
        Repeater {
            model: bar.swatches

            Rectangle {
                required property string modelData
                readonly property bool selected: Qt.colorEqual(bar.currentColor, modelData)
                width: 18
                height: 18
                radius: 9
                color: modelData
                border.color: selected ? theme.accent : "#5a5a68"
                border.width: selected ? 2 : 1

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: bar.currentColor = modelData
                }
            }
        }

        Rectangle { width: 1; height: 20; color: "#32323d"; Layout.leftMargin: 3; Layout.rightMargin: 3 }

        // Stroke thickness presets (S / M / L).
        Repeater {
            model: [
                { id: "s", label: "S" },
                { id: "m", label: "M" },
                { id: "l", label: "L" }
            ]

            Rectangle {
                required property var modelData
                readonly property bool active: bar.strokeSize === modelData.id
                width: 24
                height: 24
                radius: Math.min(4, theme.radius)
                color: active ? theme.accent : (sMa.containsMouse ? "#282834" : "transparent")

                Text {
                    anchors.centerIn: parent
                    text: modelData.label
                    color: active ? theme.accentForeground : "#d4d4de"
                    font.pixelSize: 11
                    font.weight: Font.DemiBold
                }

                MouseArea {
                    id: sMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: bar.strokeSize = modelData.id
                }
            }
        }

        Rectangle {
            visible: backend.annotationCount > 0
            width: 1; height: 20; color: "#32323d"; Layout.leftMargin: 3; Layout.rightMargin: 3
        }

        Text {
            visible: backend.annotationCount > 0
            text: qsTr("Clear")
            color: "#ff9aa4"
            font.pixelSize: 11
            font.weight: Font.Medium
            Layout.leftMargin: 2
            Layout.rightMargin: 4
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: backend.clearAnnotations()
            }
        }

        Text {
            text: "✕"
            color: "#9a9aa6"
            font.pixelSize: 13
            Layout.leftMargin: 4
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: bar.closeRequested()
            }
        }
    }
}
