import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Peek 1.0

// macOS Preview-style print sheet: live N-up/multi-page proof on the left,
// clean print and CUPS printer controls on the right.
Item {
    id: sheet

    signal closeRequested()

    anchors.fill: parent

    property bool moreOptionsExpanded: false
    property bool savingPreset: false

    function presetLabel(item) {
        void translator.language
        switch (item.id) {
        case "last-used": return qsTr("Last Used Settings")
        case "default": return qsTr("Default Settings")
        case "builtin:photo-4x6": return qsTr("Photo 4×6 Borderless")
        case "builtin:bw-draft": return qsTr("Black & White Draft")
        case "builtin:four-up": return qsTr("4-Up Sheet with Cut Guides")
        default: return item.name
        }
    }

    function presetIndex(list, activeId) {
        for (let i = 0; i < list.length; ++i) {
            if (list[i].id === activeId)
                return i
        }
        return 0
    }

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
        void translator.language
        const decimals = Math.abs(value - Math.round(value)) < 0.05 ? 0 : 1
        return Number(value).toLocaleString(Qt.locale(), 'f', decimals)
    }

    function optionIndex(list, currentVal) {
        for (let i = 0; i < list.length; ++i) {
            if (list[i].value === currentVal)
                return i
        }
        return 0
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
        width: Math.min(parent.width - 36, 940)
        height: Math.min(parent.height - 28, 650)
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
                Layout.preferredHeight: 48
                color: "#1e1e24"
                radius: theme.radius

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
                    anchors.rightMargin: 14
                    spacing: 12

                    Text {
                        text: qsTr("Print")
                        color: "#f4f4f6"
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }

                    Text {
                        Layout.fillWidth: true
                        text: {
                            void translator.language
                            if (backend.multiMode === "selected" && backend.imageCount > 1) {
                                const count = backend.selectedCount > 0 ? backend.selectedCount : backend.imageCount
                                return qsTr("%n images", "", count)
                            }
                            return backend.fileName
                        }
                        color: "#9a9aa4"
                        font.pixelSize: 13
                        elide: Text.ElideMiddle
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
                            spacing: 10

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

                            // Multi-page preview stepper.
                            RowLayout {
                                visible: backend.previewPageCount > 1
                                spacing: 4

                                Rectangle {
                                    width: 24
                                    height: 24
                                    radius: Math.min(4, theme.radius)
                                    color: prevPageMa.containsMouse && backend.previewPageIndex > 0 ? "#24242d" : "#18181f"
                                    border.color: "#2e2e38"
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: "‹"
                                        color: backend.previewPageIndex > 0 ? "#f4f4f6" : "#55555f"
                                        font.pixelSize: 14
                                    }
                                    MouseArea {
                                        id: prevPageMa
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        enabled: backend.previewPageIndex > 0
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: backend.previousPreviewPage()
                                    }
                                }

                                Text {
                                    text: {
                                        void translator.language
                                        const cur = Number(backend.previewPageIndex + 1).toLocaleString(Qt.locale(), 'f', 0)
                                        const tot = Number(backend.previewPageCount).toLocaleString(Qt.locale(), 'f', 0)
                                        return qsTr("Page %1 / %2").arg(cur).arg(tot)
                                    }
                                    color: "#d4d4de"
                                    font.pixelSize: 12
                                }

                                Rectangle {
                                    width: 24
                                    height: 24
                                    radius: Math.min(4, theme.radius)
                                    color: nextPageMa.containsMouse && backend.previewPageIndex + 1 < backend.previewPageCount ? "#24242d" : "#18181f"
                                    border.color: "#2e2e38"
                                    border.width: 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: "›"
                                        color: backend.previewPageIndex + 1 < backend.previewPageCount ? "#f4f4f6" : "#55555f"
                                        font.pixelSize: 14
                                    }
                                    MouseArea {
                                        id: nextPageMa
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        enabled: backend.previewPageIndex + 1 < backend.previewPageCount
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: backend.nextPreviewPage()
                                    }
                                }
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
                    Layout.preferredWidth: 392
                    Layout.maximumWidth: 420
                    contentWidth: width
                    contentHeight: controlsColumn.implicitHeight + 28
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    ColumnLayout {
                        id: controlsColumn
                        x: 20
                        y: 12
                        width: parent.width - 40
                        spacing: 8

                        // Printer selection.
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5

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
                                Layout.preferredHeight: 36
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

                        // macOS-style Presets selector (Last Used, Built-ins, and Custom Presets).
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                Text {
                                    text: qsTr("Presets")
                                    color: "#b4b4c0"
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                }

                                Text {
                                    visible: backend.activePresetModified
                                    text: qsTr("(Modified)")
                                    color: theme.accent
                                    font.pixelSize: 11
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    visible: backend.activePresetRemovable && !sheet.savingPreset
                                    text: qsTr("Delete")
                                    color: "#ff8c98"
                                    font.pixelSize: 12
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: backend.deleteActivePreset()
                                    }
                                }

                                Text {
                                    text: sheet.savingPreset ? qsTr("Cancel") : qsTr("Save…")
                                    color: theme.accent
                                    font.pixelSize: 12
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            sheet.savingPreset = !sheet.savingPreset
                                            if (sheet.savingPreset) {
                                                presetNameInput.text = backend.activePresetRemovable ? backend.activePresetName : ""
                                                presetNameInput.forceActiveFocus()
                                            }
                                        }
                                    }
                                }
                            }

                            ComboBox {
                                id: presetCombo
                                Layout.fillWidth: true
                                Layout.preferredHeight: 34
                                Material.roundedScale: Math.min(4, theme.radius)
                                model: {
                                    void translator.language
                                    return backend.presets.map(function(item) { return sheet.presetLabel(item) })
                                }
                                currentIndex: sheet.presetIndex(backend.presets, backend.activePresetId)
                                onModelChanged: currentIndex = sheet.presetIndex(backend.presets, backend.activePresetId)
                                onActivated: function(idx) {
                                    if (idx >= 0 && idx < backend.presets.length) {
                                        sheet.savingPreset = false
                                        backend.applyPreset(backend.presets[idx].id)
                                    }
                                }
                            }

                            // Compact inline Save Preset input drawer.
                            RowLayout {
                                visible: sheet.savingPreset
                                Layout.fillWidth: true
                                spacing: 6

                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 32
                                    radius: Math.min(4, theme.radius)
                                    color: "#111115"
                                    border.color: presetNameInput.activeFocus ? theme.accent : "#32323d"
                                    border.width: 1

                                    TextInput {
                                        id: presetNameInput
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 10
                                        verticalAlignment: TextInput.AlignVCenter
                                        color: "#f4f4f6"
                                        font.pixelSize: 12
                                        clip: true
                                        onAccepted: {
                                            if (backend.saveCurrentPreset(text))
                                                sheet.savingPreset = false
                                        }

                                        Text {
                                            anchors.fill: parent
                                            verticalAlignment: Text.AlignVCenter
                                            visible: !presetNameInput.text
                                            text: qsTr("Preset name (e.g. Glossy A4)")
                                            color: "#6e6e7c"
                                            font.pixelSize: 12
                                            elide: Text.ElideRight
                                        }
                                    }
                                }

                                Button {
                                    id: savePresetConfirmBtn
                                    Material.roundedScale: Math.min(4, theme.radius)
                                    implicitHeight: 32
                                    leftPadding: 12
                                    rightPadding: 12
                                    enabled: presetNameInput.text.trim().length > 0
                                    text: qsTr("Save")
                                    contentItem: Text {
                                        text: savePresetConfirmBtn.text
                                        color: savePresetConfirmBtn.enabled ? theme.accentForeground : "#6e6e7a"
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    background: Rectangle {
                                        radius: Math.min(4, theme.radius)
                                        color: savePresetConfirmBtn.enabled ? theme.accent : "#26262f"
                                    }
                                    onClicked: {
                                        if (backend.saveCurrentPreset(presetNameInput.text))
                                            sheet.savingPreset = false
                                    }
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
                                height: 32
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

                        // Print Selection toggle (only shown when a region is selected).
                        Rectangle {
                            visible: backend.hasSelection
                            Layout.fillWidth: true
                            implicitHeight: 36
                            radius: Math.min(4, theme.radius)
                            color: "#15151c"
                            border.color: backend.printSelectionOnly ? theme.accent : "#2c2c34"
                            border.width: 1

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 8

                                Text {
                                    Layout.fillWidth: true
                                    text: {
                                        void translator.language
                                        const w = Number(backend.selectionPixelWidth).toLocaleString(Qt.locale(), 'f', 0)
                                        const h = Number(backend.selectionPixelHeight).toLocaleString(Qt.locale(), 'f', 0)
                                        return qsTr("Print Selection (%1 × %2)").arg(w).arg(h)
                                    }
                                    color: "#f0f0f6"
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }

                                Rectangle {
                                    width: 36
                                    height: 20
                                    radius: Math.min(10, Math.max(theme.radius, 2))
                                    color: backend.printSelectionOnly ? theme.accent : "#2d2d36"

                                    Rectangle {
                                        width: 14
                                        height: 14
                                        radius: Math.min(7, Math.max(theme.radius, 1))
                                        y: 3
                                        x: backend.printSelectionOnly ? parent.width - width - 3 : 3
                                        color: backend.printSelectionOnly ? theme.accentForeground : "#a0a0ac"
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: backend.printSelectionOnly = !backend.printSelectionOnly
                            }
                        }

                        // Images per sheet (N-up).
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5

                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("Images per Sheet")
                                    color: "#b4b4c0"
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                }

                                // Optional dashed cut guides toggle when printing multi-up.
                                Text {
                                    visible: backend.perPage > 1
                                    text: backend.cutGuides ? qsTr("Cut Guides: On") : qsTr("Cut Guides: Off")
                                    color: backend.cutGuides ? theme.accent : "#8e8e9a"
                                    font.pixelSize: 11
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: backend.cutGuides = !backend.cutGuides
                                    }
                                }
                            }

                            SegmentBar {
                                Layout.fillWidth: true
                                currentValue: String(backend.perPage)
                                options: [
                                    { value: "1", label: "1" },
                                    { value: "2", label: "2" },
                                    { value: "4", label: "4" },
                                    { value: "6", label: "6" },
                                    { value: "9", label: "9" }
                                ]
                                onSelected: function(val) { backend.perPage = parseInt(val, 10) }
                            }

                            // Choose between repeating the current image or packing selected/folder images.
                            SegmentBar {
                                visible: backend.imageCount > 1 && (backend.perPage > 1 || backend.selectedCount > 1 || backend.multiMode === "selected")
                                Layout.fillWidth: true
                                currentValue: backend.multiMode
                                options: {
                                    void translator.language
                                    const batchCount = backend.selectedCount > 0 ? backend.selectedCount : backend.imageCount
                                    const countText = Number(batchCount).toLocaleString(Qt.locale(), 'f', 0)
                                    const batchLabel = backend.selectedCount > 0
                                                       ? qsTr("Selected (%1)").arg(countText)
                                                       : qsTr("All Images (%1)").arg(countText)
                                    return [
                                        { value: "repeat", label: qsTr("Repeat Current") },
                                        { value: "selected", label: batchLabel }
                                    ]
                                }
                                onSelected: function(val) { backend.multiMode = val }
                            }
                        }

                        // Paper size.
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5

                            Text {
                                text: qsTr("Paper Size")
                                color: "#b4b4c0"
                                font.pixelSize: 12
                                font.weight: Font.Medium
                            }

                            ComboBox {
                                id: paperCombo
                                Layout.fillWidth: true
                                Layout.preferredHeight: 36
                                Material.roundedScale: Math.min(4, theme.radius)
                                model: {
                                    void translator.language
                                    return sheet.paperIds.map(function(id) { return sheet.paperLabel(id) })
                                }
                                currentIndex: Math.max(0, sheet.paperIds.indexOf(backend.paperSize))
                                onModelChanged: currentIndex = Math.max(0, sheet.paperIds.indexOf(backend.paperSize))
                                onActivated: function(idx) {
                                    if (idx >= 0 && idx < sheet.paperIds.length)
                                        backend.paperSize = sheet.paperIds[idx]
                                }
                            }
                        }

                        // Orientation.
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5

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
                            spacing: 5

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
                            spacing: 5

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
                            spacing: 5

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

                        // Collapsible CUPS printer-specific options (Media Type, Quality, Duplex).
                        ColumnLayout {
                            visible: backend.hasPrinterOptions
                            Layout.fillWidth: true
                            Layout.topMargin: 2
                            spacing: 8

                            Rectangle {
                                Layout.fillWidth: true
                                height: 1
                                color: "#282830"
                            }

                            Item {
                                Layout.fillWidth: true
                                implicitHeight: 24

                                RowLayout {
                                    anchors.fill: parent
                                    spacing: 6

                                    Text {
                                        text: sheet.moreOptionsExpanded ? "▾" : "▸"
                                        color: theme.accent
                                        font.pixelSize: 12
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: qsTr("Printer Options")
                                        color: "#d4d4de"
                                        font.pixelSize: 12
                                        font.weight: Font.Medium
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: sheet.moreOptionsExpanded = !sheet.moreOptionsExpanded
                                }
                            }

                            ColumnLayout {
                                visible: sheet.moreOptionsExpanded
                                Layout.fillWidth: true
                                spacing: 8

                                ColumnLayout {
                                    visible: backend.mediaTypeOptions.length > 0
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Text {
                                        text: qsTr("Media Type")
                                        color: "#9a9aa4"
                                        font.pixelSize: 11
                                    }

                                    ComboBox {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 34
                                        Material.roundedScale: Math.min(4, theme.radius)
                                        model: backend.mediaTypeOptions.map(function(o) { return o.label })
                                        currentIndex: sheet.optionIndex(backend.mediaTypeOptions, backend.mediaType)
                                        onActivated: function(idx) {
                                            if (idx >= 0 && idx < backend.mediaTypeOptions.length)
                                                backend.mediaType = backend.mediaTypeOptions[idx].value
                                        }
                                    }
                                }

                                ColumnLayout {
                                    visible: backend.qualityOptions.length > 0
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Text {
                                        text: qsTr("Print Quality")
                                        color: "#9a9aa4"
                                        font.pixelSize: 11
                                    }

                                    ComboBox {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 34
                                        Material.roundedScale: Math.min(4, theme.radius)
                                        model: backend.qualityOptions.map(function(o) { return o.label })
                                        currentIndex: sheet.optionIndex(backend.qualityOptions, backend.printQuality)
                                        onActivated: function(idx) {
                                            if (idx >= 0 && idx < backend.qualityOptions.length)
                                                backend.printQuality = backend.qualityOptions[idx].value
                                        }
                                    }
                                }

                                ColumnLayout {
                                    visible: backend.duplexOptions.length > 0
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Text {
                                        text: qsTr("Two-Sided")
                                        color: "#9a9aa4"
                                        font.pixelSize: 11
                                    }

                                    ComboBox {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 34
                                        Material.roundedScale: Math.min(4, theme.radius)
                                        model: backend.duplexOptions.map(function(o) { return o.label })
                                        currentIndex: sheet.optionIndex(backend.duplexOptions, backend.duplex)
                                        onActivated: function(idx) {
                                            if (idx >= 0 && idx < backend.duplexOptions.length)
                                                backend.duplex = backend.duplexOptions[idx].value
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Footer actions.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 58
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
