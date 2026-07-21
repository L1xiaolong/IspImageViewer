import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."

Rectangle {
    id: root
    property int slot: 0
    property int source: 0 // 0: display, 1: source planes
    property int channelIndex: 0
    property var histogramData: {
        const revision = compareController.histogramRevision
        return compareController.histogram(root.slot, root.source)
    }

    color: "#EAF0F4"
    border.color: "#C9D4DC"
    radius: Theme.radius
    implicitWidth: 336
    implicitHeight: 288
    clip: true

    function request() { compareController.requestHistogram(slot, source) }
    function channels() { return histogramData.channels || [] }
    function selectedChannel() {
        const values = channels()
        return values.length ? values[Math.min(channelIndex, values.length - 1)] : null
    }

    onSourceChanged: { channelIndex = 0; request() }
    onHistogramDataChanged: {
        if (channelIndex >= channels().length) channelIndex = 0
        graph.requestPaint()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 7

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label { text: "Histogram"; color: Theme.graphiteInk; font.pixelSize: 12; font.weight: Font.DemiBold; Layout.fillWidth: true }
            ComboBox {
                id: sourcePicker
                model: ["Display", "Source planes"]
                currentIndex: root.source
                implicitHeight: 26
                implicitWidth: 110
                onActivated: root.source = currentIndex
                background: Rectangle { color: Theme.paperWhite; border.color: Theme.opticalGray; radius: 4 }
                contentItem: Text { leftPadding: 8; text: sourcePicker.displayText; verticalAlignment: Text.AlignVCenter; color: Theme.graphiteInk; font.pixelSize: 11 }
            }
            AppIconButton { iconSource: "qrc:/icons/ui/more.svg"; toolTipText: "Refresh histogram"; onClicked: root.request() }
        }

        ComboBox {
            id: channelPicker
            visible: root.channels().length > 1
            Layout.fillWidth: true
            implicitHeight: 25
            model: root.channels().map(function(channel) { return channel.name })
            currentIndex: root.channelIndex
            onActivated: root.channelIndex = currentIndex
            background: Rectangle { color: Theme.paperWhite; border.color: Theme.opticalGray; radius: 4 }
            contentItem: Text { leftPadding: 8; text: channelPicker.displayText; verticalAlignment: Text.AlignVCenter; color: Theme.graphiteInk; font.pixelSize: 11 }
        }

        Canvas {
            id: graph
            Layout.fillWidth: true
            Layout.preferredHeight: 118
            antialiasing: true
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.fillStyle = "#FCFCFA"
                ctx.fillRect(0, 0, width, height)
                ctx.strokeStyle = "#DCE3E8"
                ctx.lineWidth = 1
                for (let row = 0; row < 4; ++row) {
                    const y = 8 + row * (height - 20) / 3
                    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke()
                }
                const channel = root.selectedChannel()
                if (!channel || !channel.bins || !channel.bins.length) {
                    ctx.fillStyle = "#7A8790"; ctx.font = "11px sans-serif"; ctx.textAlign = "center"
                    ctx.fillText(root.histogramData.summary || "Loading histogram…", width / 2, height / 2)
                    return
                }
                let maximum = 0
                for (let i = 0; i < channel.bins.length; ++i) maximum = Math.max(maximum, Number(channel.bins[i]))
                if (!maximum) return
                const bottom = height - 12
                const usable = height - 20
                const samples = Math.max(2, Math.floor(width))
                ctx.beginPath()
                for (let point = 0; point < samples; ++point) {
                    const start = Math.floor(point * channel.bins.length / samples)
                    const end = Math.max(start + 1, Math.floor((point + 1) * channel.bins.length / samples))
                    let peak = 0
                    for (let index = start; index < end; ++index) peak = Math.max(peak, Number(channel.bins[index]))
                    const x = point * width / (samples - 1)
                    const y = bottom - usable * Math.log(peak + 1) / Math.log(maximum + 1)
                    if (point === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                }
                ctx.lineTo(width, bottom); ctx.lineTo(0, bottom); ctx.closePath()
                ctx.globalAlpha = 0.26; ctx.fillStyle = channel.color; ctx.fill(); ctx.globalAlpha = 1
                ctx.strokeStyle = channel.color; ctx.lineWidth = 1.35; ctx.stroke()
            }
        }

        Label {
            Layout.fillWidth: true
            text: root.histogramData.summary || "Waiting for image data…"
            color: Theme.mutedInk
            font.pixelSize: 10
            wrapMode: Text.Wrap
            maximumLineCount: 2
        }

        GridLayout {
            columns: 3
            visible: root.selectedChannel() !== null
            Layout.fillWidth: true
            rowSpacing: 2
            columnSpacing: 8
            Repeater {
                model: root.selectedChannel() ? [
                    ["Mean", Number(root.selectedChannel().mean).toFixed(2)],
                    ["Variance", Number(root.selectedChannel().variance).toFixed(2)],
                    ["Median", Number(root.selectedChannel().median).toFixed(1)],
                    ["Min", root.selectedChannel().min],
                    ["Max", root.selectedChannel().max],
                    ["", ""]
                ] : []
                delegate: RowLayout {
                    Layout.fillWidth: true
                    Label { text: modelData[0]; color: Theme.mutedInk; font.pixelSize: 9 }
                    Label { text: modelData[1]; color: Theme.graphiteInk; font.pixelSize: 9; font.family: Theme.monoFont; Layout.fillWidth: true; horizontalAlignment: Text.AlignRight }
                }
            }
        }
    }
}
