import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Floating PDF text search bar toggled with Ctrl+F or '/'.
Rectangle {
    id: bar

    signal closeRequested()
    signal matchNavigated()

    readonly property bool inputActiveFocus: queryInput.activeFocus

    width: Math.min(parent ? parent.width - 28 : 420, 420)
    height: 40
    radius: Math.min(4, theme.radius)
    color: "#f6181820"
    border.color: queryInput.activeFocus ? theme.accent : "#32323c"
    border.width: 1

    function focusInput(initialText) {
        if (initialText !== undefined && initialText.length > 0) {
            queryInput.text = initialText
            searchDebounce.stop()
            backend.setSearchQuery(initialText)
        }
        queryInput.forceActiveFocus()
        queryInput.selectAll()
    }

    function commitNow() {
        searchDebounce.stop()
        if (queryInput.text !== backend.searchQuery)
            backend.setSearchQuery(queryInput.text)
    }

    Timer {
        id: searchDebounce
        interval: 150
        onTriggered: backend.setSearchQuery(queryInput.text)
    }

    Connections {
        target: backend
        function onSearchChanged() {
            if (backend.searchQuery !== queryInput.text && !searchDebounce.running)
                queryInput.text = backend.searchQuery
        }
    }

    MouseArea {
        anchors.fill: parent
        onWheel: function(wheel) { wheel.accepted = true }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 6
        spacing: 6

        Text {
            text: "⌕"
            color: "#8e8e9a"
            font.pixelSize: 15
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            TextInput {
                id: queryInput
                anchors.fill: parent
                verticalAlignment: TextInput.AlignVCenter
                color: "#f4f4f6"
                selectionColor: theme.accent
                selectedTextColor: theme.accentForeground
                font.pixelSize: 13
                clip: true
                selectByMouse: true
                onTextChanged: {
                    if (text === backend.searchQuery) {
                        searchDebounce.stop()
                        return
                    }
                    if (text.length === 0) {
                        searchDebounce.stop()
                        backend.clearSearch()
                    } else {
                        searchDebounce.restart()
                    }
                }
                Keys.onReturnPressed: function(event) {
                    bar.commitNow()
                    if (event.modifiers & Qt.ShiftModifier)
                        backend.previousSearchMatch()
                    else
                        backend.nextSearchMatch()
                    bar.matchNavigated()
                    event.accepted = true
                }
                Keys.onEnterPressed: function(event) {
                    bar.commitNow()
                    if (event.modifiers & Qt.ShiftModifier)
                        backend.previousSearchMatch()
                    else
                        backend.nextSearchMatch()
                    bar.matchNavigated()
                    event.accepted = true
                }
                Keys.onEscapePressed: function(event) {
                    bar.closeRequested()
                    event.accepted = true
                }
            }

            Text {
                anchors.fill: parent
                verticalAlignment: Text.AlignVCenter
                visible: queryInput.text.length === 0
                text: qsTr("Find in PDF…")
                color: "#7a7a88"
                font.pixelSize: 12
                elide: Text.ElideRight
            }
        }

        Text {
            visible: queryInput.text.length > 0
            text: {
                void translator.language
                if (backend.searchBusy)
                    return qsTr("Searching…")
                if (backend.searchMatchCount <= 0)
                    return qsTr("No matches")
                const cur = Number(backend.searchCurrentIndex + 1).toLocaleString(Qt.locale(), 'f', 0)
                const tot = Number(backend.searchMatchCount).toLocaleString(Qt.locale(), 'f', 0)
                return qsTr("%1 / %2").arg(cur).arg(tot)
            }
            color: backend.searchBusy ? theme.accent : (backend.searchMatchCount > 0 ? "#b8b8c4" : "#ff8c98")
            font.pixelSize: 11
            font.weight: Font.Medium
        }

        // Case-sensitivity toggle (Aa).
        Rectangle {
            implicitWidth: 26
            implicitHeight: 24
            radius: Math.min(4, theme.radius)
            color: backend.searchCaseSensitive ? theme.accent : (caseMa.containsMouse ? "#282834" : "transparent")
            border.color: backend.searchCaseSensitive ? theme.accent : "#343440"
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: "Aa"
                color: backend.searchCaseSensitive ? theme.accentForeground : "#c8c8d4"
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }

            MouseArea {
                id: caseMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    bar.commitNow()
                    backend.searchCaseSensitive = !backend.searchCaseSensitive
                }
            }

            ToolTip.visible: caseMa.containsMouse
            ToolTip.text: qsTr("Match Case")
        }

        // Previous match button.
        Rectangle {
            implicitWidth: 24
            implicitHeight: 24
            radius: Math.min(4, theme.radius)
            color: prevMatchMa.containsMouse && prevMatchMa.enabled ? "#282834" : "transparent"

            Text {
                anchors.centerIn: parent
                text: "‹"
                color: prevMatchMa.enabled ? "#f0f0f4" : "#55555f"
                font.pixelSize: 16
            }

            MouseArea {
                id: prevMatchMa
                anchors.fill: parent
                hoverEnabled: true
                enabled: backend.searchMatchCount > 0
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: {
                    backend.previousSearchMatch()
                    bar.matchNavigated()
                }
            }

            ToolTip.visible: prevMatchMa.containsMouse
            ToolTip.text: qsTr("Previous Match (Shift+Enter)")
        }

        // Next match button.
        Rectangle {
            implicitWidth: 24
            implicitHeight: 24
            radius: Math.min(4, theme.radius)
            color: nextMatchMa.containsMouse && nextMatchMa.enabled ? "#282834" : "transparent"

            Text {
                anchors.centerIn: parent
                text: "›"
                color: nextMatchMa.enabled ? "#f0f0f4" : "#55555f"
                font.pixelSize: 16
            }

            MouseArea {
                id: nextMatchMa
                anchors.fill: parent
                hoverEnabled: true
                enabled: backend.searchMatchCount > 0
                cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: {
                    backend.nextSearchMatch()
                    bar.matchNavigated()
                }
            }

            ToolTip.visible: nextMatchMa.containsMouse
            ToolTip.text: qsTr("Next Match (Enter)")
        }

        // Close button.
        Rectangle {
            implicitWidth: 24
            implicitHeight: 24
            radius: Math.min(4, theme.radius)
            color: closeFindMa.containsMouse ? "#282834" : "transparent"

            Text {
                anchors.centerIn: parent
                text: "✕"
                color: "#9a9aa4"
                font.pixelSize: 11
            }

            MouseArea {
                id: closeFindMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: bar.closeRequested()
            }

            ToolTip.visible: closeFindMa.containsMouse
            ToolTip.text: qsTr("Close (Esc)")
        }
    }
}
