pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Window

Item {
    id: root

    property string fallbackCrops: "[]"
    property url stateLocation
    property string monitorName: Screen.name
    property rect monitorGeometry: Qt.rect(Screen.virtualX, Screen.virtualY, Screen.width, Screen.height)

    property string lastPublication: ""
    property var slide: null
    property Image front: first
    property Image back: second
    readonly property int fadeDuration: 450
    readonly property url nextSource: sourceForScreen(slide ? slide.crops : parseCrops(fallbackCrops))

    function parseCrops(text) {
        try {
            const crops = JSON.parse(text)
            return Array.isArray(crops) ? crops : []
        } catch (error) {
            return []
        }
    }

    function sourceForScreen(crops) {
        const named = monitorName ? crops.filter(crop => crop && crop.name === monitorName) : []
        const geometry = monitorGeometry
        const crop = named.length === 1 ? named[0] : crops.find(crop => crop
            && crop.x === geometry.x && crop.y === geometry.y
            && crop.width === geometry.width && crop.height === geometry.height)
        return crop ? crop.url : ""
    }

    function refreshSlide() {
        const settings = liveSettings.item as Settings
        if (!settings)
            return
        settings.sync()
        const text = settings.value("CurrentSlide", "")
        if (text === lastPublication)
            return
        lastPublication = text
        try {
            const current = JSON.parse(text)
            if (Array.isArray(current.crops) && Number.isFinite(current.fadeAt))
                slide = current
        } catch (error) {
            // Keep displaying the last usable slide.
        }
    }

    function loadNext() {
        if (!nextSource.toString())
            return
        fadeStart.stop()
        if (fade.running) {
            fade.stop()
            finishFade()
        }
        if (front.source.toString() === nextSource.toString()) {
            back.source = ""
            return
        }
        back.source = nextSource
        if (back.status === Image.Ready)
            prepareFade(back)
    }

    function prepareFade(image) {
        if (image !== back || image.status !== Image.Ready)
            return
        if (front.status !== Image.Ready) {
            finishFade()
            return
        }
        fadeStart.interval = Math.max(0, (slide ? slide.fadeAt : 0) - Date.now())
        fadeStart.restart()
    }

    function startFade() {
        // A greeter that catches up after a display wakes can skip an expired fade.
        const elapsed = slide ? Math.max(0, Date.now() - slide.fadeAt) : fadeDuration
        if (elapsed >= fadeDuration) {
            finishFade()
            return
        }
        back.z = 1
        front.z = 0
        fade.target = back
        fade.from = elapsed / fadeDuration
        fade.duration = fadeDuration - elapsed
        fade.start()
    }

    function finishFade() {
        const previous = front
        front = back
        back = previous
        front.opacity = 1
        front.z = 1
        back.opacity = 0
        back.z = 0
        back.source = ""
    }

    onNextSourceChanged: if (first && second) loadNext()
    Component.onCompleted: loadNext()

    Loader {
        id: liveSettings
        active: root.stateLocation.toString() !== ""
        sourceComponent: Settings { location: root.stateLocation }
        onLoaded: root.refreshSlide()
    }

    Timer {
        interval: 250
        repeat: true
        running: liveSettings.status === Loader.Ready
        onTriggered: root.refreshSlide()
    }

    Timer {
        id: fadeStart
        onTriggered: root.startFade()
    }

    NumberAnimation {
        id: fade
        property: "opacity"
        to: 1
        onFinished: root.finishFade()
    }

    Image {
        id: first
        anchors.fill: parent
        fillMode: Image.Stretch
        asynchronous: true
        opacity: 0
        onStatusChanged: root.prepareFade(first)
    }

    Image {
        id: second
        anchors.fill: parent
        fillMode: Image.Stretch
        asynchronous: true
        opacity: 0
        onStatusChanged: root.prepareFade(second)
    }
}
