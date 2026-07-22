pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import "."

Rectangle {
    id: root
    objectName: "fullScreenPropertiesCard"

    property var controller: null
    property string iconPrefix: "qrc:/icons/ui/"
    property int currentTab: 0
    readonly property var tabs: controller && controller.hasRawParameters
                                ? ["File", "EXIF", "Histogram", "RAW"]
                                : ["File", "EXIF", "Histogram"]
    signal hoverChanged(bool hovered)

    onTabsChanged: {
        if (currentTab >= tabs.length)
            currentTab = Math.max(0, tabs.length - 1)
    }

    radius: 8
    color: "#F8FAF9"
    border.width: 1
    border.color: "#C9D0D3"
    clip: true

    HoverHandler { onHoveredChanged: root.hoverChanged(hovered) }

    Rectangle {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 48
        color: "#F8FAF9"
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.top: parent.top
            anchors.topMargin: 8
            text: "IMAGE INSPECTION"
            color: Theme.probeBlue
            font.family: Theme.uiFont
            font.pixelSize: 7
            font.weight: Font.DemiBold
            font.letterSpacing: 1.0
        }
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 7
            text: root.controller ? root.controller.fileName : ""
            color: Theme.graphiteInk
            elide: Text.ElideMiddle
            font.family: Theme.uiFont
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.opticalGray
        }
    }

    Row {
        id: tabsRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        height: 30
        spacing: 2
        Repeater {
            model: root.tabs
            delegate: Button {
                id: tabButton
                required property int index
                required property string modelData
                objectName: "fullScreenPropertyTab-" + index
                text: modelData
                height: 28
                width: Math.max(52, tabText.implicitWidth + 16)
                onClicked: root.currentTab = index
                contentItem: Text {
                    id: tabText
                    text: tabButton.text
                    color: root.currentTab === tabButton.index ? Theme.graphiteInk : Theme.mutedInk
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.family: Theme.uiFont
                    font.pixelSize: 9
                    font.weight: root.currentTab === tabButton.index ? Font.DemiBold : Font.Medium
                }
                background: Item {
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: root.currentTab === tabButton.index ? 2 : 0
                        color: Theme.probeBlue
                    }
                }
            }
        }
    }

    ScrollView {
        id: scroll
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabsRow.bottom
        anchors.bottom: parent.bottom
        anchors.margins: 8
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Column {
            width: scroll.availableWidth
            spacing: 6

            PropertyFieldList {
                visible: root.currentTab === 0 || root.currentTab === 1 || root.currentTab === 3
                width: parent.width
                fields: !root.controller ? []
                      : root.currentTab === 0 ? root.controller.basicFields
                      : root.currentTab === 1 ? root.controller.exifFields
                      : root.controller.rawFields
            }

            Loader {
                active: root.currentTab === 2
                visible: active
                width: parent.width
                height: active && item ? item.implicitHeight : 0
                sourceComponent: PropertiesHistogram {
                    controller: root.controller
                    width: parent ? parent.width : 0
                }
            }
        }
    }
}
