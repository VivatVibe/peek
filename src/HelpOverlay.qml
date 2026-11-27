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
        { key: "E / Ctrl+Shift+S", label: qsTr("Export / Convert Format") },
        { key: "O / Ctrl+O", label: qsTr("Open Image or PDF") },
        { key: "Tab", label: qsTr("Toggle Thumbnail Sidebar") },
        { key: "B", label: qsTr("Toggle PDF Outline / TOC") },
        { key: "Ctrl+F / /", label: qsTr("Find Text in PDF") },
        { key: "F3 / Shift+F3", label: qsTr("Next / Previous Match") },
        { key: "X", label: qsTr("Toggle PDF Text Selection") },
        { key: "Ctrl+↑ / Ctrl+↓", label: qsTr("Move Page Up / Down") },
        { key: "Ctrl+Shift+M / Del", label: qsTr("Merge PDF / Remove Page") },
        { key: "S / Shift+Drag", label: qsTr("Select Region to Crop or Print") },
        { key: "M", label: qsTr("Toggle Markup Toolbar") },
        { key: "I", label: qsTr("Toggle Inspector (EXIF / PDF)") },
        { key: "Ctrl+K", label: qsTr("Crop to Selection") },
        { key: "Ctrl+C", label: qsTr("Copy Image or Selection") },
        { key: "Ctrl+Shift+R", label: qsTr("Adjust Size (Resize)") },
        { key: "Ctrl+Shift+C", label: qsTr("Adjust Color") },
        { key: "← / →", label: qsTr("Previous / Next Image") },
        { key: "+ / − / 0 / 1", label: qsTr("Zoom In / Out / Fit / 100%") },
        { key: "[ / ]", label: qsTr("Rotate Left / Right") },
        { key: "Shift+H / Shift+V", label: qsTr("Flip Horizontal / Vertical") },
        { key: "Ctrl+Z / Ctrl+Shift+Z", label: qsTr("Undo / Redo") },
        { key: "T / ? / Q", label: qsTr("Grid / Help / Quit") }
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

        MouseArea {
            anchors.fill: parent
            onWheel: function(wheel) { wheel.accepted = true }
        }

        ColumnLayout {
            id: contentCol
            anchors.fill: parent
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

            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: shortcutCol.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                ColumnLayout {
                    id: shortcutCol
                    width: parent.width
                    spacing: 10

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
    }
}
