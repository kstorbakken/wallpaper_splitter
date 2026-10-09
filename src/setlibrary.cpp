#include "setlibrary.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>

namespace {
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
}

SetLibrary::SetLibrary(const QString &managedRoot)
    : root(managedRoot.isEmpty()
           ? QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                 .filePath("wallpaper-splitter/sets") : managedRoot) {}

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
    if (name.trimmed().isEmpty() || name.trimmed().size() > 200)
        return failure(QObject::tr("Choose a name between 1 and 200 characters."));
    WallpaperSet set;
    auto result = load(id, &set);
    if (!result.success) return result;
    set.manifest.insert("name", name.trimmed());
    return save(set);
}

OperationResult SetLibrary::reapply(const QString &id, const QList<QRect> &layout,
                                   PlasmaApplicator &applicator) const {
    WallpaperSet set;
    auto result = load(id, &set);
    if (!result.success) return result;
    if (!set.problem.isEmpty()) return failure(set.problem);
    auto remaining = layout;
    for (const auto &screen : set.screens) {
        if (!remaining.removeOne(screen.desktopGeometry))
            return OperationResult::failure(OperationError::ImageLayout,
                QObject::tr("This set does not match the current monitor layout. Restore its layout before applying it."));
    }
    if (!remaining.isEmpty())
        return OperationResult::failure(OperationError::ImageLayout,
                                        QObject::tr("This set does not cover every connected monitor."));
    // Decode every crop before touching Plasma, including images with valid headers but damaged data.
    for (const auto &path : set.paths)
        if (QImage(path).isNull()) return failure(QObject::tr("A generated image cannot be decoded."));
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

OperationResult SetLibrary::remove(const QString &id, PlasmaApplicator &applicator) const {
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
                QObject::tr("Plasma still references this set. Apply another wallpaper on every activity using it, then try again."));
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
