import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Floating macOS Preview-style Inspector HUD showing General & EXIF camera info.
Rectangle {
    id: panel

    signal closeRequested()

    width: 320
    implicitHeight: Math.min(parent ? parent.height - 80 : 540, col.implicitHeight + 32)
    radius: theme.radius
    color: "#eb18181e"
    border.color: "#32323c"
    border.width: 1

    MouseArea {
        anchors.fill: parent
        onWheel: function(wheel) { wheel.accepted = true }
    }

    function formatBytes(bytes) {
        void translator.language
        if (!bytes || bytes <= 0)
            return "—"
        if (bytes >= 1024 * 1024)
            return qsTr("%1 MB").arg(Number(bytes / (1024.0 * 1024.0)).toLocaleString(Qt.locale(), 'f', 2))
        if (bytes >= 1024)
            return qsTr("%1 KB").arg(Number(bytes / 1024.0).toLocaleString(Qt.locale(), 'f', 0))
        return qsTr("%1 B").arg(Number(bytes).toLocaleString(Qt.locale(), 'f', 0))
    }

    function formatDate(ms) {
        void translator.language
        if (!ms)
            return "—"
        return new Date(ms).toLocaleString(Qt.locale(), Locale.ShortFormat)
    }

    function formatDim(w, h, mp) {
        void translator.language
        const ws = Number(w || 0).toLocaleString(Qt.locale(), 'f', 0)
        const hs = Number(h || 0).toLocaleString(Qt.locale(), 'f', 0)
        const mps = Number(mp || 0).toLocaleString(Qt.locale(), 'f', 1)
        return qsTr("%1 × %2 px (%3 MP)").arg(ws).arg(hs).arg(mps)
    }

    function formatPrintSize(wMm, hMm) {
        void translator.language
        const ws = Number(wMm || 0).toLocaleString(Qt.locale(), 'f', 1)
        const hs = Number(hMm || 0).toLocaleString(Qt.locale(), 'f', 1)
        return qsTr("%1 × %2 mm").arg(ws).arg(hs)
    }

    component InfoRow: RowLayout {
        property string label: ""
        property string value: ""
        Layout.fillWidth: true
        spacing: 10

        Text {
            text: label
            color: "#8e8e9c"
            font.pixelSize: 12
            Layout.preferredWidth: 122
            elide: Text.ElideRight
        }

        Text {
            Layout.fillWidth: true
            text: value
            color: "#ececf2"
            font.pixelSize: 12
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideMiddle
        }
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: 16
        contentWidth: width
        contentHeight: col.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: col
            width: parent.width
            spacing: 10

            RowLayout {
                Layout.fillWidth: true

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Inspector")
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
                    onClicked: panel.closeRequested()
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#2a2a32" }

            Text {
                text: qsTr("General")
                color: theme.accent
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }

            InfoRow { label: qsTr("File"); value: backend.metadata.fileName || "—" }
            InfoRow { label: qsTr("Format"); value: backend.metadata.format || "—" }
            InfoRow { label: qsTr("File Size"); value: panel.formatBytes(backend.metadata.fileBytes) }
            InfoRow {
                label: qsTr("Dimensions")
                value: panel.formatDim(backend.metadata.width, backend.metadata.height, backend.metadata.megapixels)
            }
            InfoRow {
                label: qsTr("Print (300 DPI)")
                value: panel.formatPrintSize(backend.metadata.printWidthMm, backend.metadata.printHeightMm)
            }
            InfoRow {
                label: qsTr("Color Depth")
                value: {
                    void translator.language
                    const bits = Number(backend.metadata.depth || 0).toLocaleString(Qt.locale(), 'f', 0)
                    return backend.metadata.hasAlpha ? qsTr("%1-bit RGBA").arg(bits) : qsTr("%1-bit RGB").arg(bits)
                }
            }
            InfoRow { label: qsTr("Modified"); value: panel.formatDate(backend.metadata.modifiedMs) }

            Rectangle { Layout.fillWidth: true; height: 1; color: "#2a2a32"; Layout.topMargin: 2 }

            Text {
                text: qsTr("Camera (EXIF)")
                color: theme.accent
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }

            Text {
                visible: !backend.metadata.hasExif
                text: qsTr("No EXIF camera metadata in this file.")
                color: "#7a7a88"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            ColumnLayout {
                visible: !!backend.metadata.hasExif
                Layout.fillWidth: true
                spacing: 8

                InfoRow { visible: !!backend.metadata.camera; label: qsTr("Camera"); value: backend.metadata.camera || "" }
                InfoRow { visible: !!backend.metadata.lens; label: qsTr("Lens"); value: backend.metadata.lens || "" }
                InfoRow { visible: !!backend.metadata.dateTaken; label: qsTr("Date Taken"); value: backend.metadata.dateTaken || "" }
                InfoRow { visible: !!backend.metadata.exposureTime; label: qsTr("Exposure"); value: backend.metadata.exposureTime || "" }
                InfoRow { visible: !!backend.metadata.aperture; label: qsTr("Aperture"); value: backend.metadata.aperture || "" }
                InfoRow { visible: !!backend.metadata.iso; label: qsTr("ISO"); value: backend.metadata.iso || "" }
                InfoRow { visible: !!backend.metadata.focalLength; label: qsTr("Focal Length"); value: backend.metadata.focalLength || "" }
            }
        }
    }
}
