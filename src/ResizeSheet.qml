import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Adjust Size modal sheet inspired by macOS Preview (Tools > Adjust Size).
Item {
    id: sheet

    signal closeRequested()

    anchors.fill: parent

    property string unitMode: "px" // px | pct
    property bool lockAspect: true
    property int targetW: 1
    property int targetH: 1
    property bool syncing: false

    function openWithCurrent() {
        syncing = true
        unitMode = "px"
        lockAspect = true
        targetW = Math.max(1, backend.imageWidth)
        targetH = Math.max(1, backend.imageHeight)
        refreshFields()
        syncing = false
    }

    function refreshFields() {
        if (unitMode === "pct") {
            const bw = Math.max(1, backend.nativeWidth)
            const bh = Math.max(1, backend.nativeHeight)
            widthInput.text = String(Math.round((targetW * 100.0) / bw))
            heightInput.text = String(Math.round((targetH * 100.0) / bh))
        } else {
            widthInput.text = String(targetW)
            heightInput.text = String(targetH)
        }
    }

    function applyPresetPercent(pct) {
        syncing = true
        const bw = Math.max(1, backend.nativeWidth)
        const bh = Math.max(1, backend.nativeHeight)
        targetW = Math.max(1, Math.round(bw * pct / 100.0))
        targetH = Math.max(1, Math.round(bh * pct / 100.0))
        refreshFields()
        syncing = false
    }

    function applyPresetWidth(w) {
        syncing = true
        const bw = Math.max(1, backend.nativeWidth)
        const bh = Math.max(1, backend.nativeHeight)
        targetW = Math.max(1, w)
        targetH = Math.max(1, Math.round(targetW * (bh / bw)))
        refreshFields()
        syncing = false
    }

    function onWidthEdited() {
        if (syncing)
            return
        const parsed = parseInt(widthInput.text, 10)
        if (isNaN(parsed) || parsed <= 0)
            return
        const bw = Math.max(1, backend.nativeWidth)
        const bh = Math.max(1, backend.nativeHeight)
        syncing = true
        if (unitMode === "pct") {
            const clamped = Math.max(1, Math.min(800, parsed))
            targetW = Math.max(1, Math.round(bw * clamped / 100.0))
            if (lockAspect) {
                targetH = Math.max(1, Math.round(bh * clamped / 100.0))
                heightInput.text = String(clamped)
            }
        } else {
            targetW = Math.max(1, Math.min(16384, parsed))
            if (lockAspect) {
                targetH = Math.max(1, Math.round(targetW * (bh / bw)))
                heightInput.text = String(targetH)
            }
        }
        syncing = false
    }

    function onHeightEdited() {
        if (syncing)
            return
        const parsed = parseInt(heightInput.text, 10)
        if (isNaN(parsed) || parsed <= 0)
            return
        const bw = Math.max(1, backend.nativeWidth)
        const bh = Math.max(1, backend.nativeHeight)
        syncing = true
        if (unitMode === "pct") {
            const clamped = Math.max(1, Math.min(800, parsed))
            targetH = Math.max(1, Math.round(bh * clamped / 100.0))
            if (lockAspect) {
                targetW = Math.max(1, Math.round(bw * clamped / 100.0))
                widthInput.text = String(clamped)
            }
        } else {
            targetH = Math.max(1, Math.min(16384, parsed))
            if (lockAspect) {
                targetW = Math.max(1, Math.round(targetH * (bw / bh)))
                widthInput.text = String(targetW)
            }
        }
        syncing = false
    }

    onVisibleChanged: {
        if (visible && backend.hasImage)
            openWithCurrent()
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
        width: Math.min(parent.width - 48, 440)
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
                    text: qsTr("Adjust Size")
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

            SegmentBar {
                Layout.fillWidth: true
                currentValue: sheet.unitMode
                options: [
                    { value: "px", label: qsTr("Pixels (px)") },
                    { value: "pct", label: qsTr("Percent (%)") }
                ]
                onSelected: function(val) {
                    sheet.syncing = true
                    sheet.unitMode = val
                    sheet.refreshFields()
                    sheet.syncing = false
                }
            }

            // Width & Height inputs.
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text {
                        text: qsTr("Width")
                        color: "#a4a4b0"
                        font.pixelSize: 12
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 36
                        radius: Math.min(4, theme.radius)
                        color: "#111115"
                        border.color: widthInput.activeFocus ? theme.accent : "#2e2e38"
                        border.width: 1

                        TextInput {
                            id: widthInput
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            verticalAlignment: TextInput.AlignVCenter
                            color: "#f4f4f6"
                            font.pixelSize: 14
                            inputMethodHints: Qt.ImhDigitsOnly
                            validator: IntValidator { bottom: 1; top: 16384 }
                            onTextEdited: sheet.onWidthEdited()
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text {
                        text: qsTr("Height")
                        color: "#a4a4b0"
                        font.pixelSize: 12
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 36
                        radius: Math.min(4, theme.radius)
                        color: "#111115"
                        border.color: heightInput.activeFocus ? theme.accent : "#2e2e38"
                        border.width: 1

                        TextInput {
                            id: heightInput
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            verticalAlignment: TextInput.AlignVCenter
                            color: "#f4f4f6"
                            font.pixelSize: 14
                            inputMethodHints: Qt.ImhDigitsOnly
                            validator: IntValidator { bottom: 1; top: 16384 }
                            onTextEdited: sheet.onHeightEdited()
                        }
                    }
                }
            }

            // Proportional lock toggle.
            Item {
                Layout.fillWidth: true
                implicitHeight: 28

                RowLayout {
                    anchors.fill: parent
                    spacing: 8

                    Rectangle {
                        width: 18
                        height: 18
                        radius: Math.min(4, theme.radius)
                        color: sheet.lockAspect ? theme.accent : "#111115"
                        border.color: sheet.lockAspect ? theme.accent : "#5a5a68"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: sheet.lockAspect ? "✓" : ""
                            color: theme.accentForeground
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Scale proportionally")
                        color: "#d4d4de"
                        font.pixelSize: 13
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: sheet.lockAspect = !sheet.lockAspect
                }
            }

            // Quick presets.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: qsTr("Presets")
                    color: "#a4a4b0"
                    font.pixelSize: 12
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Repeater {
                        model: [
                            { label: "50%", kind: "pct", val: 50 },
                            { label: "75%", kind: "pct", val: 75 },
                            { label: "1920", kind: "px", val: 1920 },
                            { label: "1280", kind: "px", val: 1280 },
                            { label: qsTr("Original"), kind: "pct", val: 100 }
                        ]

                        Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            height: 28
                            radius: Math.min(4, theme.radius)
                            color: preMa.containsMouse ? "#25252f" : "#121217"
                            border.color: "#2e2e38"
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: modelData.label
                                color: "#d6d6e0"
                                font.pixelSize: 11
                            }

                            MouseArea {
                                id: preMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (modelData.kind === "pct")
                                        sheet.applyPresetPercent(modelData.val)
                                    else
                                        sheet.applyPresetWidth(modelData.val)
                                }
                            }
                        }
                    }
                }
            }

            // Resulting dimensions summary.
            Text {
                Layout.fillWidth: true
                text: {
                    void translator.language
                    const cw = Number(backend.imageWidth).toLocaleString(Qt.locale(), 'f', 0)
                    const ch = Number(backend.imageHeight).toLocaleString(Qt.locale(), 'f', 0)
                    const tw = Number(sheet.targetW).toLocaleString(Qt.locale(), 'f', 0)
                    const th = Number(sheet.targetH).toLocaleString(Qt.locale(), 'f', 0)
                    return qsTr("Result: %1 × %2 → %3 × %4 px").arg(cw).arg(ch).arg(tw).arg(th)
                }
                color: "#9a9aa4"
                font.pixelSize: 12
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#2a2a32" }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Item { Layout.fillWidth: true }

                Button {
                    id: cancelResizeBtn
                    Material.roundedScale: Math.min(4, theme.radius)
                    implicitHeight: 36
                    leftPadding: 14
                    rightPadding: 14
                    text: qsTr("Cancel")
                    contentItem: Text {
                        text: cancelResizeBtn.text
                        color: "#c8c8d2"
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: Math.min(4, theme.radius)
                        color: cancelResizeBtn.hovered ? "#26262f" : "transparent"
                    }
                    onClicked: sheet.closeRequested()
                }

                Button {
                    id: applyResizeBtn
                    Material.roundedScale: Math.min(4, theme.radius)
                    implicitHeight: 36
                    leftPadding: 18
                    rightPadding: 18
                    text: qsTr("Apply")
                    contentItem: Text {
                        text: applyResizeBtn.text
                        color: theme.accentForeground
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: Math.min(4, theme.radius)
                        color: theme.accent
                        opacity: applyResizeBtn.down ? 0.85 : (applyResizeBtn.hovered ? 0.92 : 1.0)
                    }
                    onClicked: {
                        backend.applyResize(sheet.targetW, sheet.targetH)
                        sheet.closeRequested()
                    }
                }
            }
        }
    }
}
