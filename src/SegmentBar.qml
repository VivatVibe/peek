import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Compact segmented selector for mutually exclusive print options.
Rectangle {
    id: root

    property var options: []      // [{ value: "fit", label: qsTr("Fit") }, ...]
    property string currentValue: ""
    signal selected(string value)

    implicitHeight: 34
    radius: Math.min(4, theme.radius)
    color: "#111115"
    border.color: "#2c2c34"
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.margins: 2
        spacing: 2

        Repeater {
            model: root.options

            Rectangle {
                required property var modelData
                readonly property bool active: modelData.value === root.currentValue

                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Math.max(0, Math.min(4, theme.radius) - 1)
                color: active ? theme.accent : (segArea.containsMouse ? "#23232b" : "transparent")

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    text: modelData.label
                    color: active ? theme.accentForeground : "#d6d6de"
                    font.pixelSize: 12
                    font.weight: active ? Font.DemiBold : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }

                MouseArea {
                    id: segArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.selected(modelData.value)
                }
            }
        }
    }
}
