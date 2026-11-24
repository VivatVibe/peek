import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Floating Adjust Color HUD docked on the right so edits update live on canvas.
Rectangle {
    id: panel

    signal closeRequested()

    width: 286
    implicitHeight: col.implicitHeight + 32
    radius: theme.radius
    color: "#eb18181e"
    border.color: "#32323c"
    border.width: 1

    MouseArea { anchors.fill: parent }

    function formatSigned(value) {
        void translator.language
        const v = Math.round(value * 100)
        const num = Number(Math.abs(v)).toLocaleString(Qt.locale(), 'f', 0)
        return v > 0 ? ("+" + num) : (v < 0 ? ("−" + num) : num)
    }

    function pushColor() {
        backend.setColorAdjustments(exposureSlider.value,
                                    contrastSlider.value,
                                    saturationSlider.value,
                                    brightnessSlider.value)
    }

    ColumnLayout {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true

            Text {
                Layout.fillWidth: true
                text: qsTr("Adjust Color")
                color: "#f4f4f6"
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }

            ToolButton {
                Material.roundedScale: Math.min(4, theme.radius)
                implicitWidth: 28
                implicitHeight: 28
                text: "✕"
                font.pixelSize: 13
                onClicked: {
                    backend.commitColorEdit()
                    panel.closeRequested()
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#2a2a32" }

        // Exposure.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; text: qsTr("Exposure"); color: "#c4c4d0"; font.pixelSize: 12 }
                Text { text: panel.formatSigned(backend.exposure); color: "#9a9aa6"; font.pixelSize: 12 }
            }
            Slider {
                id: exposureSlider
                Layout.fillWidth: true
                Material.accent: theme.accent
                from: -1.0; to: 1.0; stepSize: 0.02
                value: backend.exposure
                onPressedChanged: pressed ? backend.beginColorEdit() : backend.commitColorEdit()
                onMoved: panel.pushColor()
            }
        }

        // Contrast.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; text: qsTr("Contrast"); color: "#c4c4d0"; font.pixelSize: 12 }
                Text { text: panel.formatSigned(backend.contrast); color: "#9a9aa6"; font.pixelSize: 12 }
            }
            Slider {
                id: contrastSlider
                Layout.fillWidth: true
                Material.accent: theme.accent
                from: -1.0; to: 1.0; stepSize: 0.02
                value: backend.contrast
                onPressedChanged: pressed ? backend.beginColorEdit() : backend.commitColorEdit()
                onMoved: panel.pushColor()
            }
        }

        // Saturation.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; text: qsTr("Saturation"); color: "#c4c4d0"; font.pixelSize: 12 }
                Text { text: panel.formatSigned(backend.saturation); color: "#9a9aa6"; font.pixelSize: 12 }
            }
            Slider {
                id: saturationSlider
                Layout.fillWidth: true
                Material.accent: theme.accent
                from: -1.0; to: 1.0; stepSize: 0.02
                value: backend.saturation
                onPressedChanged: pressed ? backend.beginColorEdit() : backend.commitColorEdit()
                onMoved: panel.pushColor()
            }
        }

        // Brightness.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            RowLayout {
                Layout.fillWidth: true
                Text { Layout.fillWidth: true; text: qsTr("Brightness"); color: "#c4c4d0"; font.pixelSize: 12 }
                Text { text: panel.formatSigned(backend.brightness); color: "#9a9aa6"; font.pixelSize: 12 }
            }
            Slider {
                id: brightnessSlider
                Layout.fillWidth: true
                Material.accent: theme.accent
                from: -1.0; to: 1.0; stepSize: 0.02
                value: backend.brightness
                onPressedChanged: pressed ? backend.beginColorEdit() : backend.commitColorEdit()
                onMoved: panel.pushColor()
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#2a2a32" }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                id: autoBtn
                Layout.fillWidth: true
                implicitHeight: 32
                Material.roundedScale: Math.min(4, theme.radius)
                text: qsTr("Auto Levels")
                contentItem: Text {
                    text: autoBtn.text
                    color: theme.accent
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Math.min(4, theme.radius)
                    color: autoBtn.hovered ? "#24242f" : "#14141a"
                    border.color: theme.accent
                    border.width: 1
                }
                onClicked: backend.autoLevels()
            }

            Button {
                id: resetColorBtn
                implicitHeight: 32
                leftPadding: 12
                rightPadding: 12
                enabled: backend.hasColorAdjustments
                Material.roundedScale: Math.min(4, theme.radius)
                text: qsTr("Reset")
                contentItem: Text {
                    text: resetColorBtn.text
                    color: resetColorBtn.enabled ? "#d4d4de" : "#5a5a66"
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Math.min(4, theme.radius)
                    color: resetColorBtn.hovered && resetColorBtn.enabled ? "#26262f" : "#14141a"
                    border.color: "#2e2e38"
                    border.width: 1
                }
                onClicked: backend.resetColorAdjustments()
            }
        }
    }
}
