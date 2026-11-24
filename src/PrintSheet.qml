import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Peek 1.0

// macOS Preview-style print sheet: live page proof on the left, clean print
// controls on the right.
Item {
    id: sheet

    signal closeRequested()

    anchors.fill: parent

    function paperLabel(id) {
        switch (id) {
        case "a4": return qsTr("A4 (210 × 297 mm)")
        case "letter": return qsTr("US Letter (8.5 × 11 in)")
        case "4x6": return qsTr("4 × 6 in (Photo)")
        case "5x7": return qsTr("5 × 7 in (Photo)")
        case "a5": return qsTr("A5 (148 × 210 mm)")
        case "a3": return qsTr("A3 (297 × 420 mm)")
        case "legal": return qsTr("US Legal (8.5 × 14 in)")
        default: return id
        }
    }

    readonly property var paperIds: ["a4", "letter", "4x6", "5x7", "a5", "a3", "legal"]

    function formatMm(value) {
        // Read translator.language so the binding updates live on language switch.
        void translator.language
        const decimals = Math.abs(value - Math.round(value)) < 0.05 ? 0 : 1
        return Number(value).toLocaleString(Qt.locale(), 'f', decimals)
    }

    // Dimmed backdrop; clicking outside closes the sheet.
    Rectangle {
        anchors.fill: parent
        color: "#b0000000"

        MouseArea {
            anchors.fill: parent
            onClicked: sheet.closeRequested()
        }
    }

    // Main print card.
    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 900)
        height: Math.min(parent.height - 36, 636)
        radius: theme.radius
        color: "#18181d"
        border.color: "#2e2e36"
        border.width: 1

        // Absorb clicks inside the card so they don't dismiss the sheet.
        MouseArea { anchors.fill: parent }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // Header bar.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 50
                color: "#1e1e24"
                radius: theme.radius

                // Square off bottom corners of header.
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: Math.min(theme.radius, 12)
                    color: parent.color
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 16
                    spacing: 12

                    Text {
                        text: qsTr("Print")
                        color: "#f4f4f6"
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }

                    Text {
                        Layout.fillWidth: true
                        text: backend.fileName
                        color: "#9a9aa4"
                        font.pixelSize: 13
                        elide: Text.ElideMiddle
                    }

                    ToolButton {
                        Material.roundedScale: Math.min(4, theme.radius)
                        text: "✕"
                        font.pixelSize: 14
                        onClicked: sheet.closeRequested()
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: "#2a2a32"
                }
            }

            // Body: Preview on left, controls on right.
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // Live paper preview well.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 52
                    color: "#111115"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 10

                        PrintPreview {
                            id: livePreview
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            controller: backend
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Text {
                                Layout.fillWidth: true
                                text: qsTr("%1 × %2 mm · %3")
                                          .arg(sheet.formatMm(backend.paperWidthMm))
                                          .arg(sheet.formatMm(backend.paperHeightMm))
                                          .arg(backend.effectiveLandscape ? qsTr("Landscape") : qsTr("Portrait"))
                                color: "#9a9aa4"
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }

                            Text {
                                readonly property string copiesLabel: {
                                    void translator.language
                                    return qsTr("%n copies", "", backend.copies)
                                }
                                text: copiesLabel
                                color: "#c8c8d2"
                                font.pixelSize: 12
                                font.weight: Font.Medium
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    color: "#2a2a32"
                }

                // Print settings pane.
                Flickable {
                    Layout.fillHeight: true
                    Layout.preferredWidth: 380
                    Layout.maximumWidth: 410
                    contentWidth: width
                    contentHeight: controlsColumn.implicitHeight + 28
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    ColumnLayout {
                        id: controlsColumn
                        x: 20
                        y: 14
                        width: parent.width - 40
                        spacing: 10

                        // Printer selection.
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("Printer")
                                    color: "#b4b4c0"
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                }

                                Text {
                                    text: qsTr("Refresh")
                                    color: theme.accent
                                    font.pixelSize: 12
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: backend.refreshPrinters()
                                    }
                                }
                            }

                            ComboBox {
                                id: printerCombo
                                Layout.fillWidth: true
                                Layout.preferredHeight: 38
                                Material.roundedScale: Math.min(4, theme.radius)
                                model: backend.printers
                                enabled: backend.printers.length > 0
                                currentIndex: Math.max(0, backend.printers.indexOf(backend.printer))
                                onActivated: function(idx) {
                                    if (idx >= 0 && idx < backend.printers.length)
                                        backend.printer = backend.printers[idx]
                                }
                            }

                            Rectangle {
                                visible: backend.printersError.length > 0
                                Layout.fillWidth: true
                                implicitHeight: errLabel.implicitHeight + 14
                                radius: Math.min(4, theme.radius)
                                color: "#361c1f"
                                border.color: "#7a2e36"
                                border.width: 1

                                Text {
                                    id: errLabel
                                    anchors.fill: parent
                                    anchors.margins: 7
                                    text: backend.printersError
                                    color: "#ffb4ba"
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        // Copies stepper.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Copies")
                                color: "#b4b4c0"
                                font.pixelSize: 12
                                font.weight: Font.Medium
                            }

                            Rectangle {
                                width: 112
                                height: 34
                                radius: Math.min(4, theme.radius)
                                color: "#111115"
                                border.color: "#2c2c34"
                                border.width: 1

                                RowLayout {
                                    anchors.fill: parent
                                    spacing: 0

                                    Rectangle {
                                        Layout.preferredWidth: 34
                                        Layout.fillHeight: true
                                        radius: Math.min(4, theme.radius)
                                        color: minusMa.containsMouse && backend.copies > 1 ? "#23232b" : "transparent"

                                        Text {
                                            anchors.centerIn: parent
                                            text: "−"
                                            color: backend.copies > 1 ? "#f4f4f6" : "#55555f"
                                            font.pixelSize: 16
                                        }
                                        MouseArea {
                                            id: minusMa
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            enabled: backend.copies > 1
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: backend.copies = backend.copies - 1
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: {
                                            void translator.language
                                            return Number(backend.copies).toLocaleString(Qt.locale(), 'f', 0)
                                        }
                                        color: "#f4f4f6"
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        horizontalAlignment: Text.AlignHCenter
                                    }

                                    Rectangle {
                                        Layout.preferredWidth: 34
                                        Layout.fillHeight: true
                                        radius: Math.min(4, theme.radius)
                                        color: plusMa.containsMouse && backend.copies < 99 ? "#23232b" : "transparent"

                                        Text {
                                            anchors.centerIn: parent
                                            text: "+"
                                            color: backend.copies < 99 ? "#f4f4f6" : "#55555f"
                                            font.pixelSize: 16
                                        }
                                        MouseArea {
                                            id: plusMa
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            enabled: backend.copies < 99
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: backend.copies = backend.copies + 1
                                        }
                                    }
                                }
                            }
                        }

                        // Paper size.
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Text {
                                text: qsTr("Paper Size")
                                color: "#b4b4c0"
                                font.pixelSize: 12
                                font.weight: Font.Medium
                            }

                            ComboBox {
                                id: paperCombo
                                Layout.fillWidth: true
                                Layout.preferredHeight: 38
                                Material.roundedScale: Math.min(4, theme.radius)
                                model: {
                                    void translator.language
                                    return sheet.paperIds.map(function(id) { return sheet.paperLabel(id) })
                                }
                                currentIndex: Math.max(0, sheet.paperIds.indexOf(backend.paperSize))
                                onActivated: function(idx) {
                                    if (idx >= 0 && idx < sheet.paperIds.length)
                                        backend.paperSize = sheet.paperIds[idx]
                                }
                            }
                        }

                        // Orientation.
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Text {
                                text: qsTr("Orientation")
                                color: "#b4b4c0"
                                font.pixelSize: 12
                                font.weight: Font.Medium
                            }

                            SegmentBar {
                                Layout.fillWidth: true
                                currentValue: backend.orientation
                                options: [
                                    { value: "auto", label: qsTr("Auto") },
                                    { value: "portrait", label: qsTr("Portrait") },
                                    { value: "landscape", label: qsTr("Landscape") }
                                ]
                                onSelected: function(val) { backend.orientation = val }
                            }
                        }

                        // Scale mode.
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Text {
                                text: qsTr("Scale")
                                color: "#b4b4c0"
                                font.pixelSize: 12
                                font.weight: Font.Medium
                            }

                            SegmentBar {
                                Layout.fillWidth: true
                                currentValue: backend.scaleMode
                                options: [
                                    { value: "fit", label: qsTr("Fit Page") },
                                    { value: "fill", label: qsTr("Fill Paper") },
                                    { value: "scale", label: qsTr("Custom") }
                                ]
                                onSelected: function(val) { backend.scaleMode = val }
                            }

                            RowLayout {
                                visible: backend.scaleMode === "scale"
                                Layout.fillWidth: true
                                spacing: 10

                                Slider {
                                    Layout.fillWidth: true
                                    Material.accent: theme.accent
                                    from: 10
                                    to: 200
                                    stepSize: 5
                                    value: backend.scalePercent
                                    onMoved: backend.scalePercent = Math.round(value)
                                }

                                Text {
                                    Layout.preferredWidth: 44
                                    horizontalAlignment: Text.AlignRight
                                    text: {
                                        void translator.language
                                        return qsTr("%1%").arg(Number(backend.scalePercent).toLocaleString(Qt.locale(), 'f', 0))
                                    }
                                    color: "#f4f4f6"
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                }
                            }
                        }

                        // Margins.
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Text {
                                text: qsTr("Margins")
                                color: "#b4b4c0"
                                font.pixelSize: 12
                                font.weight: Font.Medium
                            }

                            SegmentBar {
                                Layout.fillWidth: true
                                currentValue: backend.marginMode
                                options: [
                                    { value: "standard", label: qsTr("Standard") },
                                    { value: "narrow", label: qsTr("Narrow") },
                                    { value: "borderless", label: qsTr("Borderless") }
                                ]
                                onSelected: function(val) { backend.marginMode = val }
                            }
                        }

                        // Color mode.
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Text {
                                text: qsTr("Color")
                                color: "#b4b4c0"
                                font.pixelSize: 12
                                font.weight: Font.Medium
                            }

                            SegmentBar {
                                Layout.fillWidth: true
                                currentValue: backend.grayscale ? "gray" : "color"
                                options: [
                                    { value: "color", label: qsTr("Color") },
                                    { value: "gray", label: qsTr("Black & White") }
                                ]
                                onSelected: function(val) { backend.grayscale = (val === "gray") }
                            }
                        }
                    }
                }
            }

            // Footer actions.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 60
                color: "#1e1e24"
                radius: theme.radius

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: Math.min(theme.radius, 12)
                    color: parent.color
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 1
                    color: "#2a2a32"
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    spacing: 10

                    Button {
                        id: savePdfBtn
                        Material.roundedScale: Math.min(4, theme.radius)
                        implicitHeight: 36
                        leftPadding: 14
                        rightPadding: 14
                        text: qsTr("Save as PDF")
                        contentItem: Text {
                            text: savePdfBtn.text
                            color: "#e6e6ee"
                            font.pixelSize: 13
                            font.weight: Font.Medium
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: Math.min(4, theme.radius)
                            color: savePdfBtn.down ? "#2c2c36" : (savePdfBtn.hovered ? "#25252e" : "#16161c")
                            border.color: "#363642"
                            border.width: 1
                        }
                        onClicked: backend.savePdfDialog()
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        id: cancelBtn
                        Material.roundedScale: Math.min(4, theme.radius)
                        implicitHeight: 36
                        leftPadding: 14
                        rightPadding: 14
                        text: qsTr("Cancel")
                        contentItem: Text {
                            text: cancelBtn.text
                            color: "#c8c8d2"
                            font.pixelSize: 13
                            font.weight: Font.Medium
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: Math.min(4, theme.radius)
                            color: cancelBtn.hovered ? "#26262f" : "transparent"
                        }
                        onClicked: sheet.closeRequested()
                    }

                    Button {
                        id: printNowBtn
                        Material.roundedScale: Math.min(4, theme.radius)
                        implicitHeight: 36
                        leftPadding: 18
                        rightPadding: 18
                        enabled: !backend.printBusy && backend.printer.length > 0
                        text: backend.printBusy ? qsTr("Printing…") : qsTr("Print")
                        contentItem: Text {
                            text: printNowBtn.text
                            color: printNowBtn.enabled ? theme.accentForeground : "#6e6e7a"
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: Math.min(4, theme.radius)
                            color: printNowBtn.enabled ? theme.accent : "#2a2a32"
                            opacity: printNowBtn.down ? 0.85 : (printNowBtn.hovered ? 0.92 : 1.0)
                        }
                        onClicked: {
                            backend.printCurrent()
                            sheet.closeRequested()
                        }
                    }
                }
            }
        }
    }
}
