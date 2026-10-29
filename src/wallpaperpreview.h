#ifndef WALLPAPER_SPLITTER_WALLPAPERPREVIEW_H
#define WALLPAPER_SPLITTER_WALLPAPERPREVIEW_H

#include "outputservice.h"
#include <QTemporaryDir>
#include <QLockFile>

// Owns temporary crops until Plasma has confirmed restoration.
class WallpaperPreview {
public:
    explicit WallpaperPreview(PlasmaApplicator &applicator);
    ~WallpaperPreview();
    OperationResult start(const QImage &image, const QList<ScreenCrop> &screens);
    OperationResult finish();
    bool active() const { return !snapshot.isEmpty(); }
    static QString lockPath();
private:
    PlasmaApplicator &applicator;
    QTemporaryDir directory;
    QLockFile lock;
    QJsonArray snapshot;
};
#endif
