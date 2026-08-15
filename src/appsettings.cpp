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
    return preferences;
}

void AppSettings::save(const UserPreferences &preferences) {
    QSettings settings = userSettings();
    migrateLegacySettings(settings);
    settings.setValue(QStringLiteral("folders/input"), preferences.inputDirectory);
    settings.setValue(QStringLiteral("folders/export"), preferences.exportDirectory);
    settings.setValue(QStringLiteral("output/fileNameTemplate"), preferences.fileNameTemplate);
    settings.setValue(QStringLiteral("output/collisionPolicy"),
                      OutputService::collisionPolicyName(preferences.collisionPolicy));
}

void AppSettings::reset() {
    QSettings settings = userSettings();
    migrateLegacySettings(settings);
    settings.remove(QStringLiteral("folders"));
    settings.remove(QStringLiteral("output"));
}
