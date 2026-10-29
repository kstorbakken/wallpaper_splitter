#ifndef WALLPAPER_SPLITTER_BATCHSPLITTER_H
#define WALLPAPER_SPLITTER_BATCHSPLITTER_H
#include "monitorlayout.h"
#include "outputservice.h"

enum class BatchLayout { Centered, Stretch, Panorama };
class BatchSplitter {
public:
    static QStringList imagesInFolder(const QString &folder);
    static OperationResult prepare(const QImage &source, const QList<MonitorInfo> &monitors,
                                    const MonitorPreferences &preferences, BatchLayout mode,
                                    QImage *image, QList<ScreenCrop> *screens);
};
#endif
