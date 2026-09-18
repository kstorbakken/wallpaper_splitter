#include <QApplication>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QImage>
#include <QScreen>
#include <iostream>

#include "appsettings.h"
#include "outputservice.h"
#include "wallpapersplitter.h"

namespace {
bool parsePoint(const QString &value, QPoint *point, QString *error) {
    if (value.isEmpty()) {
        *point = {};
        return true;
    }
    const QStringList parts = value.split(',', Qt::KeepEmptyParts);
    bool xOk = false;
    bool yOk = false;
    if (parts.size() != 2) {
        *error = QObject::tr("Point '%1' must contain two comma-separated integers.").arg(value);
        return false;
    }
    const int x = parts.at(0).toInt(&xOk);
    const int y = parts.at(1).toInt(&yOk);
    if (!xOk || !yOk) {
        *error = QObject::tr("Point '%1' must contain two comma-separated integers.").arg(value);
        return false;
    }
    *point = {x, y};
    return true;
}

QList<ScreenCrop> automaticScreens(const QPoint &topLeft, const QPoint &bottomRight,
                                  const QSize &imageSize, bool largestFit) {
    QList<ScreenCrop> result;
    const auto screens = QApplication::screens();
    if (screens.isEmpty()) return result;
    const auto preferences = AppSettings::loadMonitors();
    if (largestFit) {
        const auto monitors = MonitorLayout::connectedMonitors();
        const auto layout = MonitorLayout::rectangles(monitors, preferences);
        QRectF bounds;
        for (const auto &rect : layout) bounds = bounds.united(rect);
        const QRect imageRect(QPoint(), imageSize);
        const auto fit = MonitorLayout::largestFit(bounds, imageRect);
        for (int i = 0; i < monitors.size(); ++i) {
            const auto crop = fit.mapRect(layout[i]);
            result.append({monitors[i].name, i + 1, monitors[i].desktopGeometry,
                           crop.toAlignedRect().intersected(imageRect)});
        }
        return result;
    }
    if (preferences.enabled) {
        const auto monitors = MonitorLayout::connectedMonitors();
        const auto layout = MonitorLayout::rectangles(monitors, preferences);
        QRectF bounds;
        for (const auto &rect : layout) bounds = bounds.united(rect);
        const double scale = bottomRight.isNull() ? 1.0
            : qMin(bottomRight.x() / bounds.width(), bottomRight.y() / bounds.height());
        for (int i = 0; i < monitors.size(); ++i) {
            const QRectF crop(QPointF(topLeft) + (layout[i].topLeft() - bounds.topLeft()) * scale,
                              layout[i].size() * scale);
            result.append({screens[i]->name(), i + 1, monitors[i].desktopGeometry, crop.toAlignedRect()});
        }
        return result;
    }
    for (int index = 0; index < screens.size(); ++index) {
        const QScreen *screen = screens.at(index);
        QRect crop = screen->geometry();
        const QPoint delta = screen->geometry().topLeft() - screens.first()->geometry().topLeft();
        crop.moveTopLeft(topLeft + delta);
        if (!bottomRight.isNull()) {
            crop.setSize(crop.size().scaled(bottomRight.x(), bottomRight.y(), Qt::KeepAspectRatio));
        }
        const QString screenName = screen->name().isEmpty()
                ? QStringLiteral("screen-%1").arg(index + 1) : screen->name();
        result.append({screenName, index + 1, screen->geometry(), crop});
    }
    return result;
}

int exitCode(OperationError error) {
    switch (error) {
        case OperationError::Arguments: return 2;
        case OperationError::ImageLayout: return 3;
        case OperationError::Collision:
        case OperationError::FileSystem: return 4;
        case OperationError::Plasma: return 5;
        case OperationError::None: return 0;
    }
    return 1;
}
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Wallpaper Splitter"));
    QApplication::setApplicationVersion(WALLPAPER_SPLITTER_VERSION);
    QApplication::setDesktopFileName(WALLPAPER_SPLITTER_APP_ID);

    if (argc <= 1) {
        WallpaperSplitter splitter;
        splitter.show();
        return QApplication::exec();
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate(
            "main", "Split or apply a wallpaper across all connected screens."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("input"),
                                 QCoreApplication::translate("main", "Image to split."));
    parser.addOptions({
            {{QStringLiteral("d"), QStringLiteral("destination")},
             QCoreApplication::translate("commandline", "Export images into <directory>."),
             QCoreApplication::translate("commandline", "directory")},
            {{QStringLiteral("p"), QStringLiteral("top-left")},
             QCoreApplication::translate("commandline", "Top-left crop-layout position as x,y."),
             QCoreApplication::translate("commandline", "x,y")},
            {{QStringLiteral("s"), QStringLiteral("bottom-right")},
             QCoreApplication::translate("commandline", "Maximum crop-layout size as width,height."),
             QCoreApplication::translate("commandline", "width,height")},
            {QStringLiteral("apply"),
             QCoreApplication::translate("commandline", "Apply crops to the current Plasma activity.")},
            {QStringLiteral("stretch-across-screens"),
             QCoreApplication::translate("commandline", "Stretch the entire image across the combined monitor layout.")},
            {QStringLiteral("fill-panorama"),
             QCoreApplication::translate("commandline", "Fill all screens with one continuous image, preserving proportions.")},
            {QStringLiteral("filename-template"),
             QCoreApplication::translate("commandline", "Override the export filename template."),
             QCoreApplication::translate("commandline", "template")},
            {QStringLiteral("collision"),
             QCoreApplication::translate("commandline", "Collision mode: fail, replace, or revision."),
             QCoreApplication::translate("commandline", "mode")}});
    parser.process(app);

    const QStringList arguments = parser.positionalArguments();
    if (arguments.size() != 1) {
        std::cerr << qPrintable(QCoreApplication::translate(
                "commandline", "Exactly one input image is required.")) << '\n';
        parser.showHelp(2);
    }
    if (parser.isSet(QStringLiteral("apply"))
        && (parser.isSet(QStringLiteral("destination"))
            || parser.isSet(QStringLiteral("filename-template"))
            || parser.isSet(QStringLiteral("collision")))) {
        std::cerr << qPrintable(QCoreApplication::translate(
                "commandline", "--destination, --filename-template, and --collision cannot be used with --apply."))
                  << '\n';
        return 2;
    }

    if (parser.isSet(QStringLiteral("stretch-across-screens")) && parser.isSet(QStringLiteral("fill-panorama"))) {
        std::cerr << "--stretch-across-screens and --fill-panorama cannot be combined.\n";
        return 2;
    }
    if ((parser.isSet(QStringLiteral("stretch-across-screens")) || parser.isSet(QStringLiteral("fill-panorama")))
        && (parser.isSet(QStringLiteral("top-left")) || parser.isSet(QStringLiteral("bottom-right")))) {
        std::cerr << qPrintable(QCoreApplication::translate("commandline",
                "Automatic layout options cannot be combined with --top-left or --bottom-right.")) << '\n';
        return 2;
    }

    QPoint topLeft;
    QPoint bottomRight;
    QString pointError;
    if (!parsePoint(parser.value(QStringLiteral("top-left")), &topLeft, &pointError)
        || !parsePoint(parser.value(QStringLiteral("bottom-right")), &bottomRight, &pointError)) {
        std::cerr << qPrintable(pointError) << '\n';
        return 2;
    }
    if (!bottomRight.isNull() && (bottomRight.x() <= 0 || bottomRight.y() <= 0)) {
        std::cerr << qPrintable(QCoreApplication::translate(
                "commandline", "The maximum crop-layout size must be positive.")) << '\n';
        return 2;
    }

    const QFileInfo imageFile(arguments.constFirst());
    QImage image(imageFile.absoluteFilePath());
    if (image.isNull()) {
        std::cerr << qPrintable(QCoreApplication::translate(
                "commandline", "Could not load input image: %1").arg(imageFile.absoluteFilePath())) << '\n';
        return 3;
    }
    const QSize originalSize = image.size();
    if (parser.isSet(QStringLiteral("stretch-across-screens")) || parser.isSet(QStringLiteral("fill-panorama"))) {
        QRectF bounds;
        const auto layout = MonitorLayout::rectangles(
                MonitorLayout::connectedMonitors(), AppSettings::loadMonitors());
        for (const auto &rect : layout) bounds = bounds.united(rect);
        if (parser.isSet(QStringLiteral("fill-panorama")))
            image = MonitorLayout::renderPanorama(image, layout);
        else if (!bounds.isEmpty())
            image = image.scaled(bounds.size().toSize(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    const QList<ScreenCrop> screens = automaticScreens(topLeft, bottomRight, image.size(),
            parser.isSet(QStringLiteral("stretch-across-screens")) ||
            parser.isSet(QStringLiteral("fill-panorama")));
    OperationResult result;
    if (parser.isSet(QStringLiteral("apply"))) {
        DBusPlasmaApplicator applicator;
        result = OutputService::applyManaged(image, originalSize, screens, imageFile.fileName(),
                                             imageFile.absoluteFilePath(), applicator);
    } else {
        const UserPreferences preferences = AppSettings::load();
        QString directory = parser.value(QStringLiteral("destination"));
        if (directory.isEmpty()) {
            directory = imageFile.absolutePath() + '/' + imageFile.baseName()
                    + QStringLiteral("_split");
        }
        ExportOptions options;
        options.directory = directory;
        options.fileNameTemplate = parser.isSet(QStringLiteral("filename-template"))
                ? parser.value(QStringLiteral("filename-template")) : preferences.fileNameTemplate;
        options.collisionPolicy = preferences.collisionPolicy == CollisionPolicy::Ask
                ? CollisionPolicy::Fail : preferences.collisionPolicy;
        if (parser.isSet(QStringLiteral("collision"))
            && (!OutputService::parseCollisionPolicy(parser.value(QStringLiteral("collision")),
                                                     &options.collisionPolicy)
                || options.collisionPolicy == CollisionPolicy::Ask)) {
            std::cerr << qPrintable(QCoreApplication::translate(
                    "commandline", "--collision must be fail, replace, or revision.")) << '\n';
            return 2;
        }
        result = OutputService::exportCrops(image, screens, imageFile.fileName(), options);
    }

    if (!result.success) {
        std::cerr << qPrintable(result.message) << '\n';
        if (!result.manifestPath.isEmpty()) {
            std::cerr << qPrintable(QCoreApplication::translate(
                    "commandline", "Managed set retained at %1").arg(
                            QFileInfo(result.manifestPath).absolutePath())) << '\n';
        }
        return exitCode(result.error);
    }
    for (const QString &path : result.paths) std::cout << path.toStdString() << '\n';
    if (!result.manifestPath.isEmpty()) std::cout << result.manifestPath.toStdString() << '\n';
    return 0;
}
