import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Export / Convert Format modal sheet inspired by macOS Preview (File > Export).
Item {
    id: sheet

    signal closeRequested()

    anchors.fill: parent

    function formatBytes(bytes) {
        void translator.language
        if (bytes <= 0)
            return "—"
        if (bytes >= 1024 * 1024)
            return qsTr("%1 MB").arg(Number(bytes / (1024.0 * 1024.0)).toLocaleString(Qt.locale(), 'f', 2))
        if (bytes >= 1024)
            return qsTr("%1 KB").arg(Number(bytes / 1024.0).toLocaleString(Qt.locale(), 'f', 0))
        return qsTr("%1 B").arg(Number(bytes).toLocaleString(Qt.locale(), 'f', 0))
    }

    onVisibleChanged: {
        if (visible && backend.hasImage)
            backend.refreshExportEstimate()
    }

    Rectangle {
        anchors.fill: parent
        color: "#b0000000"
        MouseArea {
            anchors.fill: parent
            onClicked: sheet.closeRequested()
        }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 450)
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

            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Export Image")
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
                    onClicked: sheet.closeRequested()
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#2a2a32" }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: qsTr("Format")
                    color: "#b4b4c0"
                    font.pixelSize: 12
                    font.weight: Font.Medium
                }

                SegmentBar {
                    Layout.fillWidth: true
                    currentValue: backend.exportFormat
                    options: [
                        { value: "jpeg", label: "JPEG" },
                        { value: "png", label: "PNG" },
                        { value: "webp", label: "WebP" },
                        { value: "tiff", label: "TIFF" },
                        { value: "bmp", label: "BMP" },
                        { value: "pdf", label: "PDF" }
                    ]
                    onSelected: function(val) { backend.exportFormat = val }
                }
            }

            ColumnLayout {
                visible: backend.exportSupportsQuality
                Layout.fillWidth: true
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Quality")
                        color: "#b4b4c0"
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }
                    Text {
                        text: {
                            void translator.language
                            return qsTr("%1%").arg(Number(backend.exportQuality).toLocaleString(Qt.locale(), 'f', 0))
                        }
                        color: "#f4f4f6"
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }
                }

                Slider {
                    Layout.fillWidth: true
                    Material.accent: theme.accent
                    from: 10
                    to: 100
                    stepSize: 5
                    value: backend.exportQuality
                    onMoved: backend.exportQuality = Math.round(value)
                }
            }

            // Output details summary box.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 54
                radius: Math.min(4, theme.radius)
                color: "#111115"
                border.color: "#282832"
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: qsTr("Dimensions")
                            color: "#8e8e9a"
                            font.pixelSize: 11
                        }
                        Text {
                            text: {
                                void translator.language
                                const w = Number(backend.imageWidth).toLocaleString(Qt.locale(), 'f', 0)
                                const h = Number(backend.imageHeight).toLocaleString(Qt.locale(), 'f', 0)
                                return qsTr("%1 × %2 px").arg(w).arg(h)
                            }
                            color: "#f0f0f6"
                            font.pixelSize: 13
                            font.weight: Font.Medium
                        }
                    }

                    ColumnLayout {
                        spacing: 2
                        Text {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("Estimated Size")
                            color: "#8e8e9a"
                            font.pixelSize: 11
                        }
                        Text {
                            Layout.alignment: Qt.AlignRight
                            text: sheet.formatBytes(backend.estimatedExportBytes)
                            color: theme.accent
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#2a2a32" }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Item { Layout.fillWidth: true }

                Button {
                    id: cancelExpBtn
                    Material.roundedScale: Math.min(4, theme.radius)
                    implicitHeight: 36
                    leftPadding: 14
                    rightPadding: 14
                    text: qsTr("Cancel")
                    contentItem: Text {
                        text: cancelExpBtn.text
                        color: "#c8c8d2"
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: Math.min(4, theme.radius)
                        color: cancelExpBtn.hovered ? "#26262f" : "transparent"
                    }
                    onClicked: sheet.closeRequested()
                }

                Button {
                    id: doExportBtn
                    Material.roundedScale: Math.min(4, theme.radius)
                    implicitHeight: 36
                    leftPadding: 18
                    rightPadding: 18
                    text: qsTr("Export…")
                    contentItem: Text {
                        text: doExportBtn.text
                        color: theme.accentForeground
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: Math.min(4, theme.radius)
                        color: theme.accent
                        opacity: doExportBtn.down ? 0.85 : (doExportBtn.hovered ? 0.92 : 1.0)
                    }
                    onClicked: {
                        sheet.closeRequested()
                        backend.exportImageDialog()
                    }
                }
            }
        }
    }
}
