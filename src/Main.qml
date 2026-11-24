import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Peek 1.0

ApplicationWindow {
    id: win
    width: 1060
    height: 700
    minimumWidth: 740
    minimumHeight: 500
    visible: true
    title: backend.hasImage ? (backend.hasUnsavedEdits ? "• " : "") + backend.fileName + " — Peek" : "Peek"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    property bool printVisible: false
    property bool resizeVisible: false
    property bool colorVisible: false
    property bool inspectorVisible: false
    property bool markupMode: false
    property bool textCalloutOpen: false
    property real textCalloutNormX: 0.5
    property real textCalloutNormY: 0.5
    property bool exportVisible: false
    property bool quitConfirmVisible: false
    property bool sidebarVisible: false
    property bool selectMode: false
    property string aspectPreset: "free" // free | 1:1 | 4:3 | 3:2 | 16:9 | a4
    property bool showCheckerboard: false

    readonly property bool modalOpen: helpVisible || printVisible || resizeVisible || exportVisible || quitConfirmVisible
    readonly property bool typingText: textCalloutOpen || printSheet.savingPreset || resizeVisible

    // Viewer zoom/pan state.
    property string zoomMode: "fit"
    property real zoomScale: 1.0
    property real panX: 0
    property real panY: 0

    // Live rubber-band state while dragging a selection marquee.
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

    function aspectRatioValue(preset) {
        switch (preset) {
        case "1:1": return 1.0
        case "4:3": return 4.0 / 3.0
        case "3:2": return 3.0 / 2.0
        case "16:9": return 16.0 / 9.0
        case "a4": return 210.0 / 297.0
        default: return 0.0
        }
    }

    function closeAllModals() {
        helpVisible = false
        printVisible = false
        resizeVisible = false
        exportVisible = false
        quitConfirmVisible = false
    }

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
        closeAllModals()
        backend.preparePrintSheet()
        printVisible = true
    }

    function openExportSheet() {
        if (!backend.hasImage)
            return
        closeAllModals()
        backend.refreshExportEstimate()
        exportVisible = true
    }

    function requestQuit() {
        backend.commitColorEdit()
        if (backend.hasUnsavedEdits) {
            closeAllModals()
            quitConfirmVisible = true
        } else {
            Qt.quit()
        }
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
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.typingText; onActivated: { const next = !win.helpVisible; win.closeAllModals(); win.helpVisible = next } }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.typingText; onActivated: win.requestQuit() }
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (win.textCalloutOpen) win.textCalloutOpen = false
            else if (printSheet.savingPreset) printSheet.savingPreset = false
            else if (win.modalOpen) win.closeAllModals()
            else if (win.colorVisible) { backend.commitColorEdit(); win.colorVisible = false }
            else if (win.inspectorVisible) win.inspectorVisible = false
            else if (win.markupMode) { markupDraft.clear(); win.markupMode = false }
            else if (backend.hasSelection) { backend.clearSelection(); win.selectMode = false }
            else if (win.selectMode) win.selectMode = false
            else if (backend.errorMessage.length > 0) backend.clearMessages()
        }
    }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !win.typingText; onActivated: win.openPrintSheet() }
    Shortcut { sequences: [StandardKey.Print, "P"]; context: Qt.ApplicationShortcut; enabled: !win.helpVisible && !win.resizeVisible && !win.exportVisible && !win.typingText; onActivated: { if (win.printVisible) win.printVisible = false; else win.openPrintSheet() } }
    Shortcut { sequences: ["Return", "Enter"]; context: Qt.ApplicationShortcut; enabled: win.printVisible && !printSheet.savingPreset && !backend.printBusy && backend.printer.length > 0; onActivated: { backend.printCurrent(); win.printVisible = false } }
    Shortcut { sequences: [StandardKey.Open, "O"]; context: Qt.ApplicationShortcut; enabled: !win.modalOpen && !win.typingText; onActivated: backend.openFileDialog() }
    Shortcut { sequences: [StandardKey.Save]; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.helpVisible && !win.resizeVisible; onActivated: backend.savePdfDialog() }
    Shortcut { sequences: ["Ctrl+Shift+S", "E"]; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.modalOpen && !win.typingText; onActivated: win.openExportSheet() }
    Shortcut { sequence: "Ctrl+Shift+R"; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.modalOpen && !win.typingText; onActivated: { win.closeAllModals(); win.resizeVisible = true } }
    Shortcut { sequence: "Ctrl+Shift+C"; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.modalOpen && !win.typingText; onActivated: { win.inspectorVisible = false; win.colorVisible = !win.colorVisible } }
    Shortcut { sequence: "M"; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.modalOpen && !win.typingText; onActivated: { win.selectMode = false; win.markupMode = !win.markupMode } }
    Shortcut { sequence: "I"; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.modalOpen && !win.typingText; onActivated: { win.colorVisible = false; win.inspectorVisible = !win.inspectorVisible } }
    Shortcut { sequence: "Ctrl+K"; context: Qt.ApplicationShortcut; enabled: backend.hasSelection && !win.modalOpen && !win.typingText; onActivated: { backend.cropToSelection(); win.selectMode = false; win.resetView() } }
    Shortcut { sequences: [StandardKey.Copy]; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.modalOpen && !win.typingText; onActivated: backend.copyToClipboard() }
    Shortcut { sequence: "Tab"; context: Qt.ApplicationShortcut; enabled: backend.imageCount > 1 && !win.modalOpen && !win.typingText; onActivated: win.sidebarVisible = !win.sidebarVisible }
    Shortcut { sequence: "S"; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.modalOpen && !win.typingText; onActivated: { win.markupMode = false; win.selectMode = !win.selectMode } }
    Shortcut { sequences: [StandardKey.SelectAll]; context: Qt.ApplicationShortcut; enabled: backend.imageCount > 1 && !win.modalOpen && !win.typingText; onActivated: { win.sidebarVisible = true; backend.selectAllFiles() } }
    Shortcut { sequences: ["PgDown"]; context: Qt.ApplicationShortcut; enabled: win.printVisible && backend.previewPageCount > 1; onActivated: backend.nextPreviewPage() }
    Shortcut { sequences: ["PgUp"]; context: Qt.ApplicationShortcut; enabled: win.printVisible && backend.previewPageCount > 1; onActivated: backend.previousPreviewPage() }
    Shortcut { sequences: ["Right", "L"]; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.modalOpen && !win.typingText; onActivated: { win.resetView(); backend.nextImage() } }
    Shortcut { sequences: ["Left", "H"]; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.modalOpen && !win.typingText; onActivated: { win.resetView(); backend.previousImage() } }
    Shortcut { sequences: ["+", "="]; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.modalOpen && !win.typingText; onActivated: win.zoomBy(1.25) }
    Shortcut { sequence: "-"; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.modalOpen && !win.typingText; onActivated: win.zoomBy(0.8) }
    Shortcut { sequences: ["0", "F"]; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.modalOpen && !win.typingText; onActivated: win.resetView() }
    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.modalOpen && !win.typingText; onActivated: win.setActualSize() }
    Shortcut { sequences: ["[", "Ctrl+L"]; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.modalOpen && !win.typingText; onActivated: backend.rotateLeft() }
    Shortcut { sequences: ["]", "Ctrl+R"]; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.modalOpen && !win.typingText; onActivated: backend.rotateRight() }
    Shortcut { sequence: "Shift+H"; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.modalOpen && !win.typingText; onActivated: backend.toggleFlipHorizontal() }
    Shortcut { sequence: "Shift+V"; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.modalOpen && !win.typingText; onActivated: backend.toggleFlipVertical() }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; enabled: backend.canUndo && !win.modalOpen && !win.typingText; onActivated: backend.undo() }
    Shortcut { sequences: [StandardKey.Redo]; context: Qt.ApplicationShortcut; enabled: backend.canRedo && !win.modalOpen && !win.typingText; onActivated: backend.redo() }
    Shortcut { sequence: "T"; context: Qt.ApplicationShortcut; enabled: backend.hasImage && !win.modalOpen && !win.typingText; onActivated: win.showCheckerboard = !win.showCheckerboard }

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

                        MouseArea {
                            id: panArea
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            cursorShape: (win.markupMode || win.selectMode || win.draggingSelection)
                                         ? Qt.CrossCursor
                                         : (pressed ? Qt.ClosedHandCursor : (win.zoomMode === "manual" ? Qt.OpenHandCursor : Qt.ArrowCursor))

                            property real lastX: 0
                            property real lastY: 0
                            property bool isSelectingDrag: false
                            property bool isMarkupDrag: false

                            function toImageNormX(px) {
                                return Math.max(0.0, Math.min(1.0, (px - imageFrame.x) / Math.max(1.0, imageFrame.width)))
                            }
                            function toImageNormY(py) {
                                return Math.max(0.0, Math.min(1.0, (py - imageFrame.y) / Math.max(1.0, imageFrame.height)))
                            }

                            onPressed: function(mouse) {
                                lastX = mouse.x
                                lastY = mouse.y
                                isSelectingDrag = false
                                isMarkupDrag = false

                                if (win.markupMode) {
                                    const nx = toImageNormX(mouse.x)
                                    const ny = toImageNormY(mouse.y)
                                    if (markupBar.currentTool === "text") {
                                        // Place text callout right under the pointer.
                                        win.textCalloutNormX = nx
                                        win.textCalloutNormY = ny
                                        calloutInput.text = ""
                                        win.textCalloutOpen = true
                                        calloutInput.forceActiveFocus()
                                    } else {
                                        win.textCalloutOpen = false
                                        isMarkupDrag = true
                                        markupDraft.beginStroke(nx, ny)
                                    }
                                } else if (win.selectMode || (mouse.modifiers & Qt.ShiftModifier)) {
                                    isSelectingDrag = true
                                    win.draggingSelection = true
                                    win.dragStartNormX = toImageNormX(mouse.x)
                                    win.dragStartNormY = toImageNormY(mouse.y)
                                    win.dragCurNormX = win.dragStartNormX
                                    win.dragCurNormY = win.dragStartNormY
                                }
                            }

                            onPositionChanged: function(mouse) {
                                if (!pressed || !backend.hasImage)
                                    return
                                if (isMarkupDrag) {
                                    markupDraft.updateStroke(toImageNormX(mouse.x), toImageNormY(mouse.y))
                                } else if (isSelectingDrag) {
                                    let nx = toImageNormX(mouse.x)
                                    let ny = toImageNormY(mouse.y)
                                    const ratio = win.aspectRatioValue(win.aspectPreset)
                                    if (ratio > 0.0 && backend.imageWidth > 0 && backend.imageHeight > 0) {
                                        const dxPx = (nx - win.dragStartNormX) * backend.imageWidth
                                        const dySign = ny >= win.dragStartNormY ? 1.0 : -1.0
                                        const dyPx = Math.abs(dxPx) / ratio * dySign
                                        ny = Math.max(0.0, Math.min(1.0, win.dragStartNormY + dyPx / backend.imageHeight))
                                    }
                                    win.dragCurNormX = nx
                                    win.dragCurNormY = ny
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
                                if (isMarkupDrag) {
                                    isMarkupDrag = false
                                    backend.commitDraftAnnotation(markupDraft)
                                } else if (isSelectingDrag) {
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
                                if (win.selectMode || win.markupMode)
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

                        // Live C++ draft overlay while drawing annotations.
                        MarkupDraft {
                            id: markupDraft
                            x: imageFrame.x
                            y: imageFrame.y
                            width: imageFrame.width
                            height: imageFrame.height
                            tool: markupBar.currentTool
                            color: markupBar.currentColor
                            strokeSize: markupBar.strokeSize
                        }

                        // Inline text callout box placed right where the user clicked.
                        Rectangle {
                            visible: win.textCalloutOpen
                            x: Math.max(12, Math.min(canvasArea.width - width - 12, imageFrame.x + win.textCalloutNormX * imageFrame.width - 20))
                            y: Math.max(60, Math.min(canvasArea.height - height - 16, imageFrame.y + win.textCalloutNormY * imageFrame.height - 18))
                            width: 240
                            height: 36
                            radius: Math.min(4, theme.radius)
                            color: "#f016161d"
                            border.color: theme.accent
                            border.width: 2

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 6
                                spacing: 6

                                TextInput {
                                    id: calloutInput
                                    Layout.fillWidth: true
                                    color: "#f4f4f6"
                                    font.pixelSize: 13
                                    clip: true
                                    onAccepted: {
                                        if (text.trim().length > 0)
                                            backend.addTextAnnotation(win.textCalloutNormX, win.textCalloutNormY, text, markupBar.currentColor, markupBar.strokeSize)
                                        win.textCalloutOpen = false
                                    }
                                    Text {
                                        anchors.fill: parent
                                        verticalAlignment: Text.AlignVCenter
                                        visible: !calloutInput.text
                                        text: qsTr("Type annotation…")
                                        color: "#7a7a88"
                                        font.pixelSize: 12
                                    }
                                }

                                Text {
                                    text: "↵"
                                    color: theme.accent
                                    font.pixelSize: 14
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (calloutInput.text.trim().length > 0)
                                                backend.addTextAnnotation(win.textCalloutNormX, win.textCalloutNormY, calloutInput.text, markupBar.currentColor, markupBar.strokeSize)
                                            win.textCalloutOpen = false
                                        }
                                    }
                                }
                            }
                        }

                        // Floating Markup Toolbar docked at top-center.
                        MarkupBar {
                            id: markupBar
                            visible: win.markupMode && backend.hasImage && !win.modalOpen
                            anchors.top: parent.top
                            anchors.topMargin: 12
                            anchors.horizontalCenter: parent.horizontalCenter
                            onCloseRequested: {
                                win.textCalloutOpen = false
                                markupDraft.clear()
                                win.markupMode = false
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

                        // Aspect-ratio strip when Select/Crop mode is active.
                        Rectangle {
                            visible: win.selectMode && !win.modalOpen
                            anchors.top: parent.top
                            anchors.topMargin: 12
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: aspectRow.implicitWidth + 16
                            height: 32
                            radius: Math.min(4, theme.radius)
                            color: "#e618181f"
                            border.color: "#32323c"
                            border.width: 1

                            RowLayout {
                                id: aspectRow
                                anchors.centerIn: parent
                                spacing: 4

                                Text {
                                    text: qsTr("Ratio:")
                                    color: "#8e8e9a"
                                    font.pixelSize: 11
                                    Layout.rightMargin: 2
                                }

                                Repeater {
                                    model: [
                                        { value: "free", label: qsTr("Free") },
                                        { value: "1:1", label: "1:1" },
                                        { value: "4:3", label: "4:3" },
                                        { value: "3:2", label: "3:2" },
                                        { value: "16:9", label: "16:9" },
                                        { value: "a4", label: "A4" }
                                    ]

                                    Rectangle {
                                        required property var modelData
                                        readonly property bool active: win.aspectPreset === modelData.value
                                        implicitWidth: ratioLbl.implicitWidth + 12
                                        implicitHeight: 22
                                        radius: Math.min(4, theme.radius)
                                        color: active ? theme.accent : (rMa.containsMouse ? "#262630" : "transparent")

                                        Text {
                                            id: ratioLbl
                                            anchors.centerIn: parent
                                            text: modelData.label
                                            color: active ? theme.accentForeground : "#d4d4de"
                                            font.pixelSize: 11
                                            font.weight: active ? Font.DemiBold : Font.Normal
                                        }

                                        MouseArea {
                                            id: rMa
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: win.aspectPreset = modelData.value
                                        }
                                    }
                                }
                            }
                        }

                        // Floating action bar for an active selection: Crop, Copy, Print Selection.
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
                                        return qsTr("%1 × %2").arg(w).arg(h)
                                    }
                                    color: "#a4a4b0"
                                    font.pixelSize: 12
                                }

                                Text {
                                    text: qsTr("Crop")
                                    color: theme.accent
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            backend.cropToSelection()
                                            win.selectMode = false
                                            win.resetView()
                                        }
                                    }
                                }

                                Text {
                                    text: qsTr("Copy")
                                    color: "#e4e4ec"
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: backend.copyToClipboard()
                                    }
                                }

                                Text {
                                    text: qsTr("Print Selection")
                                    color: "#e4e4ec"
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
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

                        // Floating Adjust Color HUD on right edge of canvas.
                        ColorSheet {
                            visible: win.colorVisible && backend.hasImage && !win.modalOpen
                            anchors.top: parent.top
                            anchors.topMargin: win.markupMode ? 64 : 14
                            anchors.right: parent.right
                            anchors.rightMargin: 14
                            onCloseRequested: win.colorVisible = false
                        }

                        // Floating Inspector HUD on right edge of canvas.
                        InspectorSheet {
                            visible: win.inspectorVisible && backend.hasImage && !win.modalOpen
                            anchors.top: parent.top
                            anchors.topMargin: win.markupMode ? 64 : 14
                            anchors.right: parent.right
                            anchors.rightMargin: 14
                            onCloseRequested: win.inspectorVisible = false
                        }

                        // Subtle previous / next navigation buttons.
                        Rectangle {
                            visible: backend.imageCount > 1 && !win.selectMode && !win.markupMode && (pointerTracker.hovered || prevMa.containsMouse)
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
                            visible: backend.imageCount > 1 && !win.selectMode && !win.markupMode && !win.colorVisible && !win.inspectorVisible && (pointerTracker.hovered || nextMa.containsMouse)
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

                    // Status / error toast banner.
                    Rectangle {
                        id: toast
                        readonly property bool isError: backend.errorMessage.length > 0
                        readonly property string messageText: isError ? backend.errorMessage : backend.statusMessage
                        visible: messageText.length > 0
                        anchors.top: parent.top
                        anchors.topMargin: win.selectMode ? 52 : 14
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
                    anchors.leftMargin: 10
                    anchors.rightMargin: 12
                    spacing: 4

                    ToolButton {
                        visible: backend.imageCount > 1
                        Material.roundedScale: Math.min(4, theme.radius)
                        implicitWidth: 34
                        implicitHeight: 34
                        text: "▤"
                        font.pixelSize: 15
                        highlighted: win.sidebarVisible
                        Material.accent: theme.accent
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Thumbnails (Tab)")
                        onClicked: win.sidebarVisible = !win.sidebarVisible
                    }

                    Button {
                        Material.roundedScale: Math.min(4, theme.radius)
                        flat: true
                        text: qsTr("Open")
                        onClicked: backend.openFileDialog()
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 70
                        spacing: 1
                        visible: backend.hasImage

                        Text {
                            Layout.fillWidth: true
                            text: (backend.hasUnsavedEdits ? "• " : "") + backend.fileName
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

                    // Center: Edit, Transform & Zoom controls.
                    RowLayout {
                        visible: backend.hasImage
                        spacing: 1

                        ToolButton {
                            Material.roundedScale: Math.min(4, theme.radius)
                            implicitWidth: 34; implicitHeight: 34
                            text: "⬚"
                            font.pixelSize: 15
                            highlighted: win.selectMode || backend.hasSelection
                            Material.accent: theme.accent
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Select / Crop (S)")
                            onClicked: { win.markupMode = false; win.selectMode = !win.selectMode }
                        }

                        ToolButton {
                            Material.roundedScale: Math.min(4, theme.radius)
                            implicitWidth: 34; implicitHeight: 34
                            text: "✎"
                            font.pixelSize: 15
                            highlighted: win.markupMode || backend.annotationCount > 0
                            Material.accent: theme.accent
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Markup Toolbar (M)")
                            onClicked: { win.selectMode = false; win.markupMode = !win.markupMode }
                        }

                        ToolButton {
                            Material.roundedScale: Math.min(4, theme.radius)
                            implicitWidth: 34; implicitHeight: 34
                            text: "⤢"
                            font.pixelSize: 15
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Adjust Size (Ctrl+Shift+R)")
                            onClicked: { win.closeAllModals(); win.resizeVisible = true }
                        }

                        ToolButton {
                            Material.roundedScale: Math.min(4, theme.radius)
                            implicitWidth: 34; implicitHeight: 34
                            text: "◐"
                            font.pixelSize: 15
                            highlighted: win.colorVisible || backend.hasColorAdjustments
                            Material.accent: theme.accent
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Adjust Color (Ctrl+Shift+C)")
                            onClicked: { win.inspectorVisible = false; win.colorVisible = !win.colorVisible }
                        }

                        ToolButton {
                            Material.roundedScale: Math.min(4, theme.radius)
                            implicitWidth: 34; implicitHeight: 34
                            text: "ⓘ"
                            font.pixelSize: 15
                            highlighted: win.inspectorVisible
                            Material.accent: theme.accent
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Inspector (I)")
                            onClicked: { win.colorVisible = false; win.inspectorVisible = !win.inspectorVisible }
                        }

                        ToolButton {
                            Material.roundedScale: Math.min(4, theme.radius)
                            implicitWidth: 34; implicitHeight: 34
                            text: "↺"
                            font.pixelSize: 15
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Rotate Left ([)")
                            onClicked: backend.rotateLeft()
                        }

                        ToolButton {
                            Material.roundedScale: Math.min(4, theme.radius)
                            implicitWidth: 34; implicitHeight: 34
                            text: "↻"
                            font.pixelSize: 15
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Rotate Right (])")
                            onClicked: backend.rotateRight()
                        }

                        ToolButton {
                            Material.roundedScale: Math.min(4, theme.radius)
                            implicitWidth: 30; implicitHeight: 34
                            text: "−"
                            font.pixelSize: 15
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Zoom Out (−)")
                            onClicked: win.zoomBy(0.8)
                        }

                        Button {
                            Material.roundedScale: Math.min(4, theme.radius)
                            flat: true
                            implicitWidth: 56
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
                            implicitWidth: 30; implicitHeight: 34
                            text: "+"
                            font.pixelSize: 15
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Zoom In (+)")
                            onClicked: win.zoomBy(1.25)
                        }
                    }

                    // Right: Export + Primary Print + Help.
                    Button {
                        id: toolbarExportBtn
                        visible: backend.hasImage
                        Material.roundedScale: Math.min(4, theme.radius)
                        implicitHeight: 34
                        leftPadding: 12
                        rightPadding: 12
                        text: qsTr("Export")
                        contentItem: Text {
                            text: toolbarExportBtn.text
                            color: "#e2e2ec"
                            font.pixelSize: 12
                            font.weight: Font.Medium
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: Math.min(4, theme.radius)
                            color: toolbarExportBtn.hovered ? "#25252f" : "#1b1b22"
                            border.color: "#32323c"
                            border.width: 1
                        }
                        onClicked: win.openExportSheet()
                    }

                    Button {
                        id: toolbarPrintBtn
                        Material.roundedScale: Math.min(4, theme.radius)
                        implicitHeight: 34
                        leftPadding: 15
                        rightPadding: 15
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
                        implicitWidth: 32; implicitHeight: 34
                        text: "?"
                        font.pixelSize: 14
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Keyboard Shortcuts (?)")
                        onClicked: {
                            const next = !win.helpVisible
                            win.closeAllModals()
                            win.helpVisible = next
                        }
                    }
                }
            }
        }

        PrintSheet {
            id: printSheet
            visible: win.printVisible
            onCloseRequested: {
                printSheet.savingPreset = false
                win.printVisible = false
            }
        }

        ResizeSheet {
            visible: win.resizeVisible
            onCloseRequested: win.resizeVisible = false
        }

        ExportSheet {
            visible: win.exportVisible
            onCloseRequested: win.exportVisible = false
        }

        QuitConfirmOverlay {
            visible: win.quitConfirmVisible
            onCancelRequested: win.quitConfirmVisible = false
            onExportRequested: win.openExportSheet()
            onDiscardQuitRequested: Qt.quit()
        }

        HelpOverlay {
            visible: win.helpVisible
            onCloseRequested: win.helpVisible = false
        }
    }
}
