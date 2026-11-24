import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Keyboard shortcuts reference overlay toggled with '?'.
Item {
    id: overlay

    signal closeRequested()

    anchors.fill: parent

    readonly property var shortcuts: [
        { key: "Space / Ctrl+P", label: qsTr("Open Print Sheet") },
        { key: "Enter", label: qsTr("Print (inside Print Sheet)") },
        { key: "Ctrl+S", label: qsTr("Save as PDF") },
        { key: "O / Ctrl+O", label: qsTr("Open Image") },
        { key: "← / →", label: qsTr("Previous / Next Image") },
        { key: "+ / −", label: qsTr("Zoom In / Out") },
        { key: "0 / F", label: qsTr("Fit to Window") },
        { key: "1", label: qsTr("Actual Size (100%)") },
        { key: "[ / ]", label: qsTr("Rotate Left / Right") },
        { key: "Shift+H / Shift+V", label: qsTr("Flip Horizontal / Vertical") },
        { key: "Ctrl+Z / Ctrl+Shift+Z", label: qsTr("Undo / Redo") },
        { key: "T", label: qsTr("Toggle Transparency Grid") },
        { key: "?", label: qsTr("Toggle Shortcuts Help") },
        { key: "Q", label: qsTr("Quit") }
    ]

    Rectangle {
        anchors.fill: parent
        color: "#b0000000"
        MouseArea {
            anchors.fill: parent
            onClicked: overlay.closeRequested()
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 520)
        height: Math.min(parent.height - 48, contentCol.implicitHeight + 40)
        radius: theme.radius
        color: "#18181d"
        border.color: "#2e2e36"
        border.width: 1

        MouseArea { anchors.fill: parent }

        ColumnLayout {
            id: contentCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 20
            spacing: 12

            RowLayout {
                Layout.fillWidth: true

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Keyboard Shortcuts")
                    color: "#f4f4f6"
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }

                ToolButton {
                    Material.roundedScale: Math.min(4, theme.radius)
                    implicitWidth: 32
                    implicitHeight: 32
                    text: "✕"
                    font.pixelSize: 14
                    onClicked: overlay.closeRequested()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: "#2a2a32"
            }

            Repeater {
                model: overlay.shortcuts

                RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 12

                    Text {
                        Layout.fillWidth: true
                        text: modelData.label
                        color: "#d4d4dc"
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }

                    Rectangle {
                        implicitWidth: keyText.implicitWidth + 16
                        implicitHeight: 24
                        radius: Math.min(4, theme.radius)
                        color: "#111115"
                        border.color: "#32323c"
                        border.width: 1

                        Text {
                            id: keyText
                            anchors.centerIn: parent
                            text: modelData.key
                            color: theme.accent
                            font.pixelSize: 11
                            font.family: "monospace"
                            font.weight: Font.Medium
                        }
                    }
                }
            }
        }
    }
}
