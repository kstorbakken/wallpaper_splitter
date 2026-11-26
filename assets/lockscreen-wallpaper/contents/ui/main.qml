import QtQuick
import QtQuick.Window
import org.kde.plasma.plasmoid

WallpaperItem {
    id: wallpaper

    readonly property var crops: {
        try {
            const parsed = JSON.parse(wallpaper.configuration.ScreenCrops || "[]")
            return Array.isArray(parsed) ? parsed : []
        } catch (error) {
            return []
        }
    }

    function sourceForScreen(name, x, y, width, height) {
        const namedCrops = name ? crops.filter(crop => crop && crop.name === name) : []
        const crop = namedCrops.length === 1 ? namedCrops[0] : crops.find(crop => crop
            && crop.x === x && crop.y === y && crop.width === width && crop.height === height)
        return crop ? crop.url : ""
    }

    Image {
        anchors.fill: parent
        source: wallpaper.sourceForScreen(Screen.name, Screen.virtualX, Screen.virtualY,
                                          Screen.width, Screen.height)
        fillMode: Image.Stretch
        asynchronous: true
    }
}
