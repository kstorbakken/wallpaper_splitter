import QtQuick
import org.kde.plasma.plasmoid

WallpaperItem {
    id: wallpaper

    ScreenWallpaper {
        anchors.fill: parent
        fallbackCrops: wallpaper.configuration.ScreenCrops || "[]"
        stateLocation: wallpaper.configuration.LiveState || ""
    }
}
