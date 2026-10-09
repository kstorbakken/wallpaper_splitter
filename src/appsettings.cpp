#include "appsettings.h"

#include <QSettings>
#include <QStandardPaths>

namespace {
constexpr auto settingsOrganization = "wallpaper-splitter";
constexpr auto settingsApplication = "settings";

QString picturesDirectory() {
    const QString location = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    return location.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation) : location;
}

QSettings userSettings() {
    return QSettings(QSettings::NativeFormat, QSettings::UserScope,
                     QString::fromLatin1(settingsOrganization),
                     QString::fromLatin1(settingsApplication));
}

void migrateLegacySettings(QSettings &settings) {
    const QString migrationKey = QStringLiteral("meta/legacySettingsChecked");
    if (settings.value(migrationKey, false).toBool()) return;

    QSettings legacy(QSettings::NativeFormat, QSettings::UserScope,
                     QStringLiteral("kstorbakken"), QStringLiteral("Wallpaper Splitter"));
    const QStringList keys{
            QStringLiteral("folders/input"),
            QStringLiteral("folders/export"),
            QStringLiteral("output/fileNameTemplate"),
            QStringLiteral("output/collisionPolicy"),
            QStringLiteral("output/format"),
            QStringLiteral("output/jpegQuality"),
    };
    for (const QString &key : keys) {
        if (!settings.contains(key) && legacy.contains(key)) {
            settings.setValue(key, legacy.value(key));
        }
    }
    settings.setValue(migrationKey, true);
}
}

UserPreferences AppSettings::load() {
    QSettings settings = userSettings();
    migrateLegacySettings(settings);
    UserPreferences preferences;
    preferences.closeAfterApply = settings.value(QStringLiteral("behavior/closeAfterApply"), true).toBool();
    preferences.inputDirectory = settings.value(QStringLiteral("folders/input"), picturesDirectory()).toString();
    preferences.exportDirectory = settings.value(QStringLiteral("folders/export"), picturesDirectory()).toString();
    preferences.fileNameTemplate = settings.value(QStringLiteral("output/fileNameTemplate"),
                                                  QStringLiteral("{source}-{number}")).toString();
    CollisionPolicy policy;
    if (OutputService::parseCollisionPolicy(
                settings.value(QStringLiteral("output/collisionPolicy"), QStringLiteral("ask")).toString(),
                &policy)) {
        preferences.collisionPolicy = policy;
    }
    OutputFormat format;
    if (OutputService::parseOutputFormat(
                settings.value(QStringLiteral("output/format"), QStringLiteral("automatic")).toString(),
                &format)) {
        preferences.outputFormat = format;
    }
    preferences.jpegQuality = qBound(1, settings.value(
            QStringLiteral("output/jpegQuality"), 90).toInt(), 100);
    return preferences;
}

void AppSettings::save(const UserPreferences &preferences) {
    QSettings settings = userSettings();
    migrateLegacySettings(settings);
    settings.setValue(QStringLiteral("behavior/closeAfterApply"), preferences.closeAfterApply);
    settings.setValue(QStringLiteral("folders/input"), preferences.inputDirectory);
    settings.setValue(QStringLiteral("folders/export"), preferences.exportDirectory);
    settings.setValue(QStringLiteral("output/fileNameTemplate"), preferences.fileNameTemplate);
    settings.setValue(QStringLiteral("output/collisionPolicy"),
                      OutputService::collisionPolicyName(preferences.collisionPolicy));
    settings.setValue(QStringLiteral("output/format"),
                      OutputService::outputFormatName(preferences.outputFormat));
    settings.setValue(QStringLiteral("output/jpegQuality"), preferences.jpegQuality);
}

void AppSettings::reset() {
    QSettings settings = userSettings();
    migrateLegacySettings(settings);
    settings.remove(QStringLiteral("folders"));
    settings.remove(QStringLiteral("output"));
    settings.remove(QStringLiteral("behavior"));
}

MonitorPreferences AppSettings::loadMonitors() {
    QSettings settings = userSettings();
    MonitorPreferences preferences;
    settings.beginGroup("monitors");
    preferences.enabled = settings.value("physicalSizing", false).toBool();
    for (const auto &id : settings.childGroups()) {
        settings.beginGroup(id);
        const QSizeF size = settings.value("millimeters").toSizeF();
        const QPointF position = settings.value("position").toPointF();
        if (MonitorLayout::validSize(size))
            preferences.measurements.insert(id, {size, position, settings.value("portrait", false).toBool()});
        settings.endGroup();
    }
    return preferences;
}

void AppSettings::saveMonitors(const MonitorPreferences &preferences) {
    QSettings settings = userSettings();
    settings.beginGroup("monitors");
    settings.setValue("physicalSizing", preferences.enabled);
    for (auto it = preferences.measurements.cbegin(); it != preferences.measurements.cend(); ++it) {
        settings.beginGroup(it.key());
        settings.setValue("millimeters", it->millimeters);
        settings.setValue("position", it->position);
        settings.setValue("portrait", it->portrait);
        settings.endGroup();
    }
}
