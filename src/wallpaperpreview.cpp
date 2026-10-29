#include "wallpaperpreview.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

QString WallpaperPreview::lockPath() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
            .filePath("wallpaper-splitter/preview.lock");
}

WallpaperPreview::WallpaperPreview(PlasmaApplicator &applicator)
    : applicator(applicator), lock(lockPath()) {
    lock.setStaleLockTime(0);
}

WallpaperPreview::~WallpaperPreview() {
    if (active() && !finish().success) directory.setAutoRemove(false);
}

OperationResult WallpaperPreview::start(const QImage &image, const QList<ScreenCrop> &screens) {
    if (active()) return OperationResult::failure(OperationError::Arguments,
                                                  QObject::tr("A preview is already active."));
    if (!directory.isValid()) return OperationResult::failure(OperationError::FileSystem,
                                                              QObject::tr("Could not create temporary preview files."));
    QDir().mkpath(QFileInfo(lockPath()).absolutePath());
    if (!lock.tryLock()) return OperationResult::failure(OperationError::Collision,
                                                        QObject::tr("Another wallpaper operation is in progress."));
    const ExportOptions options{directory.path(), QStringLiteral("preview-{number}"),
                                CollisionPolicy::Replace, OutputFormat::Png};
    auto result = OutputService::exportCrops(image, screens, QStringLiteral("preview"), options);
    const auto paths = result.paths;
    if (result.success) result = applicator.captureWallpaper(&snapshot);
    if (!result.success || snapshot.size() != screens.size()) {
        snapshot = {};
        lock.unlock();
        return result.success ? OperationResult::failure(OperationError::Plasma,
                    QObject::tr("Plasma could not save the previous wallpaper for every monitor.")) : result;
    }
    const auto applied = applicator.apply(screens, paths);
    if (!applied.success) {
        const auto restored = finish();
        if (!restored.success) return OperationResult::failure(OperationError::Plasma,
                applied.message + '\n' + restored.message);
    }
    return applied;
}

OperationResult WallpaperPreview::finish() {
    if (!active()) return OperationResult::ok();
    const auto result = applicator.restoreWallpaper(snapshot);
    if (result.success) {
        snapshot = {};
        lock.unlock();
        directory.remove();
    }
    return result;
}
