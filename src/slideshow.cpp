#include "slideshow.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>
#include <utility>

namespace {
const QString timerName = QStringLiteral("wallpaper-splitter-slideshow.timer");
const QString serviceName = QStringLiteral("wallpaper-splitter-slideshow.service");

OperationResult failure(const QString &message) {
    return OperationResult::failure(OperationError::FileSystem, message);
}

OperationResult writeFile(const QString &path, const QByteArray &data) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return failure(QObject::tr("Could not create the slideshow settings directory."));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        return failure(QObject::tr("Could not save %1: %2").arg(path, file.errorString()));
    return OperationResult::ok();
}

OperationResult systemctl(const QStringList &arguments) {
    const QString program = QStandardPaths::findExecutable("systemctl");
    if (program.isEmpty())
        return failure(QObject::tr("Automatic slideshows require systemd and a running user session."));
    QProcess process;
    process.start(program, QStringList{"--user"} + arguments);
    if (!process.waitForStarted(3000)) return failure(process.errorString());
    if (!process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished(1000);
        return failure(QObject::tr("The slideshow scheduler did not respond in time."));
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString detail = QString::fromUtf8(process.readAllStandardError()).trimmed();
        const QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        return failure(QObject::tr("Slideshow scheduler: %1")
                           .arg(detail.isEmpty() ? output : detail));
    }
    auto result = OperationResult::ok();
    result.message = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    return result;
}

bool validSettings(const SlideshowSettings &settings) {
    if (settings.intervalMinutes < 1 || settings.intervalMinutes > 10080
        || settings.setIds.size() > 10000) return false;
    const QRegularExpression idPattern("^set-[a-f0-9]{32}$");
    QSet<QString> seen;
    for (const auto &id : settings.setIds) {
        if (!idPattern.match(id).hasMatch() || seen.contains(id)) return false;
        seen.insert(id);
    }
    return !settings.enabled || !settings.setIds.isEmpty();
}
}

SystemdSlideshowScheduler::SystemdSlideshowScheduler(const QString &unitDirectory,
                                                     const QString &command, Runner runner)
    : directory(unitDirectory.isEmpty()
          ? QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
                .filePath("systemd/user") : unitDirectory),
      executable(command.isEmpty() ? launchExecutable() : command),
      extractAppImage(!qEnvironmentVariable("APPIMAGE").isEmpty()
                     && qEnvironmentVariable("APPIMAGE_EXTRACT_AND_RUN") == "1"),
      run(runner ? std::move(runner) : systemctl) {}

QString SystemdSlideshowScheduler::launchExecutable() {
    // AppImage's mounted internal binary disappears when the process exits.
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    return appImage.isEmpty() ? QCoreApplication::applicationFilePath()
                             : QFileInfo(appImage).absoluteFilePath();
}

QString SystemdSlideshowScheduler::serviceContents(const QString &executable, bool extractAppImage) {
    QString escaped = executable;
    escaped.replace('\\', "\\\\");
    escaped.replace('"', "\\\"");
    escaped.replace('%', "%%");
    escaped.replace('$', "$$");
    escaped.replace('\n', "\\n");
    escaped.replace('\r', "\\r");
    escaped.replace('\t', "\\t");
    // systemd forbids some characters in argv[0], even when quoted. env accepts
    // the application path as an ordinary argument and execs it without a shell.
    return QStringLiteral(
        "[Unit]\nDescription=Rotate saved Wallpaper Splitter sets\n"
        "Requisite=graphical-session.target\nAfter=graphical-session.target\n"
        "PartOf=graphical-session.target\n\n"
        "[Service]\nType=oneshot\nExecStart=/usr/bin/env -- \"%1\" --slideshow-tick\n"
        "%2TimeoutStartSec=60\n").arg(escaped, extractAppImage
            ? QStringLiteral("Environment=APPIMAGE_EXTRACT_AND_RUN=1\n") : QString());
}

QString SystemdSlideshowScheduler::timerContents(int intervalMinutes) {
    // Timers are ordered before timers.target by default. Ordering this timer
    // after graphical-session.target would create a cycle through basic.target.
    // The service checks session readiness; PartOf stops the timer at logout.
    return QStringLiteral(
        "[Unit]\nDescription=Wallpaper Splitter slideshow interval\n"
        "PartOf=graphical-session.target\n\n"
        "[Timer]\nOnActiveSec=%1min\nOnUnitInactiveSec=%1min\n"
        "AccuracySec=1s\nWakeSystem=false\nPersistent=false\n\n"
        "[Install]\nWantedBy=graphical-session.target\n").arg(intervalMinutes);
}

OperationResult SystemdSlideshowScheduler::enable(int intervalMinutes) {
    if (intervalMinutes < 1 || intervalMinutes > 10080
        || !QFileInfo(executable).isAbsolute() || !QFileInfo(executable).isFile()
        || !QFileInfo(executable).isExecutable())
        return failure(QObject::tr("Choose a valid interval and an executable application location."));
    auto result = run({"is-active", "graphical-session.target"});
    if (!result.success)
        return failure(QObject::tr("Automatic rotation requires an active graphical systemd user session.\n%1")
                           .arg(result.message));
    result = writeFile(QDir(directory).filePath(serviceName),
                            serviceContents(executable, extractAppImage).toUtf8());
    if (!result.success) return result;
    result = writeFile(QDir(directory).filePath(timerName), timerContents(intervalMinutes).toUtf8());
    if (!result.success) return result;
    result = run({"daemon-reload"});
    if (!result.success) return result;
    result = run({"enable", timerName});
    if (!result.success) return result;
    return run({"restart", timerName});
}

OperationResult SystemdSlideshowScheduler::disable() {
    const QString timerPath = QDir(directory).absoluteFilePath(timerName);
    const QString linkPath = QDir(directory).filePath("graphical-session.target.wants/" + timerName);
    const QFileInfo link(linkPath);
    const bool hasLink = link.exists() || link.isSymLink();
    const bool hasTimer = QFileInfo::exists(timerPath);
    // A never-enabled slideshow can be saved or used manually without systemd.
    if (!hasTimer && !hasLink && !QFileInfo::exists(QDir(directory).filePath(serviceName)))
        return OperationResult::ok();
    // systemctl disable scans unrelated startup links and can fail if any of
    // their targets are inaccessible. Remove only the link our [Install]
    // section creates, preserving other units and any unexpected replacement.
    if (hasLink) {
        if (!link.isSymLink() || QDir::cleanPath(link.symLinkTarget()) != QDir::cleanPath(timerPath))
            return failure(QObject::tr("The slideshow startup path contains an unexpected file or link; "
                                       "it was left unchanged: %1").arg(linkPath));
        QFile startupLink(linkPath);
        if (!startupLink.remove())
            return failure(QObject::tr("Could not remove the slideshow startup link %1: %2")
                               .arg(linkPath, startupLink.errorString()));
    }
    const auto reloaded = run({"daemon-reload"});
    if (!reloaded.success) return reloaded;
    bool needsStop = true;
    if (!hasTimer) {
        // Handle externally removed unit files without mistaking a cached,
        // still-active timer for a never-installed slideshow.
        const auto state = run({"show", "--property=LoadState", "--property=ActiveState", timerName});
        if (!state.success) return state;
        const auto properties = state.message.split('\n');
        needsStop = !(properties.contains("LoadState=not-found")
                      && properties.contains("ActiveState=inactive"));
    }
    if (needsStop) {
        const auto stopped = run({"stop", timerName});
        if (!stopped.success) return stopped;
    }
    // Keep the definitions until the timer is stopped so a failed stop can be
    // retried. Only unlink our own files; never follow symlinks or remove shared
    // systemd directories. Reload even after a partial cleanup.
    auto removed = OperationResult::ok();
    for (const auto &name : {timerName, serviceName}) {
        QFile file(QDir(directory).filePath(name));
        const QFileInfo info(file);
        if ((info.exists() || info.isSymLink()) && !file.remove()) {
            removed = failure(QObject::tr("Could not remove the slideshow unit %1: %2")
                                  .arg(file.fileName(), file.errorString()));
            break;
        }
    }
    const auto cleaned = run({"daemon-reload"});
    return removed.success ? cleaned : removed;
}

OperationResult SystemdSlideshowScheduler::status() {
    auto result = run({"is-active", timerName});
    if (!result.success) return result;
    result = run({"show", "--property=Result", "--value", serviceName});
    if (!result.success) return result;
    if (!result.message.isEmpty() && result.message != "success")
        return failure(QObject::tr("The last scheduled switch failed (%1). Check the service journal; "
                                  "save again if the application has moved.").arg(result.message));
    return OperationResult::ok();
}

Slideshow::Slideshow(const QString &configFile, const QString &managedRoot)
    : fileName(configFile.isEmpty()
          ? QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
                .filePath("wallpaper-splitter/slideshow.json") : configFile),
      library(managedRoot) {}

OperationResult Slideshow::load(SlideshowSettings *settings) const {
    *settings = {};
    QFile file(fileName);
    if (!file.exists()) return OperationResult::ok();
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        return failure(QObject::tr("Could not read slideshow settings: %1").arg(file.errorString()));
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    const auto object = document.object();
    if (error.error != QJsonParseError::NoError || object.value("schemaVersion").toInt() != 1
        || !object.value("setIds").isArray() || !object.value("enabled").isBool()
        || !object.value("shuffle").isBool() || !object.value("intervalMinutes").isDouble())
        return failure(QObject::tr("The slideshow settings are invalid or use an unsupported version."));
    settings->enabled = object.value("enabled").toBool();
    settings->shuffle = object.value("shuffle").toBool();
    settings->intervalMinutes = object.value("intervalMinutes").toInt();
    for (const auto &value : object.value("setIds").toArray()) settings->setIds.append(value.toString());
    settings->lastSetId = object.value("lastSetId").toString();
    settings->lastAttempt = object.value("lastAttempt").toString();
    settings->lastError = object.value("lastError").toString();
    if (!validSettings(*settings))
        return failure(QObject::tr("The slideshow playlist or interval is invalid."));
    return OperationResult::ok();
}

OperationResult Slideshow::save(const SlideshowSettings &settings) const {
    const QJsonObject object{{"schemaVersion", 1}, {"enabled", settings.enabled},
        {"intervalMinutes", settings.intervalMinutes}, {"shuffle", settings.shuffle},
        {"setIds", QJsonArray::fromStringList(settings.setIds)}, {"lastSetId", settings.lastSetId},
        {"lastAttempt", settings.lastAttempt}, {"lastError", settings.lastError}};
    return writeFile(fileName, QJsonDocument(object).toJson());
}

OperationResult Slideshow::configure(const SlideshowSettings &settings, SlideshowScheduler &scheduler) const {
    if (!validSettings(settings))
        return failure(QObject::tr("Choose at least one set for automatic rotation and an interval from 1 to 10080 minutes."));
    if (!QDir().mkpath(QFileInfo(fileName).absolutePath()))
        return failure(QObject::tr("Could not create the slideshow settings directory."));
    QLockFile lock(fileName + ".lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock()) return failure(QObject::tr("A slideshow operation is in progress. Try again shortly."));
    SlideshowSettings previous;
    auto result = load(&previous);
    if (!result.success) return result;
    auto updated = settings;
    updated.lastSetId = previous.lastSetId;
    updated.lastAttempt = previous.lastAttempt;
    updated.lastError.clear();
    // Persist the disabled state first, so even a failed scheduler command cannot
    // leave an old timer advancing the playlist after the user turns it off.
    updated.enabled = false;
    result = save(updated);
    if (!result.success) return result;
    // Enabling already writes both units, reloads the manager, and restarts the
    // timer. A preliminary disable can fail on stale units and prevent repair.
    result = settings.enabled ? scheduler.enable(settings.intervalMinutes) : scheduler.disable();
    if (!result.success) {
        updated.lastError = QObject::tr("Automatic rotation is disabled. %1").arg(result.message);
        // Only an enable attempt needs rollback. Repeating a failed disable
        // immediately produces the same error and cannot improve the outcome.
        if (settings.enabled) {
            const auto cleanup = scheduler.disable();
            if (!cleanup.success && cleanup.message != result.message)
                updated.lastError += '\n' + cleanup.message;
        }
        const auto saved = save(updated);
        return failure(updated.lastError + (saved.success ? QString() : '\n' + saved.message));
    }
    updated.enabled = settings.enabled;
    result = save(updated);
    if (!result.success) scheduler.disable();
    return result;
}

OperationResult Slideshow::next(const QList<QRect> &layout, PlasmaApplicator &applicator,
                               bool automatic) const {
    if (!QDir().mkpath(QFileInfo(fileName).absolutePath()))
        return failure(QObject::tr("Could not create the slideshow settings directory."));
    QLockFile lock(fileName + ".lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock()) return failure(QObject::tr("A slideshow operation is in progress. Try again shortly."));
    SlideshowSettings settings;
    auto result = load(&settings);
    if (!result.success) return result;
    if (automatic && !settings.enabled) return OperationResult::ok();

    QStringList candidates = settings.setIds;
    const auto last = candidates.indexOf(settings.lastSetId);
    if (settings.shuffle) {
        candidates.removeAll(settings.lastSetId);
        std::shuffle(candidates.begin(), candidates.end(), *QRandomGenerator::global());
        // Only repeat the last set when no other usable set remains.
        if (last >= 0) candidates.append(settings.lastSetId);
    } else if (last >= 0) {
        std::rotate(candidates.begin(), candidates.begin() + last + 1, candidates.end());
    }
    result = failure(QObject::tr("No usable slideshow sets match the connected monitors. The wallpaper was left unchanged."));
    QStringList problems;
    for (const auto &id : candidates) {
        bool attempted = false;
        const auto applied = library.reapply(id, layout, applicator, &attempted);
        if (applied.success) {
            settings.lastSetId = id;
            result = applied;
            break;
        }
        // Skip invalid files/layouts, but stop after any actual Plasma operation
        // (including a manifest write failure) or concurrent library operation.
        if (attempted || applied.error == OperationError::Collision) {
            result = applied;
            break;
        }
        problems.append(applied.message);
    }
    settings.lastAttempt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    settings.lastError = result.success ? QString() : result.message;
    if (!result.success && !problems.isEmpty()) {
        problems.removeDuplicates();
        settings.lastError += '\n' + problems.join('\n');
        result.message = settings.lastError;
    }
    const auto saved = save(settings);
    return saved.success ? result : saved;
}
