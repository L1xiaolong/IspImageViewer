pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "../Isp"

Item {
    id: root
    objectName: "fullScreenPage"

    required property var controller
    property var propertiesController: null
    property bool designMode: false
    property string iconPrefix: "qrc:/icons/ui/"
    property bool topPanelVisible: false
    property bool rightPanelVisible: false
    property bool bottomPanelVisible: false
    property bool rightEdgeHovered: false
    property bool rightPanelBridgeHovered: false
    property bool rightPanelCardHovered: false
    property real rightPanelAnchorY: height / 2
    property string pixelText: ""
    property string inspectedPath: ""
    readonly property var imageCanvas: canvasLoader.item
    readonly property var navigationData: {
        if (!imageCanvas)
            return ({ "visible": false })
        const currentNavigationRevision = imageCanvas.navigationRevision
        return imageCanvas.navigationState(0)
    }

    signal closeRequested()

    function open(paths, initialIndex) {
        topPanelVisible = false
        rightPanelVisible = false
        bottomPanelVisible = false
        rightEdgeHovered = false
        rightPanelBridgeHovered = false
        rightPanelCardHovered = false
        rightPanelAnchorY = height / 2
        pixelText = ""
        inspectedPath = ""
        controller.open(paths, initialIndex)
        forceActiveFocus()
    }

    function showTopPanel() {
        topHideTimer.stop()
        topPanelVisible = true
    }
    function showRightPanel(anchorY) {
        rightHideTimer.stop()
        if (anchorY !== undefined && Number.isFinite(Number(anchorY)))
            rightPanelAnchorY = Math.max(0, Math.min(height, Number(anchorY)))
        rightPanelVisible = true
        if (propertiesController && inspectedPath !== controller.currentPath) {
            inspectedPath = controller.currentPath
            propertiesController.loadPath(inspectedPath)
        }
    }
    function scheduleRightPanelHide() {
        if (rightEdgeHovered || rightPanelBridgeHovered || rightPanelCardHovered) {
            rightHideTimer.stop()
            return
        }
        rightHideTimer.restart()
    }
    function showBottomPanel() {
        bottomHideTimer.stop()
        bottomPanelVisible = true
    }

    Keys.onEscapePressed: closeRequested()
    Keys.onLeftPressed: controller.showPrevious()
    Keys.onRightPressed: controller.showNext()
    Keys.onSpacePressed: controller.showNext()
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_F && event.modifiers === Qt.NoModifier) {
            controller.fitImage()
            event.accepted = true
        } else if (event.key === Qt.Key_1 && event.modifiers === Qt.NoModifier) {
            controller.actualPixels()
            event.accepted = true
        }
    }

    Timer { id: topHideTimer; interval: 650; onTriggered: root.topPanelVisible = false }
    Timer {
        id: rightHideTimer
        interval: 1400
        onTriggered: {
            if (!root.rightEdgeHovered && !root.rightPanelBridgeHovered
                    && !root.rightPanelCardHovered)
                root.rightPanelVisible = false
        }
    }
    Timer { id: bottomHideTimer; interval: 5000; onTriggered: root.bottomPanelVisible = false }

    Rectangle { anchors.fill: parent; color: "#A0A0A0" }

    Loader {
        id: canvasLoader
        anchors.fill: parent
        source: root.designMode
                ? Qt.resolvedUrl("../Isp/DesignFullScreenCanvas.qml")
                : Qt.resolvedUrl("../Isp/ProductionFullScreenCanvas.qml")
        onLoaded: if (item) item.controller = root.controller
    }

    Connections {
        target: root.imageCanvas
        ignoreUnknownSignals: true
        function onPixelHovered(sourceSlot, pixel, colorValue, valid) {
            if (!valid) {
                root.pixelText = ""
                return
            }
            root.pixelText = "(" + pixel.x + "," + pixel.y + ") RGBA("
                    + Math.round(colorValue.r * 255) + ","
                    + Math.round(colorValue.g * 255) + ","
                    + Math.round(colorValue.b * 255) + ","
                    + Math.round(colorValue.a * 255) + ")"
        }
        function onContextMenuRequested(position) {
            fullScreenContextMenu.popup(position.x, position.y)
        }
    }

    Connections {
        target: root.controller
        function onCloseRequested() { root.closeRequested() }
        function onStateChanged() {
            if (root.rightPanelVisible && root.controller.currentPath !== root.inspectedPath) {
                root.inspectedPath = root.controller.currentPath
                if (root.propertiesController)
                    root.propertiesController.loadPath(root.inspectedPath)
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 28
        color: "transparent"
        z: 20
        HoverHandler {
            onHoveredChanged: {
                if (hovered) root.showTopPanel()
                else if (root.topPanelVisible) topHideTimer.restart()
            }
        }
    }

    Rectangle {
        id: rightEdgeActivator
        objectName: "fullScreenRightEdgeActivator"
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: 28
        color: "transparent"
        z: 20
        HoverHandler {
            id: rightEdgeHover
            onHoveredChanged: {
                root.rightEdgeHovered = hovered
                if (hovered)
                    root.showRightPanel(rightEdgeHover.point.position.y)
                else if (root.rightPanelVisible)
                    root.scheduleRightPanelHide()
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 28
        color: "transparent"
        z: 20
        HoverHandler {
            onHoveredChanged: {
                if (hovered) root.showBottomPanel()
                else if (root.bottomPanelVisible) bottomHideTimer.restart()
            }
        }
    }

    Rectangle {
        id: topPanel
        objectName: "fullScreenTopPanel"
        visible: root.topPanelVisible
        anchors.top: parent.top
        anchors.topMargin: 12
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(780, parent.width - 48)
        height: 42
        radius: 7
        color: "#EAF8FAF9"
        border.width: 1
        border.color: "#C7D0D3"
        z: 30
        HoverHandler {
            onHoveredChanged: {
                if (hovered) topHideTimer.stop()
                else topHideTimer.restart()
            }
        }
        AppIconButton {
            anchors.left: parent.left
            anchors.leftMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            controlSize: 30
            renderedIconSize: 15
            enabled: root.controller.canGoPrevious
            iconSource: root.iconPrefix + "back.svg"
            toolTipText: "Previous image"
            onClicked: root.controller.showPrevious()
        }
        Text {
            anchors.centerIn: parent
            width: parent.width - 180
            text: root.controller.fileName
            color: Theme.graphiteInk
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideMiddle
            font.family: Theme.uiFont
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
        Text {
            anchors.right: nextButton.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: root.controller.positionText
            color: Theme.mutedInk
            font.family: Theme.monoFont
            font.pixelSize: 10
        }
        AppIconButton {
            id: nextButton
            anchors.right: closeButton.left
            anchors.rightMargin: 2
            anchors.verticalCenter: parent.verticalCenter
            controlSize: 30
            renderedIconSize: 15
            enabled: root.controller.canGoNext
            iconSource: root.iconPrefix + "forward.svg"
            toolTipText: "Next image"
            onClicked: root.controller.showNext()
        }
        AppIconButton {
            id: closeButton
            objectName: "closeFullScreenButton"
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            controlSize: 30
            renderedIconSize: 15
            iconSource: root.iconPrefix + "close.svg"
            toolTipText: "Exit full screen (Esc)"
            onClicked: root.closeRequested()
        }
    }

    Item {
        id: rightPanelBridge
        objectName: "fullScreenRightPanelBridge"
        visible: root.rightPanelVisible
        x: rightPanel.x - 12
        y: rightPanel.y - 12
        width: rightPanel.width + 26
        height: rightPanel.height + 24
        z: 29

        HoverHandler {
            onHoveredChanged: {
                root.rightPanelBridgeHovered = hovered
                if (hovered)
                    rightHideTimer.stop()
                else
                    root.scheduleRightPanelHide()
            }
        }
    }

    FullScreenPropertiesCard {
        id: rightPanel
        visible: root.rightPanelVisible
        anchors.right: parent.right
        anchors.rightMargin: 14
        y: Math.max(12, Math.min(parent.height - height - 12,
                                 root.rightPanelAnchorY - height / 2))
        width: Math.min(400, parent.width - 56)
        height: Math.min(600, parent.height - 40)
        controller: root.propertiesController
        iconPrefix: root.iconPrefix
        z: 30
        onHoverChanged: function(hovered) {
            root.rightPanelCardHovered = hovered
            if (hovered)
                rightHideTimer.stop()
            else
                root.scheduleRightPanelHide()
        }
    }

    Rectangle {
        id: bottomPanel
        objectName: "fullScreenBottomPanel"
        visible: root.bottomPanelVisible && root.pixelText.length > 0
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(430, pixelLabel.implicitWidth + 32)
        height: 34
        radius: 6
        color: "#EAF8FAF9"
        border.width: 1
        border.color: "#C7D0D3"
        z: 30
        HoverHandler {
            onHoveredChanged: {
                if (hovered) bottomHideTimer.stop()
                else bottomHideTimer.restart()
            }
        }
        Text {
            id: pixelLabel
            anchors.centerIn: parent
            text: root.pixelText
            color: Theme.graphiteInk
            font.family: Theme.monoFont
            font.pixelSize: 11
        }
    }

    Rectangle {
        id: navigationOverlay
        objectName: "fullScreenNavigationOverlay"
        visible: root.navigationData.visible === true
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 12
        width: root.navigationData.width || 0
        height: root.navigationData.height || 0
        radius: 3
        color: "#991E2227"
        border.width: 1
        border.color: "#80FFFFFF"
        z: 25

        Image {
            anchors.fill: parent
            anchors.margins: 3
            source: root.controller.currentPath
                    ? "image://thumbnail/" + encodeURIComponent(root.controller.currentPath)
                    : ""
            sourceSize: Qt.size(90, 65)
            fillMode: Image.Stretch
            opacity: 0.72
        }
        Rectangle {
            property rect normalizedViewport: root.navigationData.viewport || Qt.rect(0, 0, 0, 0)
            x: 3 + normalizedViewport.x * (parent.width - 6)
            y: 3 + normalizedViewport.y * (parent.height - 6)
            width: normalizedViewport.width * (parent.width - 6)
            height: normalizedViewport.height * (parent.height - 6)
            color: "#18FFFFFF"
            border.width: 1
            border.color: "white"
        }
        Label {
            objectName: "fullScreenZoomLabel"
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 5
            text: root.navigationData.zoom || ""
            color: "white"
            font.pixelSize: 8
            font.weight: Font.DemiBold
            style: Text.Outline
            styleColor: "#D9000000"
        }
    }

    Rectangle {
        visible: root.controller.loading || root.controller.errorText.length > 0
        anchors.centerIn: parent
        width: Math.min(420, messageText.implicitWidth + 36)
        height: 42
        radius: 6
        color: "#DCEEF1F1"
        border.width: 1
        border.color: "#BCC5C8"
        Text {
            id: messageText
            anchors.centerIn: parent
            text: root.controller.errorText.length > 0
                  ? root.controller.errorText : "Loading " + root.controller.fileName + "…"
            color: root.controller.errorText.length > 0 ? Theme.danger : Theme.graphiteInk
            font.family: Theme.uiFont
            font.pixelSize: 11
        }
    }

    AppMenu {
        id: fullScreenContextMenu
        objectName: "fullScreenContextMenu"
        AppMenuItem { text: "Cut"; shortcutText: Qt.platform.os === "osx" ? "⌘X" : "Ctrl+X"; onTriggered: root.controller.copyCurrent(true) }
        AppMenuItem { text: "Copy"; shortcutText: Qt.platform.os === "osx" ? "⌘C" : "Ctrl+C"; onTriggered: root.controller.copyCurrent(false) }
        AppMenuItem { text: "Rename…"; onTriggered: renameDialog.openWith(root.controller.fileName) }
        AppMenuItem { text: "Move to Trash"; destructive: true; onTriggered: trashDialog.showConfirmation() }
        AppMenuSeparator {}
        AppMenuItem { text: Qt.platform.os === "osx" ? "Reveal in Finder" : "Show in File Explorer"; onTriggered: root.controller.revealCurrent() }
        AppMenuSeparator {}
        AppMenu {
            title: "Display"
            AppMenuItem { text: "1:1"; onTriggered: root.controller.actualPixels() }
            AppMenuItem { text: "Fit"; onTriggered: root.controller.fitImage() }
        }
        AppMenuSeparator {}
        AppMenuItem { text: "Properties"; onTriggered: root.showRightPanel() }
    }

    AppTextInputDialog {
        id: renameDialog
        parent: root
        dialogTitle: "Rename image"
        description: "Enter a new name for the current image"
        acceptText: "Rename"
        onSubmitted: function(text) { complete(root.controller.renameCurrentTo(text)) }
    }

    AppConfirmDialog {
        id: trashDialog
        parent: root
        dialogTitle: "Move to Trash?"
        message: "The current image will be moved to the system Trash."
        confirmText: "Move"
        destructive: true
        onConfirmed: complete(root.controller.moveCurrentToTrash())
    }

    Shortcut { sequence: "PageUp"; onActivated: root.controller.showPrevious() }
    Shortcut { sequence: "PageDown"; onActivated: root.controller.showNext() }
    Shortcut { sequences: [StandardKey.Copy]; onActivated: root.controller.copyCurrent(false) }
    Shortcut { sequences: [StandardKey.Cut]; onActivated: root.controller.copyCurrent(true) }
}
