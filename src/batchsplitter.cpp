#include "batchsplitter.h"
#include <QDir>
#include <QFileInfo>
#include <QImageReader>

QStringList BatchSplitter::imagesInFolder(const QString &folder) {
    QStringList result;
    const auto formats = QImageReader::supportedImageFormats();
    for (const auto &file : QDir(folder).entryInfoList(QDir::Files | QDir::Readable, QDir::Name))
        if (formats.contains(file.suffix().toLower().toLatin1())) result.append(file.absoluteFilePath());
    return result;
}

OperationResult BatchSplitter::prepare(const QImage &source, const QList<MonitorInfo> &monitors,
                                       const MonitorPreferences &preferences, BatchLayout mode,
                                       QImage *image, QList<ScreenCrop> *screens) {
    if (!image || !screens || source.isNull() || monitors.isEmpty())
        return OperationResult::failure(OperationError::ImageLayout,
                                        QObject::tr("An image and connected monitors are required."));
    screens->clear();
    const auto layout = MonitorLayout::rectangles(monitors, preferences);
    QRectF bounds;
    for (const auto &rect : layout) bounds = bounds.united(rect);
    if (bounds.isEmpty()) return OperationResult::failure(OperationError::ImageLayout,
                                                         QObject::tr("The monitor layout is empty."));
    if (mode == BatchLayout::Panorama) *image = MonitorLayout::renderPanorama(source, layout);
    else if (mode == BatchLayout::Stretch)
        *image = source.scaled(bounds.size().toSize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    else *image = source;
    if (image->isNull()) return OperationResult::failure(OperationError::ImageLayout,
                                                        QObject::tr("Could not render the image."));
    const auto fit = MonitorLayout::largestFit(bounds, image->rect());
    for (int i = 0; i < monitors.size(); ++i)
        screens->append({monitors[i].name, i + 1, monitors[i].desktopGeometry,
                         fit.mapRect(layout[i]).toAlignedRect().intersected(image->rect())});
    return OperationResult::ok();
}
