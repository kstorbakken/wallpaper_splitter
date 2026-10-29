#ifndef WALLPAPER_SPLITTER_SLIDESHOW_H
#define WALLPAPER_SPLITTER_SLIDESHOW_H

#include "setlibrary.h"
#include <functional>

struct SlideshowSettings {
    bool enabled{false};
    int intervalMinutes{30};
    bool shuffle{false};
    QStringList setIds;
    QString lastSetId;
    QString lastAttempt;
    QString lastError;
};

class SlideshowScheduler {
public:
    virtual ~SlideshowScheduler() = default;
    virtual OperationResult enable(int intervalMinutes) = 0;
    virtual OperationResult disable() = 0;
    virtual OperationResult status() = 0;
};

class SystemdSlideshowScheduler final : public SlideshowScheduler {
public:
    using Runner = std::function<OperationResult(const QStringList &)>;
    explicit SystemdSlideshowScheduler(const QString &unitDirectory = {},
                                       const QString &executable = {}, Runner runner = {});
    OperationResult enable(int intervalMinutes) override;
    OperationResult disable() override;
    OperationResult status() override;
    static QString serviceContents(const QString &executable, bool extractAppImage = false);
    static QString timerContents(int intervalMinutes);
    static QString launchExecutable();
private:
    QString directory;
    QString executable;
    bool extractAppImage;
    Runner run;
};

class Slideshow {
public:
    explicit Slideshow(const QString &configFile = {}, const QString &managedRoot = {});
    OperationResult load(SlideshowSettings *settings) const;
    // Saves the playlist and updates its timer together. A scheduling failure
    // leaves automatic rotation disabled, with the playlist available for retry.
    OperationResult configure(const SlideshowSettings &settings, SlideshowScheduler &scheduler) const;
    OperationResult removeSets(const QStringList &setIds, SlideshowScheduler &scheduler) const;
    OperationResult next(const QList<QRect> &layout, PlasmaApplicator &applicator,
                         bool automatic = false) const;
private:
    QString fileName;
    SetLibrary library;
    OperationResult save(const SlideshowSettings &settings) const;
};

#endif
