#ifndef WALLPAPER_SPLITTER_SETLIBRARY_H
#define WALLPAPER_SPLITTER_SETLIBRARY_H

#include "outputservice.h"
#include <QJsonObject>

struct WallpaperSet {
    QString id;
    QString name;
    QString directory;
    QJsonObject manifest;
    QList<ScreenCrop> screens;
    QStringList paths;
    QString problem;
};

class SetLibrary {
public:
    explicit SetLibrary(const QString &root = {});
    QList<WallpaperSet> sets() const;
    OperationResult load(const QString &id, WallpaperSet *set) const;
    OperationResult rename(const QString &id, const QString &name) const;
    // applicationAttempted distinguishes unusable sets from failures after
    // touching Plasma, so a slideshow can safely skip only the former.
    OperationResult reapply(const QString &id, const QList<QRect> &layout,
                            PlasmaApplicator &applicator, bool *applicationAttempted = nullptr) const;
    OperationResult exportSet(const QString &id, const ExportOptions &options) const;
    OperationResult remove(const QString &id, PlasmaApplicator &applicator) const;
    static QImage preview(const WallpaperSet &set, const QSize &size);
private:
    QString root;
};
#endif
