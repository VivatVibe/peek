import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Confirmation dialog shown when pressing Q with unexported edits.
Item {
    id: dialog

    signal cancelRequested()
    signal exportRequested()
    signal discardQuitRequested()

    anchors.fill: parent

    Rectangle {
        anchors.fill: parent
        color: "#b0000000"
        MouseArea {
            anchors.fill: parent
            onClicked: dialog.cancelRequested()
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 420)
        height: contentCol.implicitHeight + 40
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
            spacing: 14

            Text {
                Layout.fillWidth: true
                text: qsTr("Unsaved Edits")
                color: "#f4f4f6"
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("You have unexported edits on this image. Export a copy before quitting, or discard your changes?")
                color: "#b8b8c4"
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                spacing: 8

                Button {
                    id: discardBtn
                    Material.roundedScale: Math.min(4, theme.radius)
                    implicitHeight: 36
                    leftPadding: 14
                    rightPadding: 14
                    text: qsTr("Discard & Quit")
                    contentItem: Text {
                        text: discardBtn.text
                        color: "#ff9ea8"
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: Math.min(4, theme.radius)
                        color: discardBtn.hovered ? "#422026" : "#2c171b"
                        border.color: "#7a2e38"
                        border.width: 1
                    }
                    onClicked: dialog.discardQuitRequested()
                }

                Item { Layout.fillWidth: true }

                Button {
                    id: stayBtn
                    Material.roundedScale: Math.min(4, theme.radius)
                    implicitHeight: 36
                    leftPadding: 12
                    rightPadding: 12
                    text: qsTr("Cancel")
                    contentItem: Text {
                        text: stayBtn.text
                        color: "#c8c8d2"
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: Math.min(4, theme.radius)
                        color: stayBtn.hovered ? "#26262f" : "transparent"
                    }
                    onClicked: dialog.cancelRequested()
                }

                Button {
                    id: expFirstBtn
                    Material.roundedScale: Math.min(4, theme.radius)
                    implicitHeight: 36
                    leftPadding: 16
                    rightPadding: 16
                    text: qsTr("Export…")
                    contentItem: Text {
                        text: expFirstBtn.text
                        color: theme.accentForeground
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: Math.min(4, theme.radius)
                        color: theme.accent
                        opacity: expFirstBtn.down ? 0.85 : (expFirstBtn.hovered ? 0.92 : 1.0)
                    }
                    onClicked: dialog.exportRequested()
                }
            }
        }
    }
}
