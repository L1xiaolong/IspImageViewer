import QtQuick

Rectangle {
    id: root

    required property var controller
    property int slot: 0
    property var histogramData: {
        const revision = root.controller.histogramRevision
        return root.controller.histogram(root.slot)
    }

    implicitWidth: 156
    implicitHeight: 64
    radius: 3
    color: "#A61E2227"
    border.width: 1
    border.color: "#42FFFFFF"
    clip: true

    function request() {
        root.controller.requestHistogram(root.slot)
    }

    onHistogramDataChanged: graph.requestPaint()

    Canvas {
        id: graph
        anchors.fill: parent
        anchors.margins: 5
        antialiasing: true

        onPaint: {
            const context = getContext("2d")
            context.reset()

            const channels = root.histogramData.channels || []
            const channel = channels.length > 0 ? channels[0] : null
            if (!channel || !channel.bins || channel.bins.length === 0)
                return

            let maximum = 0
            for (let index = 0; index < channel.bins.length; ++index)
                maximum = Math.max(maximum, Number(channel.bins[index]))
            if (maximum <= 0)
                return

            const bottom = height - 1
            const samples = Math.max(2, Math.floor(width))
            context.beginPath()
            context.moveTo(0, bottom)
            for (let point = 0; point < samples; ++point) {
                const start = Math.floor(point * channel.bins.length / samples)
                const end = Math.max(start + 1,
                                     Math.floor((point + 1) * channel.bins.length / samples))
                let peak = 0
                for (let bin = start; bin < end; ++bin)
                    peak = Math.max(peak, Number(channel.bins[bin]))
                const x = point * width / (samples - 1)
                const y = bottom - (height - 3) * Math.log(peak + 1) / Math.log(maximum + 1)
                context.lineTo(x, y)
            }
            context.lineTo(width, bottom)
            context.closePath()
            context.fillStyle = "#66F4F5F2"
            context.fill()
            context.strokeStyle = "#E8F4F5F2"
            context.lineWidth = 1
            context.stroke()
        }
    }
}
