#ifndef WALLPAPER_SPLITTER_APPSETTINGS_H
#define WALLPAPER_SPLITTER_APPSETTINGS_H

#include "outputservice.h"

struct UserPreferences {
    QString inputDirectory;
    QString exportDirectory;
    QString fileNameTemplate{QStringLiteral("{source}-{number}")};
    CollisionPolicy collisionPolicy{CollisionPolicy::Ask};
};

class AppSettings {
public:
    static UserPreferences load();
    static void save(const UserPreferences &preferences);
    static void reset();
};

#endif
