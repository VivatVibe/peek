import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1040
    height: 700
    minimumWidth: 720
    minimumHeight: 500
    visible: true
    title: backend.hasImage ? backend.fileName + " — Peek" : "Peek"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    property bool printVisible: false
    property bool sidebarVisible: false
    property bool selectMode: false
    property bool showCheckerboard: false

    // Viewer zoom/pan state: zoomMode "fit" tracks the canvas automatically;
    // "manual" uses zoomScale and panX/panY.
    property string zoomMode: "fit"
    property real zoomScale: 1.0
    property real panX: 0
    property real panY: 0

    // Live rubber-band state while dragging a print selection marquee.
    property bool draggingSelection: false
    property real dragStartNormX: 0
    property real dragStartNormY: 0
    property real dragCurNormX: 0
    property real dragCurNormY: 0

    readonly property real fitScale: {
        if (!backend.hasImage || backend.imageWidth <= 0 || backend.imageHeight <= 0 || canvasArea.width <= 32 || canvasArea.height <= 32)
            return 1.0
        const availW = canvasArea.width - 32
        const availH = canvasArea.height - 32
        return Math.min(1.0, Math.min(availW / backend.imageWidth, availH / backend.imageHeight))
    }

    readonly property real effectiveScale: zoomMode === "fit" ? fitScale : zoomScale

    readonly property bool selectionActive: draggingSelection || backend.hasSelection
    readonly property real activeSelX: draggingSelection ? Math.min(dragStartNormX, dragCurNormX) : backend.selectionNorm.x
    readonly property real activeSelY: draggingSelection ? Math.min(dragStartNormY, dragCurNormY) : backend.selectionNorm.y
    readonly property real activeSelW: draggingSelection ? Math.abs(dragCurNormX - dragStartNormX) : backend.selectionNorm.width
    readonly property real activeSelH: draggingSelection ? Math.abs(dragCurNormY - dragStartNormY) : backend.selectionNorm.height

    function resetView() {
        zoomMode = "fit"
        zoomScale = fitScale
        panX = 0
        panY = 0
    }

    function setActualSize() {
        if (!backend.hasImage)
            return
        zoomMode = "manual"
        zoomScale = 1.0
        panX = 0
        panY = 0
    }

    function zoomBy(factor, anchorX, anchorY) {
        if (!backend.hasImage)
            return
        const oldScale = effectiveScale
        const nextScale = Math.max(0.05, Math.min(16.0, oldScale * factor))
        const ratio = nextScale / oldScale

        const cx = canvasArea.width / 2 + panX
        const cy = canvasArea.height / 2 + panY
        const ax = anchorX !== undefined ? anchorX : (pointerTracker.hovered ? pointerTracker.point.position.x : canvasArea.width / 2)
        const ay = anchorY !== undefined ? anchorY : (pointerTracker.hovered ? pointerTracker.point.position.y : canvasArea.height / 2)

        zoomMode = "manual"
        zoomScale = nextScale
        panX = (cx - ax) * ratio + ax - canvasArea.width / 2
        panY = (cy - ay) * ratio + ay - canvasArea.height / 2
    }

    function openPrintSheet() {
        if (!backend.hasImage) {
            backend.openFileDialog()
            return
        }
        helpVisible = false
        backend.preparePrintSheet()
        printVisible = true
    }

    Connections {
        target: backend
        function onImageChanged() {
            if (win.zoomMode === "fit") {
                win.panX = 0
                win.panY = 0
            }
        }
    }

    // Auto-hide success status banner after 4 seconds; errors stay until dismissed.
    Timer {
        id: statusTimer
        interval: 4000
        onTriggered: {
            if (backend.errorMessage.length === 0)
                backend.clearMessages()
        }
    }

    Connections {
        target: backend
        function onMessageChanged() {
            if (backend.statusMessage.length > 0)
                statusTimer.restart()
            else
                statusTimer.stop()
        }
    }

    // Keyboard shortcuts.
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: { win.printVisible = false; win.helpVisible = !win.helpVisible } }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (win.helpVisible) win.helpVisible = false
            else if (win.printVisible) win.printVisible = false
            else if (backend.hasSelection) { backend.clearSelection(); win.selectMode = false }
            else if (win.selectMode) win.selectMode = false
            else if (backend.errorMessage.length > 0) backend.clearMessages()
        }
    }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !win.printVisible && !win.helpVisible; onActivated: win.openPrintSheet() }
    Shortcut { sequences: [StandardKey.Print, "P"]; context: Qt.ApplicationShortcut; enabled: !win.helpVisible; onActivated: { if (win.printVisible) win.printVisible = false; else win.openPrintSheet() } }
    Shortcut { sequences: ["Return", "Enter"]; context: Qt.ApplicationShortcut; enabled: win.printVisible && !backend.printBusy && backend.printer.length > 0; onActivated: { backend.printCurrent(); win.printVisible = false } }
    Shortcut { sequences: [StandardKey.Open, "O"]; context: Qt.ApplicationShortcut; enabled: !win.printVisible && !win.helpVisible; onActivated: backend.openFileDialog() }
    Shortcut { sequences: [StandardKey.Save]; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.helpVisible; onActivated: backend.savePdfDialog() }
    Shortcut { sequence: "Tab"; context: Qt.ApplicationShortcut; enabled: backend.imageCount > 1 && !win.printVisible && !win.helpVisible; onActivated: win.sidebarVisible = !win.sidebarVisible }
    Shortcut { sequence: "S"; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.printVisible && !win.helpVisible; onActivated: win.selectMode = !win.selectMode }
    Shortcut { sequences: [StandardKey.SelectAll]; context: Qt.ApplicationShortcut; enabled: backend.imageCount > 1 && !win.printVisible && !win.helpVisible; onActivated: { win.sidebarVisible = true; backend.selectAllFiles() } }
    Shortcut { sequences: ["PgDown"]; context: Qt.ApplicationShortcut; enabled: win.printVisible && backend.previewPageCount > 1; onActivated: backend.nextPreviewPage() }
    Shortcut { sequences: ["PgUp"]; context: Qt.ApplicationShortcut; enabled: win.printVisible && backend.previewPageCount > 1; onActivated: backend.previousPreviewPage() }
    Shortcut { sequences: ["Right", "L"]; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.printVisible && !win.helpVisible; onActivated: { win.resetView(); backend.nextImage() } }
    Shortcut { sequences: ["Left", "H"]; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.printVisible && !win.helpVisible; onActivated: { win.resetView(); backend.previousImage() } }
    Shortcut { sequences: ["+", "="]; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.printVisible && !win.helpVisible; onActivated: win.zoomBy(1.25) }
    Shortcut { sequence: "-"; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.printVisible && !win.helpVisible; onActivated: win.zoomBy(0.8) }
    Shortcut { sequences: ["0", "F"]; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.printVisible && !win.helpVisible; onActivated: win.resetView() }
    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.printVisible && !win.helpVisible; onActivated: win.setActualSize() }
    Shortcut { sequences: ["[", "Ctrl+L"]; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.helpVisible; onActivated: backend.rotateLeft() }
    Shortcut { sequences: ["]", "Ctrl+R"]; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.helpVisible; onActivated: backend.rotateRight() }
    Shortcut { sequence: "Shift+H"; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.helpVisible; onActivated: backend.toggleFlipHorizontal() }
    Shortcut { sequence: "Shift+V"; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.helpVisible; onActivated: backend.toggleFlipVertical() }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; enabled: backend.canUndo && !win.helpVisible; onActivated: backend.undo() }
    Shortcut { sequences: [StandardKey.Redo]; context: Qt.ApplicationShortcut; enabled: backend.canRedo && !win.helpVisible; onActivated: backend.redo() }
    Shortcut { sequence: "T"; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.printVisible && !win.helpVisible; onActivated: win.showCheckerboard = !win.showCheckerboard }

    // Right-to-left languages mirror the window; Translator sets layoutDirection.
    Item {
        id: mirroredRoot
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // Middle row: optional Thumbnail Sidebar + Main image stage.
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                ThumbnailSidebar {
                    visible: win.sidebarVisible && backend.imageCount > 1
                    Layout.preferredWidth: 180
                    Layout.fillHeight: true
                    onImagePicked: win.resetView()
                }

                Item {
                    id: canvasArea
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    HoverHandler {
                        id: pointerTracker
                    }

                    // Empty state when no image is loaded.
                    Item {
                        anchors.fill: parent
                        visible: !backend.hasImage

                        Rectangle {
                            anchors.centerIn: parent
                            width: Math.min(parent.width - 64, 440)
                            height: 270
                            radius: theme.radius
                            color: dropArea.containsDrag ? "#1c1c24" : "#15151a"
                            border.color: dropArea.containsDrag ? theme.accent : "#2a2a33"
                            border.width: dropArea.containsDrag ? 2 : 1

                            ColumnLayout {
                                anchors.centerIn: parent
                                width: parent.width - 48
                                spacing: 14

                                Rectangle {
                                    Layout.alignment: Qt.AlignHCenter
                                    width: 56
                                    height: 56
                                    radius: Math.max(theme.radius, 6)
                                    color: "#202028"
                                    border.color: "#32323d"
                                    border.width: 1

                                    Canvas {
                                        anchors.fill: parent
                                        onPaint: {
                                            const ctx = getContext("2d")
                                            ctx.clearRect(0, 0, width, height)
                                            ctx.strokeStyle = theme.accent
                                            ctx.lineWidth = 2.2
                                            ctx.lineCap = "round"
                                            ctx.lineJoin = "round"
                                            ctx.strokeRect(14, 16, 28, 24)
                                            ctx.beginPath()
                                            ctx.moveTo(16, 36)
                                            ctx.lineTo(24, 27)
                                            ctx.lineTo(30, 33)
                                            ctx.lineTo(35, 29)
                                            ctx.lineTo(40, 36)
                                            ctx.stroke()
                                            ctx.fillStyle = theme.accent
                                            ctx.beginPath()
                                            ctx.arc(34, 22, 2.3, 0, Math.PI * 2)
                                            ctx.fill()
                                        }
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: qsTr("Drop an image to preview & print")
                                    color: "#f4f4f6"
                                    font.pixelSize: 16
                                    font.weight: Font.DemiBold
                                    wrapMode: Text.WordWrap
                                }

                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: qsTr("Press O to choose a file, or ? for keyboard shortcuts")
                                    color: "#8e8e9a"
                                    font.pixelSize: 13
                                    wrapMode: Text.WordWrap
                                }

                                Button {
                                    id: emptyOpenBtn
                                    Layout.alignment: Qt.AlignHCenter
                                    Layout.topMargin: 4
                                    implicitHeight: 38
                                    leftPadding: 18
                                    rightPadding: 18
                                    Material.roundedScale: Math.min(4, theme.radius)
                                    text: qsTr("Open Image")
                                    contentItem: Text {
                                        text: emptyOpenBtn.text
                                        color: theme.accentForeground
                                        font.pixelSize: 13
                                        font.weight: Font.DemiBold
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    background: Rectangle {
                                        radius: Math.min(4, theme.radius)
                                        color: theme.accent
                                        opacity: emptyOpenBtn.down ? 0.85 : (emptyOpenBtn.hovered ? 0.92 : 1.0)
                                    }
                                    onClicked: backend.openFileDialog()
                                }
                            }
                        }
                    }

                    // Image viewport.
                    Item {
                        anchors.fill: parent
                        visible: backend.hasImage

                        // Optional checkerboard behind transparent images.
                        Canvas {
                            visible: win.showCheckerboard
                            x: imageFrame.x
                            y: imageFrame.y
                            width: imageFrame.width
                            height: imageFrame.height
                            onWidthChanged: requestPaint()
                            onHeightChanged: requestPaint()
                            onVisibleChanged: if (visible) requestPaint()
                            onPaint: {
                                const ctx = getContext("2d")
                                const size = 12
                                for (let py = 0; py < height; py += size) {
                                    for (let px = 0; px < width; px += size) {
                                        const even = (((px / size) | 0) + ((py / size) | 0)) % 2 === 0
                                        ctx.fillStyle = even ? "#2c2c34" : "#1c1c22"
                                        ctx.fillRect(px, py, size, size)
                                    }
                                }
                            }
                        }

                        Image {
                            id: imageFrame
                            width: Math.max(1, backend.imageWidth * win.effectiveScale)
                            height: Math.max(1, backend.imageHeight * win.effectiveScale)
                            x: (canvasArea.width - width) / 2 + win.panX
                            y: (canvasArea.height - height) / 2 + win.panY
                            source: backend.hasImage ? ("image://peek/current?v=" + backend.imageVersion) : ""
                            cache: false
                            asynchronous: false
                            smooth: true
                            mipmap: true
                            fillMode: Image.Stretch
                        }

                        // Pan, double-click zoom toggle, wheel zoom toward pointer,
                        // and rectangular region selection (Select mode or Shift+Drag).
                        MouseArea {
                            id: panArea
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            cursorShape: (win.selectMode || win.draggingSelection)
                                         ? Qt.CrossCursor
                                         : (pressed ? Qt.ClosedHandCursor : (win.zoomMode === "manual" ? Qt.OpenHandCursor : Qt.ArrowCursor))

                            property real lastX: 0
                            property real lastY: 0
                            property bool isSelectingDrag: false

                            function toImageNormX(px) {
                                return Math.max(0.0, Math.min(1.0, (px - imageFrame.x) / Math.max(1.0, imageFrame.width)))
                            }
                            function toImageNormY(py) {
                                return Math.max(0.0, Math.min(1.0, (py - imageFrame.y) / Math.max(1.0, imageFrame.height)))
                            }

                            onPressed: function(mouse) {
                                lastX = mouse.x
                                lastY = mouse.y
                                if (win.selectMode || (mouse.modifiers & Qt.ShiftModifier)) {
                                    isSelectingDrag = true
                                    win.draggingSelection = true
                                    win.dragStartNormX = toImageNormX(mouse.x)
                                    win.dragStartNormY = toImageNormY(mouse.y)
                                    win.dragCurNormX = win.dragStartNormX
                                    win.dragCurNormY = win.dragStartNormY
                                } else {
                                    isSelectingDrag = false
                                }
                            }

                            onPositionChanged: function(mouse) {
                                if (!pressed || !backend.hasImage)
                                    return
                                if (isSelectingDrag) {
                                    win.dragCurNormX = toImageNormX(mouse.x)
                                    win.dragCurNormY = toImageNormY(mouse.y)
                                } else {
                                    win.zoomMode = "manual"
                                    win.zoomScale = win.effectiveScale
                                    win.panX += (mouse.x - lastX)
                                    win.panY += (mouse.y - lastY)
                                    lastX = mouse.x
                                    lastY = mouse.y
                                }
                            }

                            onReleased: {
                                if (isSelectingDrag) {
                                    isSelectingDrag = false
                                    win.draggingSelection = false
                                    const x = Math.min(win.dragStartNormX, win.dragCurNormX)
                                    const y = Math.min(win.dragStartNormY, win.dragCurNormY)
                                    const w = Math.abs(win.dragCurNormX - win.dragStartNormX)
                                    const h = Math.abs(win.dragCurNormY - win.dragStartNormY)
                                    backend.setSelectionRect(x, y, w, h)
                                }
                            }

                            onDoubleClicked: {
                                if (win.selectMode)
                                    return
                                if (win.zoomMode === "fit")
                                    win.setActualSize()
                                else
                                    win.resetView()
                            }

                            onWheel: function(wheel) {
                                const factor = wheel.angleDelta.y > 0 ? 1.15 : (1.0 / 1.15)
                                win.zoomBy(factor, wheel.x, wheel.y)
                            }
                        }

                        // Dimmed mask & selection marquee over imageFrame.
                        Item {
                            visible: win.selectionActive
                            x: imageFrame.x
                            y: imageFrame.y
                            width: imageFrame.width
                            height: imageFrame.height

                            readonly property real sx: win.activeSelX * width
                            readonly property real sy: win.activeSelY * height
                            readonly property real sw: win.activeSelW * width
                            readonly property real sh: win.activeSelH * height

                            // Four outer scrims around the selected box.
                            Rectangle { x: 0; y: 0; width: parent.width; height: parent.sy; color: "#88000000" }
                            Rectangle { x: 0; y: parent.sy + parent.sh; width: parent.width; height: Math.max(0, parent.height - parent.sy - parent.sh); color: "#88000000" }
                            Rectangle { x: 0; y: parent.sy; width: parent.sx; height: parent.sh; color: "#88000000" }
                            Rectangle { x: parent.sx + parent.sw; y: parent.sy; width: Math.max(0, parent.width - parent.sx - parent.sw); height: parent.sh; color: "#88000000" }

                            Rectangle {
                                x: parent.sx
                                y: parent.sy
                                width: parent.sw
                                height: parent.sh
                                color: "transparent"
                                border.color: theme.accent
                                border.width: 2
                            }
                        }

                        // Compact floating action bar for an active print selection.
                        Rectangle {
                            visible: backend.hasSelection && !win.draggingSelection
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 14
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: selBarRow.implicitWidth + 24
                            height: 36
                            radius: Math.min(4, theme.radius)
                            color: "#e618181f"
                            border.color: theme.accent
                            border.width: 1

                            RowLayout {
                                id: selBarRow
                                anchors.centerIn: parent
                                spacing: 12

                                Text {
                                    text: {
                                        void translator.language
                                        const w = Number(backend.selectionPixelWidth).toLocaleString(Qt.locale(), 'f', 0)
                                        const h = Number(backend.selectionPixelHeight).toLocaleString(Qt.locale(), 'f', 0)
                                        return qsTr("Selection: %1 × %2").arg(w).arg(h)
                                    }
                                    color: "#f0f0f6"
                                    font.pixelSize: 12
                                }

                                Text {
                                    text: qsTr("Print Selection")
                                    color: theme.accent
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: win.openPrintSheet()
                                    }
                                }

                                Text {
                                    text: "✕"
                                    color: "#9a9aa4"
                                    font.pixelSize: 12
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            backend.clearSelection()
                                            win.selectMode = false
                                        }
                                    }
                                }
                            }
                        }

                        // Subtle previous / next navigation buttons for folders with multiple images.
                        Rectangle {
                            visible: backend.imageCount > 1 && !win.selectMode && (pointerTracker.hovered || prevMa.containsMouse)
                            anchors.left: parent.left
                            anchors.leftMargin: 16
                            anchors.verticalCenter: parent.verticalCenter
                            width: 40
                            height: 40
                            radius: Math.min(20, Math.max(theme.radius, 4))
                            color: prevMa.containsMouse ? "#cc24242c" : "#9916161c"
                            border.color: "#383842"
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: "‹"
                                color: "#f4f4f6"
                                font.pixelSize: 22
                            }

                            MouseArea {
                                id: prevMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { win.resetView(); backend.previousImage() }
                            }
                        }

                        Rectangle {
                            visible: backend.imageCount > 1 && !win.selectMode && (pointerTracker.hovered || nextMa.containsMouse)
                            anchors.right: parent.right
                            anchors.rightMargin: 16
                            anchors.verticalCenter: parent.verticalCenter
                            width: 40
                            height: 40
                            radius: Math.min(20, Math.max(theme.radius, 4))
                            color: nextMa.containsMouse ? "#cc24242c" : "#9916161c"
                            border.color: "#383842"
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: "›"
                                color: "#f4f4f6"
                                font.pixelSize: 22
                            }

                            MouseArea {
                                id: nextMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: { win.resetView(); backend.nextImage() }
                            }
                        }
                    }

                    // Drag-and-drop overlay anywhere over the canvas.
                    DropArea {
                        id: dropArea
                        anchors.fill: parent
                        onDropped: function(drop) {
                            if (drop.hasUrls) {
                                win.resetView()
                                backend.openUrls(drop.urls)
                            }
                        }
                    }

                    // Status / error banner floating at top center.
                    Rectangle {
                        id: toast
                        readonly property bool isError: backend.errorMessage.length > 0
                        readonly property string messageText: isError ? backend.errorMessage : backend.statusMessage
                        visible: messageText.length > 0
                        anchors.top: parent.top
                        anchors.topMargin: 14
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Math.min(parent.width - 40, toastRow.implicitWidth + 28)
                        height: 38
                        radius: Math.min(4, theme.radius)
                        color: isError ? "#3a1b1f" : "#1d251e"
                        border.color: isError ? "#8c333d" : theme.accent
                        border.width: 1

                        RowLayout {
                            id: toastRow
                            anchors.centerIn: parent
                            spacing: 10

                            Text {
                                text: toast.messageText
                                color: toast.isError ? "#ffc2c8" : "#f4f4f6"
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }

                            Text {
                                text: "✕"
                                color: "#9a9aa4"
                                font.pixelSize: 12
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: backend.clearMessages()
                                }
                            }
                        }
                    }
                }
            }

            // Compact bottom toolbar.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 54
                color: "#16161b"

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 1
                    color: "#26262e"
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 14
                    spacing: 6

                    // Sidebar toggle when multiple images exist.
                    ToolButton {
                        visible: backend.imageCount > 1
                        Material.roundedScale: Math.min(4, theme.radius)
                        implicitWidth: 36
                        implicitHeight: 36
                        text: "▤"
                        font.pixelSize: 15
                        highlighted: win.sidebarVisible
                        Material.accent: theme.accent
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Thumbnails (Tab)")
                        onClicked: win.sidebarVisible = !win.sidebarVisible
                    }

                    // Left: Open + file metadata.
                    Button {
                        Material.roundedScale: Math.min(4, theme.radius)
                        flat: true
                        text: qsTr("Open")
                        onClicked: backend.openFileDialog()
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 80
                        spacing: 1
                        visible: backend.hasImage

                        Text {
                            Layout.fillWidth: true
                            text: backend.fileName
                            color: "#f0f0f4"
                            font.pixelSize: 13
                            font.weight: Font.Medium
                            elide: Text.ElideMiddle
                        }

                        Text {
                            Layout.fillWidth: true
                            text: {
                                void translator.language
                                const w = Number(backend.imageWidth).toLocaleString(Qt.locale(), 'f', 0)
                                const h = Number(backend.imageHeight).toLocaleString(Qt.locale(), 'f', 0)
                                if (backend.imageCount > 1) {
                                    const idx = Number(backend.imageIndex + 1).toLocaleString(Qt.locale(), 'f', 0)
                                    const tot = Number(backend.imageCount).toLocaleString(Qt.locale(), 'f', 0)
                                    return qsTr("%1 × %2 · %3 / %4").arg(w).arg(h).arg(idx).arg(tot)
                                }
                                return qsTr("%1 × %2").arg(w).arg(h)
                            }
                            color: "#8e8e9a"
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        visible: !backend.hasImage
                    }

                    // Center: Select region, transform, and zoom controls.
                    RowLayout {
                        visible: backend.hasImage
                        spacing: 2

                        ToolButton {
                            Material.roundedScale: Math.min(4, theme.radius)
                            implicitWidth: 36
                            implicitHeight: 36
                            text: "⬚"
                            font.pixelSize: 16
                            highlighted: win.selectMode || backend.hasSelection
                            Material.accent: theme.accent
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Select Region to Print (S)")
                            onClicked: win.selectMode = !win.selectMode
                        }

                        ToolButton {
                            Material.roundedScale: Math.min(4, theme.radius)
                            text: "↺"
                            font.pixelSize: 16
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Rotate Left ([)")
                            onClicked: backend.rotateLeft()
                        }

                        ToolButton {
                            Material.roundedScale: Math.min(4, theme.radius)
                            text: "↻"
                            font.pixelSize: 16
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Rotate Right (])")
                            onClicked: backend.rotateRight()
                        }

                        ToolButton {
                            Material.roundedScale: Math.min(4, theme.radius)
                            text: "−"
                            font.pixelSize: 16
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Zoom Out (−)")
                            onClicked: win.zoomBy(0.8)
                        }

                        Button {
                            Material.roundedScale: Math.min(4, theme.radius)
                            flat: true
                            implicitWidth: 62
                            text: {
                                void translator.language
                                const pct = Math.round(win.effectiveScale * 100)
                                return qsTr("%1%").arg(Number(pct).toLocaleString(Qt.locale(), 'f', 0))
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Fit to Window (0)")
                            onClicked: win.resetView()
                        }

                        ToolButton {
                            Material.roundedScale: Math.min(4, theme.radius)
                            text: "+"
                            font.pixelSize: 16
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Zoom In (+)")
                            onClicked: win.zoomBy(1.25)
                        }
                    }

                    // Right: Primary Print button + Help button.
                    Button {
                        id: toolbarPrintBtn
                        Material.roundedScale: Math.min(4, theme.radius)
                        implicitHeight: 34
                        leftPadding: 16
                        rightPadding: 16
                        enabled: backend.hasImage
                        text: {
                            void translator.language
                            if (backend.selectedCount > 1) {
                                const c = Number(backend.selectedCount).toLocaleString(Qt.locale(), 'f', 0)
                                return qsTr("Print (%1)").arg(c)
                            }
                            return qsTr("Print")
                        }
                        contentItem: Text {
                            text: toolbarPrintBtn.text
                            color: toolbarPrintBtn.enabled ? theme.accentForeground : "#6e6e7a"
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: Math.min(4, theme.radius)
                            color: toolbarPrintBtn.enabled ? theme.accent : "#26262e"
                            opacity: toolbarPrintBtn.down ? 0.85 : (toolbarPrintBtn.hovered ? 0.92 : 1.0)
                        }
                        onClicked: win.openPrintSheet()
                    }

                    ToolButton {
                        Material.roundedScale: Math.min(4, theme.radius)
                        text: "?"
                        font.pixelSize: 14
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Keyboard Shortcuts (?)")
                        onClicked: {
                            win.printVisible = false
                            win.helpVisible = !win.helpVisible
                        }
                    }
                }
            }
        }

        // Print sheet modal.
        PrintSheet {
            visible: win.printVisible
            onCloseRequested: win.printVisible = false
        }

        // Keyboard shortcuts overlay.
        HelpOverlay {
            visible: win.helpVisible
            onCloseRequested: win.helpVisible = false
        }
    }
}
