import QtQuick

ImageCanvas {
    id: root
    property var controller: null
    presentationMode: 0
    viewSynchronized: false

    onControllerChanged: {
        if (controller)
            controller.attachCanvas(root)
    }
    Component.onCompleted: {
        if (controller)
            controller.attachCanvas(root)
    }
}
