import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Collapsible thumbnail strip for stepping through a folder or PDF,
// multi-selecting for N-up printing, and organizing/merging PDF pages.
Rectangle {
    id: sidebar

    signal imagePicked()

    color: "#141419"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header showing count and batch-select shortcuts.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: "#191920"

            MouseArea {
                anchors.fill: parent
                onWheel: function(wheel) { wheel.accepted = true }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 6

                Text {
                    Layout.fillWidth: true
                    text: {
                        void translator.language
                        if (backend.selectedCount > 0)
                            return qsTr("%n selected", "", backend.selectedCount)
                        if (backend.isPdfMode)
                            return qsTr("%n pages", "", backend.imageCount)
                        return qsTr("%n images", "", backend.imageCount)
                    }
                    color: backend.selectedCount > 0 ? theme.accent : "#a4a4b0"
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }

                Text {
                    visible: backend.selectedCount < backend.imageCount && backend.imageCount > 1
                    text: qsTr("All")
                    color: "#c8c8d4"
                    font.pixelSize: 11
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: backend.selectAllFiles()
                    }
                }

                Text {
                    visible: backend.selectedCount > 0
                    text: qsTr("Clear")
                    color: "#c8c8d4"
                    font.pixelSize: 11
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: backend.clearFileSelection()
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: "#26262e"
            }
        }

        // Scrollable thumbnail list; each delegate is full-width and centered.
        ListView {
            id: thumbList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            topMargin: 10
            bottomMargin: 10
            model: backend.fileItems
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Item {
                required property var modelData
                readonly property bool isCurrent: modelData.index === backend.imageIndex
                readonly property bool isSelected: modelData.selected

                width: thumbList.width
                height: 118

                Rectangle {
                    id: card
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width - 20
                    height: parent.height
                    radius: Math.min(4, theme.radius)
                    color: isCurrent ? "#22222c" : (cardArea.containsMouse ? "#1c1c24" : "#18181f")
                    border.color: isCurrent ? theme.accent : (isSelected ? theme.accent : "#2a2a34")
                    border.width: isCurrent ? 2 : 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 4

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            Image {
                                anchors.fill: parent
                                source: "image://peek/thumb/" + modelData.index + "?v=" + backend.fileListVersion + "_" + (isCurrent ? backend.imageVersion : 0)
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                                cache: false
                                smooth: true
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: {
                                void translator.language
                                if (modelData.isPdfPage) {
                                    const num = Number(modelData.pageNumber).toLocaleString(Qt.locale(), 'f', 0)
                                    return qsTr("Page %1").arg(num)
                                }
                                return modelData.fileName
                            }
                            color: isCurrent ? "#f4f4f6" : "#a8a8b4"
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    MouseArea {
                        id: cardArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: function(mouse) {
                            if (mouse.modifiers & Qt.ControlModifier) {
                                backend.toggleFileSelected(modelData.index)
                            } else if (mouse.modifiers & Qt.ShiftModifier) {
                                backend.selectRangeTo(modelData.index)
                            } else {
                                backend.selectIndex(modelData.index)
                                sidebar.imagePicked()
                            }
                        }
                    }

                    // Selection checkbox badge in top-right corner.
                    Rectangle {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 5
                        width: 20
                        height: 20
                        radius: Math.min(4, theme.radius)
                        visible: isSelected || cardArea.containsMouse || checkMa.containsMouse
                        color: isSelected ? theme.accent : "#cc141419"
                        border.color: isSelected ? theme.accent : "#787886"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: isSelected ? "✓" : ""
                            color: theme.accentForeground
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }

                        MouseArea {
                            id: checkMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: backend.toggleFileSelected(modelData.index)
                        }
                    }
                }
            }
        }

        // Compact page organization footer: Merge, Move Up, Move Down, Delete.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            color: "#191920"

            MouseArea {
                anchors.fill: parent
                onWheel: function(wheel) { wheel.accepted = true }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: "#26262e"
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 4

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 26
                    radius: Math.min(4, theme.radius)
                    color: mergeMa.containsMouse ? "#252530" : "#1d1d26"
                    border.color: "#2f2f3b"
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("+ Merge")
                        color: theme.accent
                        font.pixelSize: 11
                        font.weight: Font.Medium
                    }

                    MouseArea {
                        id: mergeMa
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: backend.appendFilesDialog()
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 26
                    Layout.preferredHeight: 26
                    radius: Math.min(4, theme.radius)
                    color: upMa.containsMouse && upMa.enabled ? "#252530" : "transparent"
                    border.color: "#2f2f3b"
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "↑"
                        color: upMa.enabled ? "#e2e2ec" : "#55555f"
                        font.pixelSize: 12
                    }

                    MouseArea {
                        id: upMa
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: backend.imageIndex > 0
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: backend.moveCurrentPageUp()
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 26
                    Layout.preferredHeight: 26
                    radius: Math.min(4, theme.radius)
                    color: downMa.containsMouse && downMa.enabled ? "#252530" : "transparent"
                    border.color: "#2f2f3b"
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "↓"
                        color: downMa.enabled ? "#e2e2ec" : "#55555f"
                        font.pixelSize: 12
                    }

                    MouseArea {
                        id: downMa
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: backend.imageIndex >= 0 && backend.imageIndex + 1 < backend.imageCount
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: backend.moveCurrentPageDown()
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 26
                    Layout.preferredHeight: 26
                    radius: Math.min(4, theme.radius)
                    color: delMa.containsMouse && delMa.enabled ? "#3a1e24" : "transparent"
                    border.color: delMa.containsMouse && delMa.enabled ? "#7a303a" : "#2f2f3b"
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "✕"
                        color: delMa.enabled ? "#ff8c98" : "#55555f"
                        font.pixelSize: 11
                    }

                    MouseArea {
                        id: delMa
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: backend.imageCount > 1
                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: backend.deleteCurrentPage()
                    }
                }
            }
        }
    }

    // Right border divider.
    Rectangle {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: 1
        color: "#26262e"
    }
}
