#include "setlibrary.h"
#include "monitorlayout.h"
#include "wallpaperpreview.h"

#include <KConfig>
#include <KConfigGroup>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>

namespace {
const auto lockScreenConfigName = QStringLiteral("kscreenlockerrc");
const auto imageWallpaperPlugin = QStringLiteral("org.kde.image");
const auto splitWallpaperPlugin = QStringLiteral("io.github.kstorbakken.wallpaper_splitter.lockscreen");

OperationResult failure(const QString &message) {
    return OperationResult::failure(OperationError::FileSystem, message);
}
QRect rectangle(const QJsonValue &value) {
    const auto o = value.toObject();
    return {o.value("x").toInt(), o.value("y").toInt(),
            o.value("width").toInt(), o.value("height").toInt()};
}
OperationResult save(const WallpaperSet &set) {
    QSaveFile file(QDir(set.directory).filePath("manifest.json"));
    const auto data = QJsonDocument(set.manifest).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        return failure(QObject::tr("Could not save the set: %1").arg(file.errorString()));
    return OperationResult::ok();
}

OperationResult validateLayout(const QList<ScreenCrop> &screens, QList<QRect> remaining) {
    for (const auto &screen : screens) {
        if (!remaining.removeOne(screen.desktopGeometry))
            return OperationResult::failure(OperationError::ImageLayout,
                QObject::tr("This set does not match the current monitor layout. Restore its layout before applying it."));
    }
    if (!remaining.isEmpty())
        return OperationResult::failure(OperationError::ImageLayout,
            QObject::tr("This set does not cover every connected monitor."));
    return OperationResult::ok();
}

OperationResult installLockScreenWallpaper() {
    const QDir data(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
    const QDir package(data.absoluteFilePath("plasma/wallpapers/" + splitWallpaperPlugin));
    const QStringList files{"contents/config/main.xml", "contents/ui/main.qml", "metadata.json"};
    for (const auto &file : files) {
        QFile resource(":/lockscreen-wallpaper/" + file);
        if (!resource.open(QIODevice::ReadOnly))
            return failure(QObject::tr("The bundled lock screen wallpaper could not be read."));
        const QByteArray contents = resource.readAll();
        const QString path = package.filePath(file);
        QFile installed(path);
        if (installed.open(QIODevice::ReadOnly) && installed.readAll() == contents) continue;
        installed.close();
        QSaveFile output(path);
        if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !output.open(QIODevice::WriteOnly)
            || output.write(contents) != contents.size() || !output.commit())
            return failure(QObject::tr("Could not install the Plasma wallpaper plugin at %1.").arg(path));
    }
    return OperationResult::ok();
}
}

SetLibrary::SetLibrary(const QString &managedRoot)
    : root(managedRoot.isEmpty() ? OutputService::defaultManagedRoot() : managedRoot) {}

OperationResult SetLibrary::load(const QString &id, WallpaperSet *set) const {
    if (set == nullptr) return failure(QObject::tr("No set output was provided."));
    *set = {};
    set->id = id;
    set->name = id;
    set->directory = QDir(root).absoluteFilePath(id);
    const QFileInfo directory(set->directory);
    if (!QRegularExpression("^set-[a-f0-9]{32}$").match(id).hasMatch()
        || directory.isSymLink() || !directory.isDir())
        return failure(QObject::tr("Invalid managed set directory."));
    const QString manifestPath = QDir(set->directory).filePath("manifest.json");
    QFile file(manifestPath);
    if (QFileInfo(manifestPath).isSymLink() || !file.open(QIODevice::ReadOnly)
        || file.size() > 1024 * 1024)
        return failure(QObject::tr("The set manifest cannot be read."));
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    set->manifest = document.object();
    const auto &m = set->manifest;
    if (error.error != QJsonParseError::NoError || m.value("schemaVersion").toInt() != 1
        || m.value("id").toString() != id || m.value("crops").toArray().isEmpty())
        return failure(QObject::tr("The set manifest is invalid or uses an unsupported version."));
    set->name = m.value("name").toString();
    if (set->name.isEmpty()) set->name = m.value("sourceName").toString(id);
    QSet<QString> paths;
    QSet<int> numbers;
    QList<QRect> geometries;
    for (const auto &value : m.value("crops").toArray()) {
        const auto crop = value.toObject();
        const QString relative = crop.value("path").toString();
        const QString path = QDir(set->directory).filePath(relative);
        const ScreenCrop screen{crop.value("screenName").toString(),
                                crop.value("screenNumber").toInt(),
                                rectangle(crop.value("desktopGeometry")),
                                rectangle(crop.value("cropRectangle"))};
        if (relative.isEmpty() || relative.contains('/') || relative.contains('\\')
            || relative == "." || relative == ".." || relative == "manifest.json"
            || QFileInfo(path).isSymLink() || paths.contains(relative)
            || screen.number < 1 || numbers.contains(screen.number)
            || !screen.desktopGeometry.isValid() || !screen.cropRect.isValid()
            || geometries.contains(screen.desktopGeometry))
            return failure(QObject::tr("The set contains an unsafe path or invalid crop mapping."));
        paths.insert(relative);
        numbers.insert(screen.number);
        geometries.append(screen.desktopGeometry);
        set->screens.append(screen);
        set->paths.append(path);
        QImageReader reader(path);
        if (!reader.canRead() || reader.size() != screen.cropRect.size())
            set->problem = QObject::tr("A generated image is missing, unreadable, or has changed dimensions.");
    }
    return OperationResult::ok();
}

QList<WallpaperSet> SetLibrary::sets() const {
    QList<WallpaperSet> result;
    for (const auto &id : QDir(root).entryList({"set-*"}, QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        WallpaperSet set;
        const auto loaded = load(id, &set);
        if (!loaded.success) set.problem = loaded.message;
        result.append(set);
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return a.manifest.value("createdAt").toString() > b.manifest.value("createdAt").toString();
    });
    return result;
}

OperationResult SetLibrary::rename(const QString &id, const QString &name) const {
    QLockFile lock(QDir(root).filePath(".operation.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock()) return OperationResult::failure(OperationError::Collision,
        QObject::tr("A library operation is in progress. Try again shortly."));
    if (name.trimmed().isEmpty() || name.trimmed().size() > 200)
        return failure(QObject::tr("Choose a name between 1 and 200 characters."));
    WallpaperSet set;
    auto result = load(id, &set);
    if (!result.success) return result;
    set.manifest.insert("name", name.trimmed());
    return save(set);
}

OperationResult SetLibrary::reapply(const QString &id, const QList<QRect> &layout,
                                   PlasmaApplicator &applicator, bool *applicationAttempted) const {
    if (applicationAttempted) *applicationAttempted = false;
    QDir().mkpath(QFileInfo(WallpaperPreview::lockPath()).absolutePath());
    QLockFile previewLock(WallpaperPreview::lockPath());
    previewLock.setStaleLockTime(0);
    if (!previewLock.tryLock()) return OperationResult::failure(OperationError::Collision,
            QObject::tr("A wallpaper preview is active. Try again after it ends."));
    if (!QFileInfo(root).isDir()) return failure(QObject::tr("The wallpaper library is unavailable."));
    QLockFile lock(QDir(root).filePath(".operation.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock()) return OperationResult::failure(OperationError::Collision,
        QObject::tr("A library operation is in progress. Try again shortly."));
    WallpaperSet set;
    auto result = load(id, &set);
    if (!result.success) return result;
    if (!set.problem.isEmpty()) return failure(set.problem);
    result = validateLayout(set.screens, layout);
    if (!result.success) return result;
    // Decode every crop before touching Plasma, including images with valid headers but damaged data.
    for (const auto &path : set.paths)
        if (QImage(path).isNull()) return failure(QObject::tr("A generated image cannot be decoded."));
    if (applicationAttempted) *applicationAttempted = true;
    result = applicator.apply(set.screens, set.paths);
    set.manifest.insert("status", result.success ? "applied" : "failed");
    set.manifest.insert("lastAppliedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (result.success) set.manifest.remove("error");
    else set.manifest.insert("error", result.message);
    const auto saved = save(set);
    return saved.success ? result : saved;
}

OperationResult SetLibrary::exportSet(const QString &id, const ExportOptions &options) const {
    WallpaperSet set;
    const auto result = load(id, &set);
    if (!result.success) return result;
    if (!set.problem.isEmpty()) return failure(set.problem);
    const QString sourceName = set.manifest.value("sourceName").toString(set.name);
    return OutputService::exportCrops(set.paths, set.screens, sourceName, options);
}

OperationResult SetLibrary::applyLockScreen(const QString &id, const QList<MonitorInfo> &monitors) const {
    QLockFile lock(QDir(root).filePath(".operation.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock()) return OperationResult::failure(OperationError::Collision,
        QObject::tr("A library operation is in progress. Try again shortly."));
    WallpaperSet set;
    auto result = load(id, &set);
    if (!result.success) return result;
    if (!set.problem.isEmpty()) return failure(set.problem);
    QList<QRect> layout;
    for (const auto &monitor : monitors) layout.append(monitor.desktopGeometry);
    result = validateLayout(set.screens, layout);
    if (!result.success) return result;

    QJsonArray crops;
    for (int i = 0; i < set.screens.size(); ++i) {
        const auto &screen = set.screens.at(i);
        const auto &path = set.paths.at(i);
        if (QImage(path).size() != screen.cropRect.size())
            return failure(QObject::tr("A generated image cannot be decoded or has changed dimensions."));
        const QRect geometry = screen.desktopGeometry;
        const QString &name = monitors.at(layout.indexOf(geometry)).name;
        crops.append(QJsonObject{{"name", name}, {"x", geometry.x()}, {"y", geometry.y()},
            {"width", geometry.width()}, {"height", geometry.height()},
            {"url", QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)}});
    }

    KConfig config(lockScreenConfigName, KConfig::NoGlobals);
    auto greeter = config.group("Greeter");
    auto wallpaper = greeter.group("Wallpaper").group(splitWallpaperPlugin).group("General");
    if (config.accessMode() != KConfig::ReadWrite || greeter.isEntryImmutable("WallpaperPlugin")
        || wallpaper.isEntryImmutable("ScreenCrops"))
        return OperationResult::failure(OperationError::Plasma,
            QObject::tr("The lock screen wallpaper settings are read-only."));
    result = installLockScreenWallpaper();
    if (!result.success) return result;
    wallpaper.writeEntry("ScreenCrops", QString::fromUtf8(QJsonDocument(crops).toJson(QJsonDocument::Compact)));
    greeter.writeEntry("WallpaperPlugin", splitWallpaperPlugin);
    if (!config.sync()) {
        config.markAsClean();
        return OperationResult::failure(OperationError::Plasma,
            QObject::tr("Could not save the lock screen wallpaper settings."));
    }
    return OperationResult::ok(set.paths);
}

OperationResult SetLibrary::remove(const QString &id, PlasmaApplicator &applicator) const {
    QLockFile lock(QDir(root).filePath(".operation.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock()) return OperationResult::failure(OperationError::Collision,
        QObject::tr("A library operation is in progress. Try again shortly."));
    WallpaperSet set;
    auto result = load(id, &set);
    if (!result.success) return result;
    QStringList owned{"manifest.json"};
    for (const auto &path : set.paths) owned.append(QFileInfo(path).fileName());
    const QDir directory(set.directory);
    const auto entries = directory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const auto &entry : entries)
        if (!owned.contains(entry) || QFileInfo(directory.filePath(entry)).isDir())
            return failure(QObject::tr("The set directory contains unrecognized files; it was not deleted."));
    result = applicator.wallpaperReferences();
    if (!result.success) return result;
    const KConfig lockScreenConfig(lockScreenConfigName, KConfig::NoGlobals);
    if (lockScreenConfig.accessMode() == KConfig::NoAccess
        && !QStandardPaths::locate(QStandardPaths::GenericConfigLocation, lockScreenConfigName).isEmpty())
        return OperationResult::failure(OperationError::Plasma,
            QObject::tr("Could not read the lock screen wallpaper settings."));
    const auto greeter = lockScreenConfig.group("Greeter");
    const QString plugin = greeter.readEntry("WallpaperPlugin", imageWallpaperPlugin);
    const auto wallpaper = greeter.group("Wallpaper").group(plugin).group("General");
    if (plugin == splitWallpaperPlugin) {
        const auto crops = QJsonDocument::fromJson(wallpaper.readEntry("ScreenCrops", QString()).toUtf8()).array();
        for (const auto &crop : crops) result.paths.append(crop.toObject().value("url").toString());
    } else {
        result.paths.append(wallpaper.readPathEntry("Image", QString()));
        result.paths.append(wallpaper.readPathEntry("SlidePaths", QStringList()));
    }
    for (const auto &reference : result.paths) {
        if (reference.isEmpty()) continue;
        const QUrl url(reference);
        const QString path = url.isLocalFile() ? url.toLocalFile() : reference;
        const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
        const QString canonical = QFileInfo(path).canonicalFilePath();
        const QString prefix = QFileInfo(set.directory).canonicalFilePath() + '/';
        if (absolute == QDir::cleanPath(set.directory) || absolute.startsWith(set.directory + '/')
            || canonical.startsWith(prefix)
            || (QFileInfo(path).isDir() && (set.directory.startsWith(absolute + '/')
                || prefix.startsWith(canonical + '/'))))
            return OperationResult::failure(OperationError::Plasma,
                QObject::tr("Plasma still references this set. Replace it wherever it is used on the desktop or lock screen, then try again."));
    }
    for (const auto &path : set.paths)
        if (QFileInfo::exists(path) && !QFile::remove(path))
            return failure(QObject::tr("Could not delete %1.").arg(path));
    if (!QFile::remove(directory.filePath("manifest.json")) || !QDir().rmdir(set.directory))
        return failure(QObject::tr("Could not finish deleting the set directory."));
    return OperationResult::ok();
}

QImage SetLibrary::preview(const WallpaperSet &set, const QSize &size) {
    QRect bounds;
    for (const auto &screen : set.screens) bounds = bounds.united(screen.cropRect);
    if (!bounds.isValid() || !size.isValid()) return {};
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    const double scale = std::min(double(size.width()) / bounds.width(), double(size.height()) / bounds.height());
    painter.translate((size.width() - bounds.width() * scale) / 2,
                      (size.height() - bounds.height() * scale) / 2);
    painter.scale(scale, scale);
    painter.translate(-bounds.topLeft());
    for (int i = 0; i < set.screens.size(); ++i) {
        const QRect rect = set.screens[i].cropRect;
        QImageReader reader(set.paths.value(i));
        reader.setScaledSize((QSizeF(rect.size()) * scale).toSize().expandedTo(QSize(1, 1)));
        painter.fillRect(rect, Qt::darkGray);
        painter.drawImage(rect, reader.read());
    }
    return image;
}
