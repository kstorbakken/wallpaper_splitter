#include "outputservice.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSet>
#include <KConfig>
#include <KConfigGroup>

#include <algorithm>
#include <utility>

namespace {
OperationResult writeImage(const QImage &image, const QString &path) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return OperationResult::failure(OperationError::FileSystem,
                                        QObject::tr("Could not open %1 for writing: %2")
                                                .arg(path, file.errorString()));
    }
    if (!image.save(&file, "PNG")) {
        file.cancelWriting();
        return OperationResult::failure(OperationError::FileSystem,
                                        QObject::tr("Could not encode %1 as PNG.").arg(path));
    }
    if (!file.commit()) {
        return OperationResult::failure(OperationError::FileSystem,
                                        QObject::tr("Could not finish writing %1: %2")
                                                .arg(path, file.errorString()));
    }
    return OperationResult::ok({path});
}

OperationResult writeJson(const QJsonObject &object, const QString &path) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return OperationResult::failure(OperationError::FileSystem,
                                        QObject::tr("Could not open %1 for writing: %2")
                                                .arg(path, file.errorString()));
    }
    if (file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        return OperationResult::failure(OperationError::FileSystem,
                                        QObject::tr("Could not write manifest %1: %2")
                                                .arg(path, file.errorString()));
    }
    return OperationResult::ok({}, path);
}

QString cropDigest(const QImage &image) {
    const QByteArrayView pixels(reinterpret_cast<const char *>(image.constBits()),
                                image.sizeInBytes());
    return QString::fromLatin1(QCryptographicHash::hash(pixels, QCryptographicHash::Sha256)
                                       .toHex().left(16));
}

OperationResult expandNames(const QList<CropArtifact> &artifacts,
                            const QString &sourceName,
                            const QString &nameTemplate,
                            int revision,
                            QStringList *names) {
    if (nameTemplate.trimmed().isEmpty()) {
        return OperationResult::failure(OperationError::Arguments,
                                        QObject::tr("The filename template cannot be empty."));
    }
    if (nameTemplate.contains('/') || nameTemplate.contains('\\')) {
        return OperationResult::failure(OperationError::Arguments,
                                        QObject::tr("The filename template cannot contain path separators."));
    }

    static const QSet<QString> allowed{
            QStringLiteral("source"), QStringLiteral("screen"), QStringLiteral("number"),
            QStringLiteral("revision"), QStringLiteral("digest")};
    const QRegularExpression tokenExpression(QStringLiteral(R"(\{([^{}]+)\})"));
    auto matchIterator = tokenExpression.globalMatch(nameTemplate);
    while (matchIterator.hasNext()) {
        const QString token = matchIterator.next().captured(1);
        if (!allowed.contains(token)) {
            return OperationResult::failure(
                    OperationError::Arguments,
                    QObject::tr("Unknown filename field: {%1}").arg(token));
        }
    }
    QString withoutTokens = nameTemplate;
    withoutTokens.remove(tokenExpression);
    if (withoutTokens.contains('{') || withoutTokens.contains('}')) {
        return OperationResult::failure(OperationError::Arguments,
                                        QObject::tr("The filename template contains unmatched braces."));
    }

    const QString safeSource = OutputService::sanitizeFileComponent(
            QFileInfo(sourceName).completeBaseName().isEmpty()
                    ? QStringLiteral("wallpaper") : QFileInfo(sourceName).completeBaseName());
    const QString revisionSuffix = revision > 1
            ? QStringLiteral("-r%1").arg(revision) : QString();
    QSet<QString> uniqueNames;
    names->clear();
    for (const CropArtifact &artifact : artifacts) {
        QString name = nameTemplate;
        name.replace(QStringLiteral("{source}"), safeSource);
        name.replace(QStringLiteral("{screen}"), OutputService::sanitizeFileComponent(
                artifact.screen.name.isEmpty()
                        ? QObject::tr("screen-%1").arg(artifact.screen.number)
                        : artifact.screen.name));
        name.replace(QStringLiteral("{number}"), QString::number(artifact.screen.number));
        name.replace(QStringLiteral("{revision}"), revisionSuffix);
        name.replace(QStringLiteral("{digest}"), artifact.digest);
        if (revision > 1 && !nameTemplate.contains(QStringLiteral("{revision}"))) {
            name += revisionSuffix;
        }
        name = OutputService::sanitizeFileComponent(name);
        if (name.isEmpty()) {
            return OperationResult::failure(OperationError::Arguments,
                                            QObject::tr("The filename template produced an empty name."));
        }
        name += QStringLiteral(".png");
        if (uniqueNames.contains(name)) {
            return OperationResult::failure(
                    OperationError::Arguments,
                    QObject::tr("The filename template produces the duplicate name %1.").arg(name));
        }
        uniqueNames.insert(name);
        names->append(name);
    }
    return OperationResult::ok();
}

bool anyExists(const QString &directory, const QStringList &names) {
    const QDir dir(directory);
    return std::any_of(names.cbegin(), names.cend(), [&](const QString &name) {
        return QFileInfo::exists(dir.filePath(name));
    });
}

OperationResult exportArtifacts(const QList<CropArtifact> &artifacts,
                                const QString &sourceName,
                                const ExportOptions &options) {
    if (options.directory.isEmpty()) {
        return OperationResult::failure(OperationError::Arguments,
                                        QObject::tr("No export directory was selected."));
    }
    QStringList names;
    OperationResult result = expandNames(artifacts, sourceName, options.fileNameTemplate, 0, &names);
    if (!result.success) return result;
    if (!QDir().mkpath(options.directory)) {
        return OperationResult::failure(OperationError::FileSystem,
                                        QObject::tr("Could not create export directory %1.")
                                                .arg(options.directory));
    }
    const bool collision = anyExists(options.directory, names);
    if (collision && (options.collisionPolicy == CollisionPolicy::Ask
                      || options.collisionPolicy == CollisionPolicy::Fail)) {
        return OperationResult::failure(OperationError::Collision,
                                        QObject::tr("One or more export files already exist."));
    }
    if (collision && options.collisionPolicy == CollisionPolicy::Revision) {
        int revision = 2;
        do {
            result = expandNames(artifacts, sourceName, options.fileNameTemplate, revision++, &names);
            if (!result.success) return result;
        } while (anyExists(options.directory, names));
    }

    QStringList paths;
    const QDir directory(options.directory);
    for (int index = 0; index < artifacts.size(); ++index) {
        const QString path = directory.filePath(names.at(index));
        result = writeImage(artifacts.at(index).image, path);
        if (!result.success) return OperationResult::failure(result.error, result.message, paths);
        paths.append(path);
    }
    return OperationResult::ok(paths);
}

QJsonObject rectangleJson(const QRect &rectangle) {
    return {{QStringLiteral("x"), rectangle.x()},
            {QStringLiteral("y"), rectangle.y()},
            {QStringLiteral("width"), rectangle.width()},
            {QStringLiteral("height"), rectangle.height()}};
}

QJsonObject sizeJson(const QSize &size) {
    return {{QStringLiteral("width"), size.width()},
            {QStringLiteral("height"), size.height()}};
}

QJsonObject managedManifest(const QString &setId, const QString &createdAt,
                            const QString &status, const QString &error,
                            const QString &sourceName, const QString &sourcePath,
                            const QSize &originalSize, const QSize &renderedSize,
                            const QList<CropArtifact> &artifacts,
                            const QStringList &relativePaths) {
    QJsonArray crops;
    for (int index = 0; index < artifacts.size(); ++index) {
        const CropArtifact &artifact = artifacts.at(index);
        crops.append(QJsonObject{
                {QStringLiteral("screenName"), artifact.screen.name},
                {QStringLiteral("screenNumber"), artifact.screen.number},
                {QStringLiteral("desktopGeometry"), rectangleJson(artifact.screen.desktopGeometry)},
                {QStringLiteral("cropRectangle"), rectangleJson(artifact.screen.cropRect)},
                {QStringLiteral("digest"), artifact.digest},
                {QStringLiteral("path"), relativePaths.at(index)}});
    }
    QJsonObject manifest{
            {QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("id"), setId},
            {QStringLiteral("createdAt"), createdAt},
            {QStringLiteral("status"), status},
            {QStringLiteral("sourceName"), sourceName},
            {QStringLiteral("originalSize"), sizeJson(originalSize)},
            {QStringLiteral("renderedSize"), sizeJson(renderedSize)},
            {QStringLiteral("crops"), crops}};
    if (!sourcePath.isEmpty()) manifest.insert(QStringLiteral("sourcePath"), sourcePath);
    if (!error.isEmpty()) manifest.insert(QStringLiteral("error"), error);
    return manifest;
}

QString managedSetId(const QSize &originalSize, const QSize &renderedSize,
                     const QList<CropArtifact> &artifacts) {
    QJsonArray crops;
    for (const CropArtifact &artifact : artifacts) {
        crops.append(QJsonObject{
                {QStringLiteral("screenNumber"), artifact.screen.number},
                {QStringLiteral("desktopGeometry"), rectangleJson(artifact.screen.desktopGeometry)},
                {QStringLiteral("cropRectangle"), rectangleJson(artifact.screen.cropRect)},
                {QStringLiteral("digest"), artifact.digest}});
    }
    const QJsonObject identity{
            {QStringLiteral("originalSize"), sizeJson(originalSize)},
            {QStringLiteral("renderedSize"), sizeJson(renderedSize)},
            {QStringLiteral("crops"), crops}};
    const QByteArray digest = QCryptographicHash::hash(
            QJsonDocument(identity).toJson(QJsonDocument::Compact),
            QCryptographicHash::Sha256).toHex();
    return QStringLiteral("set-") + QString::fromLatin1(digest.left(32));
}
}

OperationResult OperationResult::ok(QStringList paths, const QString &manifestPath) {
    return {true, OperationError::None, {}, std::move(paths), manifestPath};
}

OperationResult OperationResult::failure(OperationError error, const QString &message,
                                         QStringList paths, const QString &manifestPath) {
    return {false, error, message, std::move(paths), manifestPath};
}

OperationResult OutputService::createCrops(const QImage &image,
                                           const QList<ScreenCrop> &screens,
                                           QList<CropArtifact> *artifacts) {
    if (artifacts == nullptr) {
        return OperationResult::failure(OperationError::Arguments,
                                        QObject::tr("No crop output was provided."));
    }
    artifacts->clear();
    if (screens.isEmpty()) {
        return OperationResult::failure(OperationError::ImageLayout,
                                        QObject::tr("No screens are available to crop."));
    }
    if (image.isNull()) {
        return OperationResult::failure(OperationError::ImageLayout,
                                        QObject::tr("The source image could not be loaded."));
    }

    QRect cropBounds;
    for (const ScreenCrop &screen : screens) {
        if (!screen.cropRect.isValid()) {
            return OperationResult::failure(OperationError::ImageLayout,
                                            QObject::tr("Screen %1 has an invalid crop rectangle.")
                                                    .arg(screen.number));
        }
        cropBounds = cropBounds.isNull() ? screen.cropRect : cropBounds.united(screen.cropRect);
    }
    if (cropBounds.width() > image.width() || cropBounds.height() > image.height()) {
        return OperationResult::failure(OperationError::ImageLayout,
                                        QObject::tr("The combined screen layout is larger than the image."));
    }

    QPoint correction;
    if (cropBounds.left() < image.rect().left()) correction.setX(image.rect().left() - cropBounds.left());
    else if (cropBounds.right() > image.rect().right()) correction.setX(image.rect().right() - cropBounds.right());
    if (cropBounds.top() < image.rect().top()) correction.setY(image.rect().top() - cropBounds.top());
    else if (cropBounds.bottom() > image.rect().bottom()) correction.setY(image.rect().bottom() - cropBounds.bottom());

    for (ScreenCrop screen : screens) {
        screen.cropRect.translate(correction);
        if (!image.rect().contains(screen.cropRect)) {
            return OperationResult::failure(OperationError::ImageLayout,
                                            QObject::tr("The crop for screen %1 is outside the image.")
                                                    .arg(screen.number));
        }
        QImage crop = image.copy(screen.cropRect);
        artifacts->append({screen, crop, cropDigest(crop)});
    }
    return OperationResult::ok();
}

OperationResult OutputService::exportCrops(const QImage &image,
                                           const QList<ScreenCrop> &screens,
                                           const QString &sourceName,
                                           const ExportOptions &options) {
    QList<CropArtifact> artifacts;
    OperationResult result = createCrops(image, screens, &artifacts);
    if (!result.success) return result;
    return exportArtifacts(artifacts, sourceName, options);
}

OperationResult OutputService::exportCrops(const QStringList &cropPaths,
                                           const QList<ScreenCrop> &screens,
                                           const QString &sourceName,
                                           const ExportOptions &options) {
    if (cropPaths.isEmpty() || cropPaths.size() != screens.size()) {
        return OperationResult::failure(OperationError::ImageLayout,
                                        QObject::tr("The saved set has an incomplete crop mapping."));
    }
    QList<CropArtifact> artifacts;
    for (int index = 0; index < cropPaths.size(); ++index) {
        const QImage crop(cropPaths.at(index));
        if (crop.isNull() || crop.size() != screens.at(index).cropRect.size()) {
            return OperationResult::failure(
                    OperationError::FileSystem,
                    QObject::tr("A generated image is missing, unreadable, or has changed dimensions."));
        }
        artifacts.append({screens.at(index), crop, cropDigest(crop)});
    }
    return exportArtifacts(artifacts, sourceName, options);
}

OperationResult OutputService::applyManaged(const QImage &image,
                                            const QSize &originalSize,
                                            const QList<ScreenCrop> &screens,
                                            const QString &sourceName,
                                            const QString &sourcePath,
                                            PlasmaApplicator &applicator,
                                            const QString &managedRoot) {
    QList<CropArtifact> artifacts;
    OperationResult result = createCrops(image, screens, &artifacts);
    if (!result.success) return result;

    const QString setId = managedSetId(originalSize, image.size(), artifacts);
    const QString setsRoot = managedRoot.isEmpty()
            ? QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                      .filePath(QStringLiteral("wallpaper-splitter/sets"))
            : managedRoot;
    const QString setDirectory = QDir(setsRoot).filePath(setId);
    if (!QDir().mkpath(setDirectory)) {
        return OperationResult::failure(OperationError::FileSystem,
                                        QObject::tr("Could not create managed set directory %1.")
                                                .arg(setDirectory));
    }

    QStringList paths;
    QStringList relativePaths;
    for (const CropArtifact &artifact : artifacts) {
        const QString relative = QStringLiteral("screen-%1-%2.png")
                .arg(artifact.screen.number).arg(artifact.digest);
        const QString path = QDir(setDirectory).filePath(relative);
        result = writeImage(artifact.image, path);
        if (!result.success) return OperationResult::failure(result.error, result.message, paths);
        relativePaths.append(relative);
        paths.append(path);
    }

    const QString manifestPath = QDir(setDirectory).filePath(QStringLiteral("manifest.json"));
    QJsonObject previous;
    QFile previousFile(manifestPath);
    if (previousFile.open(QIODevice::ReadOnly))
        previous = QJsonDocument::fromJson(previousFile.readAll()).object();
    const QString createdAt = previous.value("createdAt").toString(
            QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    auto manifest = managedManifest(setId, createdAt, QStringLiteral("prepared"), {},
                                    sourceName, sourcePath, originalSize, image.size(),
                                    artifacts, relativePaths);
    if (previous.contains("name")) manifest.insert("name", previous.value("name"));
    result = writeJson(manifest, manifestPath);
    if (!result.success) return OperationResult::failure(result.error, result.message, paths, manifestPath);

    QList<ScreenCrop> correctedScreens;
    for (const CropArtifact &artifact : artifacts) correctedScreens.append(artifact.screen);
    OperationResult applyResult = applicator.apply(correctedScreens, paths);
    const QString status = applyResult.success ? QStringLiteral("applied") : QStringLiteral("failed");
    manifest.insert("status", status);
    manifest.insert("lastAppliedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!applyResult.success) manifest.insert("error", applyResult.message);
    const OperationResult manifestResult = writeJson(manifest, manifestPath);
    if (!manifestResult.success) {
        return OperationResult::failure(manifestResult.error, manifestResult.message, paths, manifestPath);
    }
    if (!applyResult.success) {
        return OperationResult::failure(OperationError::Plasma, applyResult.message, paths, manifestPath);
    }
    return OperationResult::ok(paths, manifestPath);
}

QString OutputService::sanitizeFileComponent(const QString &value) {
    QString sanitized = value.trimmed();
    sanitized.replace(QRegularExpression(QStringLiteral(R"([\x00-\x1f<>:"/\\|?*])")),
                      QStringLiteral("_"));
    sanitized.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral(" "));
    while (sanitized.endsWith('.') || sanitized.endsWith(' ')) sanitized.chop(1);
    return sanitized;
}

OperationResult OutputService::validateFileNameTemplate(const QString &fileNameTemplate) {
    QList<CropArtifact> artifacts{
            {{QStringLiteral("DP-1"), 1, QRect(0, 0, 1, 1), QRect(0, 0, 1, 1)},
             QImage(1, 1, QImage::Format_RGB32), QStringLiteral("0123456789abcdef")},
            {{QStringLiteral("HDMI-1"), 2, QRect(1, 0, 1, 1), QRect(1, 0, 1, 1)},
             QImage(1, 1, QImage::Format_RGB32), QStringLiteral("fedcba9876543210")}};
    QStringList names;
    return expandNames(artifacts, QStringLiteral("wallpaper.jpg"), fileNameTemplate, 0, &names);
}

QString OutputService::collisionPolicyName(CollisionPolicy policy) {
    switch (policy) {
        case CollisionPolicy::Ask: return QStringLiteral("ask");
        case CollisionPolicy::Fail: return QStringLiteral("fail");
        case CollisionPolicy::Replace: return QStringLiteral("replace");
        case CollisionPolicy::Revision: return QStringLiteral("revision");
    }
    return QStringLiteral("ask");
}

bool OutputService::parseCollisionPolicy(const QString &value, CollisionPolicy *policy) {
    if (policy == nullptr) return false;
    const QString normalized = value.toLower();
    if (normalized == QStringLiteral("ask")) *policy = CollisionPolicy::Ask;
    else if (normalized == QStringLiteral("fail")) *policy = CollisionPolicy::Fail;
    else if (normalized == QStringLiteral("replace")) *policy = CollisionPolicy::Replace;
    else if (normalized == QStringLiteral("revision")) *policy = CollisionPolicy::Revision;
    else return false;
    return true;
}

QString DBusPlasmaApplicator::buildScript(const QList<ScreenCrop> &screens,
                                          const QStringList &paths) {
    QJsonArray crops;
    for (int index = 0; index < screens.size(); ++index) {
        const QRect geometry = screens.at(index).desktopGeometry;
        crops.append(QJsonObject{{QStringLiteral("path"), paths.value(index)},
                                 {QStringLiteral("x"), geometry.x()},
                                 {QStringLiteral("y"), geometry.y()},
                                 {QStringLiteral("width"), geometry.width()},
                                 {QStringLiteral("height"), geometry.height()}});
    }
    const QString cropArray = QString::fromUtf8(QJsonDocument(crops).toJson(QJsonDocument::Compact));
    return QStringLiteral(R"(
const crops = %1;
let applied = 0;
let unmatchedDesktops = 0;
function sameGeometry(crop, geometry) {
    return crop.x === geometry.x && crop.y === geometry.y
        && crop.width === geometry.width && crop.height === geometry.height;
}
const targets = [];
for (const desktop of desktopsForActivity(currentActivity())) {
    if (desktop.screen < 0) continue;
    const geometry = screenGeometry(desktop.screen);
    const crop = crops.find(candidate => sameGeometry(candidate, geometry));
    if (crop === undefined) {
        unmatchedDesktops++;
        continue;
    }
    targets.push({desktop, crop});
}
// Validate the entire activity before changing any desktop.
if (unmatchedDesktops === 0 && targets.length === crops.length
    && new Set(targets.map(target => target.crop)).size === crops.length) {
  for (const {desktop, crop} of targets) {
    desktop.wallpaperPlugin = 'org.kde.image';
    desktop.currentConfigGroup = ['Wallpaper', 'org.kde.image', 'General'];
    desktop.writeConfig('Image', crop.path);
    applied++;
  }
}
print('WALLPAPER_SPLITTER_RESULT ' + JSON.stringify({applied, expected: crops.length,
                                                     unmatchedDesktops}));
)").arg(cropArray);
}

OperationResult DBusPlasmaApplicator::apply(const QList<ScreenCrop> &screens,
                                            const QStringList &paths) {
    if (screens.size() != paths.size() || screens.isEmpty()) {
        return OperationResult::failure(OperationError::Arguments,
                                        QObject::tr("The screen and crop counts do not match."));
    }
    QDBusMessage message = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.plasmashell"), QStringLiteral("/PlasmaShell"),
            QStringLiteral("org.kde.PlasmaShell"), QStringLiteral("evaluateScript"));
    message.setArguments({buildScript(screens, paths)});
    const QDBusMessage reply = QDBusConnection::sessionBus().call(message);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        return OperationResult::failure(OperationError::Plasma,
                                        QObject::tr("Plasma could not apply the wallpaper: %1")
                                                .arg(reply.errorMessage()));
    }
    const QString output = reply.arguments().isEmpty()
            ? QString() : reply.arguments().constFirst().toString();
    const QRegularExpression resultExpression(
            QStringLiteral(R"(WALLPAPER_SPLITTER_RESULT (\{[^\r\n]+\}))"));
    const QRegularExpressionMatch match = resultExpression.match(output);
    if (!match.hasMatch()) {
        return OperationResult::failure(
                OperationError::Plasma,
                QObject::tr("Plasma did not confirm that the wallpaper was applied."));
    }
    const QJsonObject summary = QJsonDocument::fromJson(match.captured(1).toUtf8()).object();
    const int applied = summary.value(QStringLiteral("applied")).toInt(-1);
    const int expected = summary.value(QStringLiteral("expected")).toInt(-1);
    const int unmatched = summary.value(QStringLiteral("unmatchedDesktops")).toInt(-1);
    if (applied != expected || unmatched != 0) {
        return OperationResult::failure(
                OperationError::Plasma,
                QObject::tr("Plasma applied %1 of %2 crops; %3 desktop(s) had no matching geometry.")
                        .arg(applied).arg(expected).arg(unmatched));
    }
    return OperationResult::ok(paths);
}

OperationResult DBusPlasmaApplicator::wallpaperReferences() {
    // Read live containments first; persisted config also covers stopped activities
    // and disconnected monitors. Keep references even for inactive plugins.
    QDBusMessage message = QDBusMessage::createMethodCall(
            QStringLiteral("org.kde.plasmashell"), QStringLiteral("/PlasmaShell"),
            QStringLiteral("org.kde.PlasmaShell"), QStringLiteral("evaluateScript"));
    message.setArguments({QStringLiteral(R"(
const references = [];
for (const desktop of desktops()) {
    desktop.currentConfigGroup = ['Wallpaper', 'org.kde.image', 'General'];
    references.push(desktop.readConfig('Image', ''));
    desktop.currentConfigGroup = ['Wallpaper', 'org.kde.slideshow', 'General'];
    references.push(desktop.readConfig('Image', ''));
    for (const path of desktop.readConfig('SlidePaths', [])) references.push(path);
}
print('WALLPAPER_SPLITTER_REFERENCES ' + JSON.stringify(references));
)")});
    const auto reply = QDBusConnection::sessionBus().call(message);
    const QString output = reply.arguments().isEmpty() ? QString() : reply.arguments().first().toString();
    const auto match = QRegularExpression(
            QStringLiteral(R"(WALLPAPER_SPLITTER_REFERENCES (\[[^\r\n]*\]))")).match(output);
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(match.captured(1).toUtf8(), &parseError);
    if (reply.type() == QDBusMessage::ErrorMessage || !match.hasMatch()
        || parseError.error != QJsonParseError::NoError || !document.isArray())
        return OperationResult::failure(OperationError::Plasma,
            QObject::tr("Could not verify Plasma wallpaper references. The set was not deleted."));
    QStringList paths;
    for (const auto &value : document.array()) {
        if (!value.isString())
            return OperationResult::failure(OperationError::Plasma,
                    QObject::tr("Plasma returned an unknown wallpaper reference."));
        if (!value.toString().isEmpty()) paths.append(value.toString());
    }
    const QString configPath = QStandardPaths::locate(QStandardPaths::GenericConfigLocation,
                                                      "plasma-org.kde.plasma.desktop-appletsrc");
    QFile configFile(configPath);
    if (configPath.isEmpty() || !configFile.open(QIODevice::ReadOnly))
        return OperationResult::failure(OperationError::Plasma,
                QObject::tr("Could not read saved Plasma wallpaper references. The set was not deleted."));
    KConfig config(configPath, KConfig::SimpleConfig);
    const auto collect = [&paths](const auto &self, const KConfigGroup &group) -> void {
        for (const auto &key : group.keyList()) {
            if (key == "Image") paths.append(group.readPathEntry(key, QString()));
            if (key == "SlidePaths") paths.append(group.readPathEntry(key, QStringList()));
        }
        for (const auto &name : group.groupList()) self(self, group.group(name));
    };
    for (const auto &name : config.groupList()) collect(collect, config.group(name));
    return OperationResult::ok(paths);
}
