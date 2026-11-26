#include "slideshow.h"
#include "slideshowdialog.h"
#include "monitorlayout.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QHeaderView>
#include "wallpapersetlist.h"
#include <QLockFile>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

namespace {
class FakePlasma : public PlasmaApplicator {
public:
    bool succeeds{true};
    int calls{0};
    QStringList paths;
    std::function<void()> duringApply;
    OperationResult apply(const QList<ScreenCrop> &, const QStringList &value) override {
        ++calls;
        paths = value;
        if (duringApply) duringApply();
        return succeeds ? OperationResult::ok(value)
                        : OperationResult::failure(OperationError::Plasma, "Plasma unavailable");
    }
    OperationResult wallpaperReferences() override { return OperationResult::ok(); }
};

class FakeScheduler : public SlideshowScheduler {
public:
    bool active{false};
    bool failEnable{false};
    bool failDisable{false};
    int disableCalls{0};
    int interval{0};
    OperationResult enable(int minutes) override {
        interval = minutes;
        active = true; // Simulate even a partially successful enable.
        return failEnable ? OperationResult::failure(OperationError::FileSystem, "enable failed")
                          : OperationResult::ok();
    }
    OperationResult disable() override {
        ++disableCalls;
        if (failDisable) return OperationResult::failure(OperationError::FileSystem, "stop failed");
        active = false;
        return OperationResult::ok();
    }
    OperationResult status() override {
        return active ? OperationResult::ok()
                      : OperationResult::failure(OperationError::FileSystem, "inactive");
    }
};

struct Fixture {
    QTemporaryDir temp;
    QString config = temp.filePath("config/slideshow.json");
    QString root = temp.filePath("sets");
    Slideshow slideshow{config, root};
    FakePlasma plasma;
    FakeScheduler scheduler;
    QList<MonitorInfo> monitors{{"monitor", "DP-1", QRect(0, 0, 10, 10), {}, {}}};

    QString addSet(const QColor &color, const QSize &size = QSize(10, 10)) {
        QImage image(size, QImage::Format_RGB32);
        image.fill(color);
        const auto result = OutputService::applyManaged(image, size,
            {{"monitor", 1, image.rect(), image.rect()}}, color.name() + ".png", {}, plasma, root);
        if (!result.success) return {};
        return QFileInfo(result.manifestPath).dir().dirName();
    }

    SlideshowSettings settings() {
        SlideshowSettings value;
        if (!slideshow.load(&value).success) qFatal("Could not read test settings");
        return value;
    }
};

struct LockScreenFixture : Fixture {
    const QByteArray oldConfig = qgetenv("XDG_CONFIG_HOME");
    const QByteArray oldData = qgetenv("XDG_DATA_HOME");
    const QString statePath = temp.filePath("config/wallpaper-splitter/lockscreen.ini");

    LockScreenFixture() {
        qputenv("XDG_CONFIG_HOME", QFile::encodeName(temp.filePath("config")));
        qputenv("XDG_DATA_HOME", QFile::encodeName(temp.filePath("data")));
    }

    ~LockScreenFixture() {
        if (oldConfig.isNull()) qunsetenv("XDG_CONFIG_HOME");
        else qputenv("XDG_CONFIG_HOME", oldConfig);
        if (oldData.isNull()) qunsetenv("XDG_DATA_HOME");
        else qputenv("XDG_DATA_HOME", oldData);
    }

    QString lockScreenSet() const {
        const QSettings state(statePath, QSettings::IniFormat);
        const auto slide = QJsonDocument::fromJson(state.value("CurrentSlide").toString().toUtf8()).object();
        const auto crops = slide.value("crops").toArray();
        const QUrl crop(crops.at(0).toObject().value("url").toString());
        return QFileInfo(crop.toLocalFile()).dir().dirName();
    }
};
}

class SlideshowTest : public QObject {
    Q_OBJECT
private slots:
    void orderedPlaybackSurvivesRestartAndEdits();
    void optionallyMirrorsTheSuccessfulDesktopSlide();
    void separateLockScreenPlaylistAdvancesIndependently();
    void deletingLockScreenSetsKeepsAnyRemainingRotation();
    void removesDeletedSetsWithoutResettingRemainingPlaylist();
    void removingLastSetDisablesRotationWhenSchedulerFails();
    void shuffleAvoidsImmediateRepeats();
    void skipsUnusableSetsAndKeepsWallpaperWhenNoneMatch();
    void failureDoesNotAdvanceOrApplyAnotherSet();
    void disabledTickDoesNothingButManualNextWorks();
    void schedulerFailureLeavesRotationDisabled();
    void savingEnabledSlideshowRepairsUnitsBeforeManagingTimer();
    void concurrentOperationsCannotRace();
    void validatesSettingsWithoutClobberingSavedPlaylist();
    void generatesAndControlsUserTimer();
    void disablingTouchesOnlyTheSlideshowStartupLink();
    void disablingPreservesUnexpectedStartupEntries();
    void disablingHandlesMissingUnitFiles();
    void disablingRetainsUnitsWhenStopFails();
    void systemdAcceptsGeneratedUnits();
    void schedulerReportsFailureAndUsesAppImagePath();
    void schedulerStatusReportsLaunchFailures();
    void dialogSavesPlaylistOrderAndSettings();
    void dialogSavesLockScreenChoices();
    void dialogOmitsDeletedSetsAndSavesCleanPlaylist();
    void dialogCanSaveEmptyPlaylistAfterAllSetsWereDeleted();
    void dialogKeepsManualPlaybackAvailableAfterSchedulerFailure();
    void commandLineRejectsConflictsAndHonorsDisabledState();
};

void SlideshowTest::orderedPlaybackSurvivesRestartAndEdits() {
    Fixture f;
    const QString a = f.addSet(Qt::red), b = f.addSet(Qt::green), c = f.addSet(Qt::blue);
    QVERIFY(!a.isEmpty() && !b.isEmpty() && !c.isEmpty());
    SlideshowSettings settings;
    settings.setIds = {a, b, c};
    settings.enabled = true;
    settings.intervalMinutes = 17;
    QVERIFY(f.slideshow.configure(settings, f.scheduler).success);
    QVERIFY(f.scheduler.active);
    QCOMPARE(f.scheduler.interval, 17);
    QVERIFY(f.slideshow.next(f.monitors, f.plasma, true).success);
    QCOMPARE(f.settings().lastSetId, a);
    Slideshow restarted(f.config, f.root);
    QVERIFY(restarted.next(f.monitors, f.plasma, true).success);
    QCOMPARE(f.settings().lastSetId, b);
    // A dialog opened before a switch must not overwrite the latest cursor.
    settings.intervalMinutes = 25;
    QVERIFY(f.slideshow.configure(settings, f.scheduler).success);
    QCOMPARE(f.settings().lastSetId, b);
    QVERIFY(restarted.next(f.monitors, f.plasma, true).success);
    QCOMPARE(f.settings().lastSetId, c);
    QVERIFY(restarted.next(f.monitors, f.plasma, true).success);
    QCOMPARE(f.settings().lastSetId, a);
    settings.setIds = {c, b}; // Removing the last set starts at the first remaining entry.
    QVERIFY(f.slideshow.configure(settings, f.scheduler).success);
    QVERIFY(restarted.next(f.monitors, f.plasma, true).success);
    QCOMPARE(f.settings().lastSetId, c);
}

void SlideshowTest::optionallyMirrorsTheSuccessfulDesktopSlide() {
    LockScreenFixture f;
    SlideshowSettings settings;
    settings.setIds = {f.addSet(Qt::red), f.addSet(Qt::green), f.addSet(Qt::blue)};
    // A version 1 settings file written before lock screen rotation defaults to Off.
    QVERIFY(QDir().mkpath(QFileInfo(f.config).absolutePath()));
    QFile legacy(f.config);
    QVERIFY(legacy.open(QIODevice::WriteOnly));
    legacy.write(QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"enabled", false}, {"shuffle", false},
        {"intervalMinutes", 30}, {"setIds", QJsonArray::fromStringList(settings.setIds)}}).toJson());
    legacy.close();
    QCOMPARE(f.settings().lockScreenMode, LockScreenRotation::Off);
    QVERIFY(f.slideshow.next(f.monitors, f.plasma).success);
    QVERIFY(!QFileInfo::exists(f.statePath));

    settings.lockScreenMode = LockScreenRotation::MatchDesktop;
    settings.enabled = true;
    settings.shuffle = true;
    const auto configured = f.slideshow.configure(settings, f.scheduler, f.monitors);
    QVERIFY2(configured.success, qPrintable(configured.message));
    QCOMPARE(f.lockScreenSet(), settings.setIds.first());
    const auto result = f.slideshow.next(f.monitors, f.plasma, true);
    QVERIFY2(result.success, qPrintable(result.message));
    const auto shown = f.settings().lastSetId;
    QVERIFY(shown != settings.setIds.first());
    QCOMPARE(f.lockScreenSet(), shown);
    QCOMPARE(QFileInfo(f.plasma.paths.first()).dir().dirName(), shown);

    f.plasma.succeeds = false;
    QVERIFY(!f.slideshow.next(f.monitors, f.plasma, true).success);
    QCOMPARE(f.settings().lastSetId, shown);
    QCOMPARE(f.lockScreenSet(), shown);
}

void SlideshowTest::separateLockScreenPlaylistAdvancesIndependently() {
    LockScreenFixture f;
    const auto first = f.addSet(Qt::red);
    const auto second = f.addSet(Qt::green);
    const auto incompatible = f.addSet(Qt::blue, QSize(20, 10));
    SlideshowSettings settings;
    settings.setIds = {first, second};
    settings.lockScreenMode = LockScreenRotation::SeparatePlaylist;
    settings.lockScreenSetIds = {incompatible, second, first};
    QVERIFY(f.slideshow.configure(settings, f.scheduler).success);

    QVERIFY(f.slideshow.next(f.monitors, f.plasma).success);
    QCOMPARE(f.settings().lastSetId, first);
    QCOMPARE(f.settings().lastLockScreenSetId, second);
    QCOMPARE(f.lockScreenSet(), second);

    f.plasma.succeeds = false;
    const auto partial = Slideshow(f.config, f.root).next(f.monitors, f.plasma);
    QVERIFY(!partial.success);
    QVERIFY(partial.message.contains("Desktop:"));
    QCOMPARE(f.settings().lastSetId, first);
    QCOMPARE(f.settings().lastLockScreenSetId, first);
    QCOMPARE(f.lockScreenSet(), first);

    f.plasma.succeeds = true;
    settings.lockScreenSetIds = {incompatible};
    QVERIFY(f.slideshow.configure(settings, f.scheduler).success);
    const auto lockFailure = f.slideshow.next(f.monitors, f.plasma);
    QVERIFY(!lockFailure.success);
    QVERIFY(lockFailure.message.contains("Lock screen:"));
    QCOMPARE(f.settings().lastSetId, second);
    QCOMPARE(f.lockScreenSet(), first);
}

void SlideshowTest::deletingLockScreenSetsKeepsAnyRemainingRotation() {
    LockScreenFixture f;
    const auto desktop = f.addSet(Qt::red);
    const auto lockScreen = f.addSet(Qt::green);
    SlideshowSettings settings;
    settings.enabled = true;
    settings.setIds = {desktop};
    settings.lockScreenMode = LockScreenRotation::SeparatePlaylist;
    settings.lockScreenSetIds = {lockScreen};
    QVERIFY(f.slideshow.configure(settings, f.scheduler).success);
    QVERIFY(f.slideshow.removeSets({lockScreen}, f.scheduler).success);
    QCOMPARE(f.settings().lockScreenMode, LockScreenRotation::Off);
    QVERIFY(f.settings().enabled);
    QVERIFY(f.scheduler.active);

    QVERIFY(f.slideshow.configure(settings, f.scheduler).success);
    QVERIFY(f.slideshow.removeSets({desktop}, f.scheduler).success);
    QVERIFY(f.settings().enabled);
    f.plasma.calls = 0;
    QVERIFY(f.slideshow.next(f.monitors, f.plasma, true).success);
    QCOMPARE(f.plasma.calls, 0);
    QCOMPARE(f.lockScreenSet(), lockScreen);

    QVERIFY(f.slideshow.removeSets({lockScreen}, f.scheduler).success);
    QVERIFY(f.settings().lastLockScreenSetId.isEmpty());
    QVERIFY(!f.settings().enabled);
    QVERIFY(!f.scheduler.active);
}

void SlideshowTest::dialogSavesLockScreenChoices() {
    Fixture f;
    const auto first = f.addSet(Qt::red);
    const auto second = f.addSet(Qt::green);
    const auto duplicate = f.addSet(Qt::blue);
    SetLibrary library(f.root);
    QVERIFY(library.rename(first, "Zulu").success);
    QVERIFY(library.rename(second, "Alpha").success);
    QVERIFY(library.rename(duplicate, "Alpha").success);
    QStringList alphabetical{second, duplicate};
    alphabetical.sort();
    alphabetical.append(first);
    SlideshowDialog dialog(nullptr, f.root, f.config, &f.scheduler);
    auto *mode = dialog.findChild<QComboBox *>("slideshowLockScreenMode");
    auto *playlists = dialog.findChild<QTabWidget *>("slideshowPlaylists");
    auto *desktop = dialog.findChild<WallpaperSetList *>("slideshowSets");
    auto *lockScreen = dialog.findChild<WallpaperSetList *>("slideshowLockScreenSets");
    auto *save = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save);
    auto *next = dialog.findChild<QPushButton *>("slideshowNext");
    QCOMPARE(mode->currentText(), "Off");
    QVERIFY(!playlists->isTabVisible(1));
    for (int i = 0; i < desktop->topLevelItemCount(); ++i)
        if (desktop->topLevelItem(i)->data(0, Qt::UserRole).toString() == first)
            desktop->topLevelItem(i)->setCheckState(0, Qt::Checked);

    mode->setCurrentText("Match desktop");
    save->click();
    QCOMPARE(f.settings().lockScreenMode, LockScreenRotation::MatchDesktop);
    QVERIFY(next->isEnabled());
    QVERIFY(!playlists->isTabVisible(1));

    mode->setCurrentText("Separate playlist");
    QVERIFY(playlists->isTabVisible(1));
    QVERIFY(!next->isEnabled());
    QCOMPARE(playlists->currentIndex(), 1);
    for (int i = 0; i < lockScreen->topLevelItemCount(); ++i)
        if (lockScreen->topLevelItem(i)->data(0, Qt::UserRole).toString() == second)
            lockScreen->topLevelItem(i)->setCheckState(0, Qt::Checked);
    save->click();
    QVERIFY(next->isEnabled());
    QCOMPARE(f.settings().setIds, QStringList{first});
    QCOMPARE(f.settings().lockScreenSetIds, QStringList{second});

    SlideshowDialog reopened(nullptr, f.root, f.config, &f.scheduler);
    auto *reopenedMode = reopened.findChild<QComboBox *>("slideshowLockScreenMode");
    auto *reopenedLockScreen = reopened.findChild<WallpaperSetList *>("slideshowLockScreenSets");
    QCOMPARE(reopenedMode->currentText(), "Separate playlist");
    auto *reopenedDesktop = reopened.findChild<WallpaperSetList *>("slideshowSets");
    QCOMPARE(reopenedDesktop->topLevelItemCount(), alphabetical.size());
    QCOMPARE(reopenedLockScreen->topLevelItemCount(), alphabetical.size());
    for (int i = 0; i < alphabetical.size(); ++i) {
        const auto &id = alphabetical.at(i);
        QCOMPARE(reopenedDesktop->topLevelItem(i)->data(0, Qt::UserRole).toString(), id);
        QCOMPARE(reopenedLockScreen->topLevelItem(i)->data(0, Qt::UserRole).toString(), id);
        QCOMPARE(reopenedDesktop->topLevelItem(i)->checkState(0), id == first ? Qt::Checked : Qt::Unchecked);
        QCOMPARE(reopenedLockScreen->topLevelItem(i)->checkState(0), id == second ? Qt::Checked : Qt::Unchecked);
    }
    reopened.show();
    reopened.findChild<QTabWidget *>("slideshowPlaylists")->setCurrentIndex(1);
    auto *header = reopenedLockScreen->header();
    QTest::mouseClick(header->viewport(), Qt::LeftButton, Qt::NoModifier,
        QPoint(header->sectionViewportPosition(WallpaperSetList::DateAdded) + 8, header->height() / 2));
    QCOMPARE(reopenedDesktop->header()->sortIndicatorSection(), WallpaperSetList::DateAdded);
    QCOMPARE(reopenedDesktop->setIds(), reopenedLockScreen->setIds());
    QCOMPARE(reopenedDesktop->setIds(true), QStringList{first});
    QCOMPARE(reopenedLockScreen->setIds(true), QStringList{second});

    reopenedMode->setCurrentText("Off");
    reopened.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    QCOMPARE(f.settings().lockScreenMode, LockScreenRotation::Off);
    QCOMPARE(f.settings().lockScreenSetIds, QStringList{second});
}

void SlideshowTest::removesDeletedSetsWithoutResettingRemainingPlaylist() {
    Fixture f;
    QVERIFY(f.slideshow.removeSets({"set-00000000000000000000000000000000"}, f.scheduler).success);
    QVERIFY(!QFileInfo::exists(f.config));
    const auto a = f.addSet(Qt::red), b = f.addSet(Qt::green), c = f.addSet(Qt::blue);
    SlideshowSettings settings;
    settings.setIds = {a, b, c};
    settings.enabled = true;
    settings.intervalMinutes = 17;
    QVERIFY(f.slideshow.configure(settings, f.scheduler).success);
    QVERIFY(f.slideshow.next(f.monitors, f.plasma).success);
    settings = f.settings();
    settings.shuffle = true;
    QVERIFY(f.slideshow.configure(settings, f.scheduler).success);
    QVERIFY(f.slideshow.removeSets({b, "set-00000000000000000000000000000000"}, f.scheduler).success);
    const auto remaining = f.settings();
    QCOMPARE(remaining.setIds, (QStringList{a, c}));
    QCOMPARE(remaining.lastSetId, a);
    QCOMPARE(remaining.lastAttempt, settings.lastAttempt);
    QCOMPARE(remaining.intervalMinutes, 17);
    QVERIFY(remaining.shuffle);
    QVERIFY(remaining.enabled);
    QVERIFY(f.scheduler.active);
    QCOMPARE(f.scheduler.disableCalls, 0);
    QVERIFY(f.slideshow.removeSets({a, c}, f.scheduler).success);
    QVERIFY(f.settings().setIds.isEmpty());
    QVERIFY(f.settings().lastSetId.isEmpty());
    QVERIFY(!f.settings().enabled);
    QVERIFY(!f.scheduler.active);
    QCOMPARE(f.scheduler.disableCalls, 1);
}

void SlideshowTest::removingLastSetDisablesRotationWhenSchedulerFails() {
    Fixture f;
    const auto id = f.addSet(Qt::blue);
    SlideshowSettings settings;
    settings.enabled = true;
    settings.setIds = {id};
    QVERIFY(f.slideshow.configure(settings, f.scheduler).success);
    f.scheduler.failDisable = true;
    QVERIFY(!f.slideshow.removeSets({id}, f.scheduler).success);
    QVERIFY(f.settings().setIds.isEmpty());
    QVERIFY(!f.settings().enabled);
    f.plasma.calls = 0;
    QVERIFY(f.slideshow.next(f.monitors, f.plasma, true).success);
    QCOMPARE(f.plasma.calls, 0);
}

void SlideshowTest::shuffleAvoidsImmediateRepeats() {
    Fixture f;
    SlideshowSettings settings;
    settings.setIds = {f.addSet(Qt::red), f.addSet(Qt::green), f.addSet(Qt::blue)};
    settings.shuffle = true;
    QVERIFY(f.slideshow.configure(settings, f.scheduler).success);
    QString previous;
    for (int i = 0; i < 30; ++i) {
        QVERIFY(f.slideshow.next(f.monitors, f.plasma).success);
        const QString current = f.settings().lastSetId;
        QVERIFY(settings.setIds.contains(current));
        QVERIFY(current != previous);
        previous = current;
    }
    settings.setIds = {previous};
    QVERIFY(f.slideshow.configure(settings, f.scheduler).success);
    QVERIFY(f.slideshow.next(f.monitors, f.plasma).success);
    QCOMPARE(f.settings().lastSetId, previous);
}

void SlideshowTest::skipsUnusableSetsAndKeepsWallpaperWhenNoneMatch() {
    Fixture f;
    const auto missing = f.addSet(Qt::red);
    const auto incompatible = f.addSet(Qt::green, QSize(20, 10));
    const auto broken = f.addSet(Qt::yellow);
    const auto good = f.addSet(Qt::blue);
    SetLibrary library(f.root);
    QVERIFY(library.remove(missing, f.plasma).success);
    WallpaperSet set;
    QVERIFY(library.load(broken, &set).success);
    QFile corrupt(set.paths.first());
    QVERIFY(corrupt.open(QIODevice::WriteOnly));
    corrupt.write("broken image");
    corrupt.close();
    SlideshowSettings settings;
    settings.setIds = {missing, incompatible, broken, good};
    QVERIFY(f.slideshow.configure(settings, f.scheduler).success);
    f.plasma.calls = 0;
    QVERIFY(f.slideshow.next(f.monitors, f.plasma).success);
    QCOMPARE(f.plasma.calls, 1);
    QCOMPARE(f.settings().lastSetId, good);
    QVERIFY(library.remove(good, f.plasma).success);
    f.plasma.calls = 0;
    const auto result = f.slideshow.next(f.monitors, f.plasma);
    QVERIFY(!result.success);
    QVERIFY(result.message.contains("No usable"));
    QCOMPARE(f.plasma.calls, 0);
    QCOMPARE(f.settings().lastSetId, good);
    QVERIFY(!f.settings().lastError.isEmpty());
    QVERIFY(!f.settings().lastAttempt.isEmpty());
}

void SlideshowTest::failureDoesNotAdvanceOrApplyAnotherSet() {
    Fixture f;
    const auto a = f.addSet(Qt::red), b = f.addSet(Qt::green), c = f.addSet(Qt::blue);
    SlideshowSettings settings;
    settings.setIds = {a, b, c};
    QVERIFY(f.slideshow.configure(settings, f.scheduler).success);
    QVERIFY(f.slideshow.next(f.monitors, f.plasma).success);
    f.plasma.calls = 0;
    f.plasma.succeeds = false;
    QVERIFY(!f.slideshow.next(f.monitors, f.plasma).success);
    QCOMPARE(f.plasma.calls, 1);
    QCOMPARE(f.settings().lastSetId, a);
    QCOMPARE(f.settings().lastError, "Plasma unavailable");
    f.plasma.succeeds = true;
    QVERIFY(f.slideshow.next(f.monitors, f.plasma).success);
    QCOMPARE(f.settings().lastSetId, b);
    QVERIFY(f.settings().lastError.isEmpty());
    // A failure saving the manifest after Plasma changed must also stop the loop.
    f.plasma.calls = 0;
    f.plasma.duringApply = [&] {
        const auto manifest = QDir(f.root).filePath(c + "/manifest.json");
        QVERIFY(QFile::remove(manifest));
        QVERIFY(QDir().mkdir(manifest));
    };
    QVERIFY(!f.slideshow.next(f.monitors, f.plasma).success);
    QCOMPARE(f.plasma.calls, 1);
    QCOMPARE(f.settings().lastSetId, b);
}

void SlideshowTest::disabledTickDoesNothingButManualNextWorks() {
    Fixture f;
    SlideshowSettings settings;
    settings.setIds = {f.addSet(Qt::blue)};
    QVERIFY(f.slideshow.configure(settings, f.scheduler).success);
    f.plasma.calls = 0;
    QVERIFY(f.slideshow.next(f.monitors, f.plasma, true).success);
    QCOMPARE(f.plasma.calls, 0);
    QVERIFY(f.settings().lastAttempt.isEmpty());
    QVERIFY(f.slideshow.next(f.monitors, f.plasma).success);
    QCOMPARE(f.plasma.calls, 1);
}

void SlideshowTest::schedulerFailureLeavesRotationDisabled() {
    Fixture f;
    SlideshowSettings settings;
    settings.setIds = {f.addSet(Qt::blue)};
    settings.enabled = true;
    f.scheduler.failEnable = true;
    QVERIFY(!f.slideshow.configure(settings, f.scheduler).success);
    QVERIFY(!f.settings().enabled);
    QVERIFY(!f.scheduler.active);
    QCOMPARE(f.settings().setIds, settings.setIds);
    QVERIFY(f.settings().lastError.contains("enable failed"));
    f.scheduler.failEnable = false;
    QVERIFY(f.slideshow.configure(settings, f.scheduler).success);
    QVERIFY(f.settings().enabled);
    f.scheduler.failDisable = true;
    settings.enabled = false;
    const int disableCalls = f.scheduler.disableCalls;
    QVERIFY(!f.slideshow.configure(settings, f.scheduler).success);
    QCOMPARE(f.scheduler.disableCalls, disableCalls + 1);
    QCOMPARE(f.settings().lastError.count("stop failed"), 1);
    QVERIFY(f.scheduler.active); // Can't stop it, but the persisted off flag makes it harmless.
    QVERIFY(!f.settings().enabled);
    f.plasma.calls = 0;
    QVERIFY(f.slideshow.next(f.monitors, f.plasma, true).success);
    QCOMPARE(f.plasma.calls, 0);
}

void SlideshowTest::savingEnabledSlideshowRepairsUnitsBeforeManagingTimer() {
    Fixture f;
    SlideshowSettings settings;
    settings.setIds = {f.addSet(Qt::blue)};
    settings.enabled = true;
    const QString units = f.temp.filePath("systemd/user");
    bool reloaded = false;
    QStringList operations;
    SystemdSlideshowScheduler scheduler(units, QCoreApplication::applicationFilePath(),
        [&](const QStringList &args) {
            operations.append(args.first());
            if (args.first() == "disable")
                return OperationResult::failure(OperationError::FileSystem, "Access denied");
            if (args.first() == "daemon-reload") {
                if (!QFileInfo::exists(QDir(units).filePath("wallpaper-splitter-slideshow.timer"))
                    || !QFileInfo::exists(QDir(units).filePath("wallpaper-splitter-slideshow.service")))
                    return OperationResult::failure(OperationError::FileSystem, "unit files not repaired");
                reloaded = true;
            }
            if ((args.first() == "enable" || args.first() == "restart") && !reloaded)
                return OperationResult::failure(OperationError::FileSystem, "stale manager state");
            return OperationResult::ok();
        });
    QVERIFY(f.slideshow.configure(settings, scheduler).success);
    QVERIFY(f.settings().enabled);
    // Simulate a unit being removed while the manager still remembers it.
    QVERIFY(QFile::remove(QDir(units).filePath("wallpaper-splitter-slideshow.service")));
    reloaded = false;
    operations.clear();
    const auto repaired = f.slideshow.configure(settings, scheduler);
    QVERIFY2(repaired.success, qPrintable(repaired.message));
    QCOMPARE(operations, (QStringList{"is-active", "daemon-reload", "enable", "restart"}));
    QVERIFY(f.settings().enabled);
}

void SlideshowTest::concurrentOperationsCannotRace() {
    Fixture f;
    const auto id = f.addSet(Qt::blue);
    SlideshowSettings settings;
    settings.setIds = {id};
    QVERIFY(f.slideshow.configure(settings, f.scheduler).success);
    QLockFile stateLock(f.config + ".lock");
    QVERIFY(stateLock.tryLock());
    f.plasma.calls = 0;
    QVERIFY(!f.slideshow.next(f.monitors, f.plasma).success);
    QVERIFY(!f.slideshow.configure(settings, f.scheduler).success);
    QVERIFY(!f.slideshow.removeSets({id}, f.scheduler).success);
    QCOMPARE(f.plasma.calls, 0);
    stateLock.unlock();
    QCOMPARE(f.settings().setIds, QStringList{id});
    QLockFile libraryLock(QDir(f.root).filePath(".operation.lock"));
    QVERIFY(libraryLock.tryLock());
    const auto result = f.slideshow.next(f.monitors, f.plasma);
    QCOMPARE(result.error, OperationError::Collision);
    QCOMPARE(f.plasma.calls, 0);
    SetLibrary library(f.root);
    QVERIFY(!library.rename(id, "changed").success);
    QVERIFY(!library.remove(id, f.plasma).success);
    QCOMPARE(f.addSet(Qt::blue), QString());
    libraryLock.unlock();
    QVERIFY(f.slideshow.next(f.monitors, f.plasma).success);
}

void SlideshowTest::validatesSettingsWithoutClobberingSavedPlaylist() {
    Fixture f;
    SlideshowSettings settings;
    settings.setIds = {f.addSet(Qt::blue)};
    settings.enabled = true;
    QVERIFY(f.slideshow.configure(settings, f.scheduler).success);
    auto invalid = settings;
    invalid.intervalMinutes = 0;
    QVERIFY(!f.slideshow.configure(invalid, f.scheduler).success);
    invalid = settings;
    invalid.setIds.append(settings.setIds.first());
    QVERIFY(!f.slideshow.configure(invalid, f.scheduler).success);
    invalid.setIds = {"../outside"};
    QVERIFY(!f.slideshow.configure(invalid, f.scheduler).success);
    invalid.setIds.clear();
    QVERIFY(!f.slideshow.configure(invalid, f.scheduler).success);
    QCOMPARE(f.settings().setIds, settings.setIds);
    QVERIFY(f.settings().enabled);
    QFile file(f.config);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{\"schemaVersion\":99}");
    file.close();
    f.plasma.calls = 0;
    QVERIFY(!f.slideshow.next(f.monitors, f.plasma, true).success);
    QVERIFY(!f.slideshow.configure(settings, f.scheduler).success);
    QCOMPARE(f.plasma.calls, 0);
    QVERIFY(!f.slideshow.removeSets(settings.setIds, f.scheduler).success);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), "{\"schemaVersion\":99}");
}

void SlideshowTest::generatesAndControlsUserTimer() {
    QTemporaryDir temp;
    const QString executable = temp.filePath("Wallpaper $special% \"quote\" \\app");
    QFile binary(executable);
    QVERIFY(binary.open(QIODevice::WriteOnly));
    binary.write("#!/bin/sh\nexit 0\n");
    binary.close();
    QVERIFY(binary.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    QList<QStringList> commands;
    SystemdSlideshowScheduler scheduler(temp.path(), executable, [&](const QStringList &args) {
        commands.append(args);
        return OperationResult::ok();
    });
    QVERIFY(scheduler.enable(5).success);
    QCOMPARE(commands, (QList<QStringList>{{"is-active", "graphical-session.target"}, {"daemon-reload"},
        {"enable", "wallpaper-splitter-slideshow.timer"}, {"restart", "wallpaper-splitter-slideshow.timer"}}));
    QFile service(temp.filePath("wallpaper-splitter-slideshow.service"));
    QVERIFY(service.open(QIODevice::ReadOnly));
    const auto serviceText = service.readAll();
    QVERIFY(serviceText.contains("$$special%%"));
    QVERIFY(serviceText.contains("\\\"quote\\\""));
    QVERIFY(serviceText.contains("--slideshow-tick"));
    QVERIFY(serviceText.contains("Type=oneshot"));
    QVERIFY(serviceText.contains("ExecStart=/usr/bin/env -- "));
    QVERIFY(serviceText.contains("Requisite=graphical-session.target"));
    QFile timer(temp.filePath("wallpaper-splitter-slideshow.timer"));
    QVERIFY(timer.open(QIODevice::ReadOnly));
    const auto timerText = timer.readAll();
    QVERIFY(timerText.contains("OnActiveSec=5min"));
    QVERIFY(timerText.contains("OnUnitInactiveSec=5min"));
    QVERIFY(timerText.contains("WantedBy=graphical-session.target"));
    QVERIFY(timerText.contains("WakeSystem=false"));
    QVERIFY(scheduler.disable().success);
    QCOMPARE(commands.at(commands.size() - 2), (QStringList{"stop", "wallpaper-splitter-slideshow.timer"}));
    QCOMPARE(commands.last(), (QStringList{"daemon-reload"}));
    QVERIFY(!QFileInfo::exists(service.fileName()));
    QVERIFY(!QFileInfo::exists(timer.fileName()));
    QVERIFY(scheduler.status().success);
    QCOMPARE(commands.at(commands.size() - 2),
             (QStringList{"is-active", "wallpaper-splitter-slideshow.timer"}));
    QCOMPARE(commands.last(), (QStringList{"show", "--property=Result", "--value",
                                          "wallpaper-splitter-slideshow.service"}));
    QVERIFY(SystemdSlideshowScheduler::serviceContents(executable, true)
                .contains("Environment=APPIMAGE_EXTRACT_AND_RUN=1"));
}

void SlideshowTest::schedulerReportsFailureAndUsesAppImagePath() {
    QTemporaryDir temp;
    int calls = 0;
    SystemdSlideshowScheduler scheduler(temp.path(), QCoreApplication::applicationFilePath(),
        [&](const QStringList &) { ++calls; return OperationResult::failure(OperationError::FileSystem, "no systemd"); });
    QVERIFY(scheduler.disable().success); // No units yet: no dependency on systemd.
    QCOMPARE(calls, 0);
    QVERIFY(!scheduler.enable(0).success);
    QCOMPARE(calls, 0);
    QVERIFY(!scheduler.enable(10).success);
    QCOMPARE(calls, 1); // Don't continue enabling without a graphical user session.
    const QByteArray before = qgetenv("APPIMAGE");
    qputenv("APPIMAGE", "/tmp/Wallpaper Splitter.AppImage");
    const auto path = SystemdSlideshowScheduler::launchExecutable();
    if (before.isNull()) qunsetenv("APPIMAGE"); else qputenv("APPIMAGE", before);
    QCOMPARE(path, "/tmp/Wallpaper Splitter.AppImage");
}

void SlideshowTest::disablingTouchesOnlyTheSlideshowStartupLink() {
    QTemporaryDir temp;
    const QString timer = temp.filePath("wallpaper-splitter-slideshow.timer");
    const QString startup = temp.filePath("graphical-session.target.wants");
    const QString ownLink = startup + "/wallpaper-splitter-slideshow.timer";
    const QString unrelatedDirectory = temp.filePath("timers.target.wants");
    const QString unrelatedLink = unrelatedDirectory + "/eos-update-notifier.timer";
    const QString inaccessibleDirectory = temp.filePath("private");
    QVERIFY(QDir().mkpath(startup));
    QVERIFY(QDir().mkpath(unrelatedDirectory));
    QVERIFY(QDir().mkpath(inaccessibleDirectory));
    QFile foreignUnit(inaccessibleDirectory + "/eos-update-notifier.timer");
    QVERIFY(foreignUnit.open(QIODevice::WriteOnly));
    foreignUnit.write("[Timer]\nOnActiveSec=1h\n");
    foreignUnit.close();
    QVERIFY(QFile::link(foreignUnit.fileName(), unrelatedLink));
    QList<QStringList> commands;
    SystemdSlideshowScheduler scheduler(temp.path(), QCoreApplication::applicationFilePath(),
        [&](const QStringList &args) {
            commands.append(args);
            if (args.first() == "disable")
                return OperationResult::failure(OperationError::FileSystem,
                    "File eos-update-notifier.timer: Permission denied");
            return OperationResult::ok();
        });
    QVERIFY(scheduler.enable(30).success);
    QVERIFY(QFile::link(timer, ownLink)); // Link normally created by systemctl enable.
    commands.clear();
    const auto oldPermissions = QFileInfo(inaccessibleDirectory).permissions();
    QVERIFY(QFile::setPermissions(inaccessibleDirectory, {}));
    const auto result = scheduler.disable();
    QVERIFY(QFile::setPermissions(inaccessibleDirectory, oldPermissions));
    QVERIFY2(result.success, qPrintable(result.message));
    QVERIFY(!QFileInfo(ownLink).isSymLink());
    QVERIFY(!QFileInfo::exists(timer));
    QVERIFY(!QFileInfo::exists(temp.filePath("wallpaper-splitter-slideshow.service")));
    QVERIFY(QFileInfo(unrelatedLink).isSymLink());
    QCOMPARE(QFileInfo(unrelatedLink).symLinkTarget(), foreignUnit.fileName());
    QCOMPARE(commands, (QList<QStringList>{{"daemon-reload"},
        {"stop", "wallpaper-splitter-slideshow.timer"}, {"daemon-reload"}}));
    commands.clear();
    QVERIFY(scheduler.disable().success); // Already cleaned up.
    QVERIFY(commands.isEmpty());
    QVERIFY(scheduler.enable(15).success);
    QVERIFY(QFileInfo::exists(timer));
    QVERIFY(QFileInfo::exists(temp.filePath("wallpaper-splitter-slideshow.service")));
}

void SlideshowTest::disablingPreservesUnexpectedStartupEntries() {
    QTemporaryDir temp;
    const QString startup = temp.filePath("graphical-session.target.wants");
    const QString entry = startup + "/wallpaper-splitter-slideshow.timer";
    QVERIFY(QDir().mkpath(startup));
    QList<QStringList> commands;
    SystemdSlideshowScheduler scheduler(temp.path(), QCoreApplication::applicationFilePath(),
        [&](const QStringList &args) { commands.append(args); return OperationResult::ok(); });
    QFile file(entry);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("keep this file");
    file.close();
    QVERIFY(!scheduler.disable().success);
    QVERIFY(commands.isEmpty());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), "keep this file");
    file.close();
    QVERIFY(file.remove());
    const auto unrelated = temp.filePath("unrelated.timer");
    QVERIFY(QFile::link(unrelated, entry));
    QVERIFY(!scheduler.disable().success);
    QVERIFY(commands.isEmpty());
    QCOMPARE(QFileInfo(entry).symLinkTarget(), unrelated);
}

void SlideshowTest::disablingHandlesMissingUnitFiles() {
    QTemporaryDir temp;
    const QString startup = temp.filePath("graphical-session.target.wants");
    const QString entry = startup + "/wallpaper-splitter-slideshow.timer";
    QVERIFY(QDir().mkpath(startup));
    QString activeState = "inactive";
    QList<QStringList> commands;
    SystemdSlideshowScheduler scheduler(temp.path(), QCoreApplication::applicationFilePath(),
        [&](const QStringList &args) {
            commands.append(args);
            auto result = OperationResult::ok();
            if (args.first() == "show")
                result.message = "LoadState=not-found\nActiveState=" + activeState;
            return result;
        });
    // Include a relative, dangling link, as produced by some unit-file tools.
    QVERIFY(QFile::link("../wallpaper-splitter-slideshow.timer", entry));
    QVERIFY(scheduler.disable().success);
    QVERIFY(!QFileInfo(entry).isSymLink());
    QCOMPARE(commands, (QList<QStringList>{{"daemon-reload"},
        {"show", "--property=LoadState", "--property=ActiveState", "wallpaper-splitter-slideshow.timer"},
        {"daemon-reload"}})); // No stop of a nonexistent inactive unit.
    QVERIFY(QFile::link("../wallpaper-splitter-slideshow.timer", entry));
    activeState = "active";
    commands.clear();
    QVERIFY(scheduler.disable().success);
    QCOMPARE(commands.at(commands.size() - 2), (QStringList{"stop", "wallpaper-splitter-slideshow.timer"}));
    QCOMPARE(commands.last(), (QStringList{"daemon-reload"}));
}

void SlideshowTest::disablingRetainsUnitsWhenStopFails() {
    QTemporaryDir temp;
    bool failStop = true;
    SystemdSlideshowScheduler scheduler(temp.path(), QCoreApplication::applicationFilePath(),
        [&](const QStringList &args) {
            if (failStop && args.first() == "stop")
                return OperationResult::failure(OperationError::FileSystem, "stop denied");
            return OperationResult::ok();
        });
    QVERIFY(scheduler.enable(30).success);
    const auto result = scheduler.disable();
    QVERIFY(!result.success);
    QCOMPARE(result.message, "stop denied");
    QVERIFY(QFileInfo::exists(temp.filePath("wallpaper-splitter-slideshow.timer")));
    QVERIFY(QFileInfo::exists(temp.filePath("wallpaper-splitter-slideshow.service")));
    failStop = false;
    QVERIFY(scheduler.disable().success);
    QVERIFY(!QFileInfo::exists(temp.filePath("wallpaper-splitter-slideshow.timer")));
    QVERIFY(!QFileInfo::exists(temp.filePath("wallpaper-splitter-slideshow.service")));
}

void SlideshowTest::systemdAcceptsGeneratedUnits() {
    const QString analyzer = QStandardPaths::findExecutable("systemd-analyze");
    if (analyzer.isEmpty()) QSKIP("systemd-analyze is not installed");
    QTemporaryDir temp;
    const QString executable = temp.filePath(R"(Wallpaper $special% "quoted" \AppImage)");
    QFile binary(executable);
    QVERIFY(binary.open(QIODevice::WriteOnly));
    binary.write("#!/bin/sh\nexit 0\n");
    binary.close();
    QVERIFY(binary.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    SystemdSlideshowScheduler scheduler(temp.path(), executable,
        [](const QStringList &) { return OperationResult::ok(); });
    QVERIFY(scheduler.enable(30).success);
    QProcess process;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("XDG_RUNTIME_DIR", temp.path());
    process.setProcessEnvironment(env);
    process.start(analyzer, {"--user", "--man=no", "verify",
        temp.filePath("wallpaper-splitter-slideshow.service"),
        temp.filePath("wallpaper-splitter-slideshow.timer")});
    QVERIFY(process.waitForFinished(10000));
    const auto errors = process.readAllStandardError();
    QVERIFY2(process.exitCode() == 0, errors.constData());
}

void SlideshowTest::schedulerStatusReportsLaunchFailures() {
    QTemporaryDir temp;
    bool active = true;
    QString serviceResult = "exit-code";
    SystemdSlideshowScheduler scheduler(temp.path(), QCoreApplication::applicationFilePath(),
        [&](const QStringList &args) {
            if (args.first() == "is-active")
                return active ? OperationResult::ok()
                              : OperationResult::failure(OperationError::FileSystem, "timer inactive");
            auto result = OperationResult::ok();
            result.message = serviceResult;
            return result;
        });
    const auto failed = scheduler.status();
    QVERIFY(!failed.success);
    QVERIFY(failed.message.contains("last scheduled switch failed"));
    serviceResult = "success";
    QVERIFY(scheduler.status().success);
    active = false;
    QCOMPARE(scheduler.status().message, "timer inactive");
}

void SlideshowTest::dialogSavesPlaylistOrderAndSettings() {
    Fixture f;
    const auto a = f.addSet(Qt::red), b = f.addSet(Qt::blue);
    SetLibrary library(f.root);
    QVERIFY(library.rename(a, "Alpha").success);
    QVERIFY(library.rename(b, "Bravo").success);
    SlideshowSettings settings;
    settings.setIds = {a, b};
    QVERIFY(f.slideshow.configure(settings, f.scheduler).success);
    SlideshowDialog dialog(nullptr, f.root, f.config, &f.scheduler);
    dialog.show();
    auto *list = dialog.findChild<WallpaperSetList *>("slideshowSets");
    auto *next = dialog.findChild<QPushButton *>("slideshowNext");
    QVERIFY(list);
    QCOMPARE(list->topLevelItemCount(), 2);
    QCOMPARE(list->topLevelItem(0)->data(0, Qt::UserRole).toString(), a);
    auto *header = list->header();
    QCOMPARE(list->dragDropMode(), QAbstractItemView::InternalMove);
    QVERIFY(next->isEnabled());
    const auto clickHeader = [header](int column) {
        QTest::mouseClick(header->viewport(), Qt::LeftButton, Qt::NoModifier,
            QPoint(header->sectionViewportPosition(column) + 8, header->height() / 2));
    };
    clickHeader(WallpaperSetList::Name);
    QCOMPARE(list->setIds(), (QStringList{b, a}));
    QCOMPARE(header->sortIndicatorOrder(), Qt::DescendingOrder);
    clickHeader(WallpaperSetList::Name);
    QCOMPARE(list->topLevelItem(0)->data(0, Qt::UserRole).toString(), a);
    list->setCurrentItem(list->topLevelItem(0));
    list->parentWidget()->findChild<QPushButton *>("moveSetDown")->click();
    QCOMPARE(list->setIds(), (QStringList{b, a}));
    QVERIFY(!header->isSortIndicatorShown());
    dialog.findChild<QSpinBox *>("slideshowInterval")->setValue(45);
    dialog.findChild<QCheckBox *>("slideshowEnabled")->setChecked(true);
    dialog.findChild<QCheckBox *>("slideshowShuffle")->setChecked(true);
    QVERIFY(!dialog.findChild<QPushButton *>("slideshowNext")->isEnabled());
    dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    QCOMPARE(f.settings().setIds, (QStringList{b, a}));
    QCOMPARE(f.settings().intervalMinutes, 45);
    QVERIFY(f.settings().shuffle);
    QVERIFY(f.settings().enabled);
    QVERIFY(dialog.findChild<QPushButton *>("slideshowNext")->isEnabled());
    QVERIFY(dialog.findChild<QLabel *>("slideshowStatus")->text().contains("rotation is on"));
    SlideshowDialog reopened(nullptr, f.root, f.config, &f.scheduler);
    QCOMPARE(reopened.findChild<WallpaperSetList *>("slideshowSets")->topLevelItem(0)->data(0, Qt::UserRole).toString(), a);
    reopened.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    QCOMPARE(f.settings().setIds, (QStringList{b, a}));
}

void SlideshowTest::dialogOmitsDeletedSetsAndSavesCleanPlaylist() {
    Fixture f;
    const auto a = f.addSet(Qt::red), b = f.addSet(Qt::blue), deleted = f.addSet(Qt::green);
    SlideshowSettings settings;
    settings.setIds = {b, deleted, a};
    settings.enabled = true;
    QVERIFY(f.slideshow.configure(settings, f.scheduler).success);
    QVERIFY(SetLibrary(f.root).remove(deleted, f.plasma).success);
    const auto added = f.addSet(Qt::yellow);
    SlideshowDialog dialog(nullptr, f.root, f.config, &f.scheduler);
    auto *list = dialog.findChild<WallpaperSetList *>("slideshowSets");
    QCOMPARE(list->topLevelItemCount(), 3);
    QCOMPARE(list->topLevelItem(0)->data(0, Qt::UserRole).toString(), b);
    QCOMPARE(list->topLevelItem(1)->data(0, Qt::UserRole).toString(), a);
    QCOMPARE(list->topLevelItem(2)->data(0, Qt::UserRole).toString(), added);
    QCOMPARE(list->topLevelItem(0)->checkState(0), Qt::Checked);
    QCOMPARE(list->topLevelItem(1)->checkState(0), Qt::Checked);
    QCOMPARE(list->topLevelItem(2)->checkState(0), Qt::Unchecked);
    dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    QCOMPARE(f.settings().setIds, (QStringList{b, a}));
    QVERIFY(f.settings().enabled);
    SlideshowDialog reopened(nullptr, f.root, f.config, &f.scheduler);
    QCOMPARE(reopened.findChild<WallpaperSetList *>("slideshowSets")->topLevelItemCount(), 3);
}

void SlideshowTest::dialogCanSaveEmptyPlaylistAfterAllSetsWereDeleted() {
    Fixture f;
    const auto id = f.addSet(Qt::blue);
    SlideshowSettings settings;
    settings.setIds = {id};
    settings.enabled = true;
    QVERIFY(f.slideshow.configure(settings, f.scheduler).success);
    QVERIFY(SetLibrary(f.root).remove(id, f.plasma).success);
    SlideshowDialog dialog(nullptr, f.root, f.config, &f.scheduler);
    QCOMPARE(dialog.findChild<WallpaperSetList *>("slideshowSets")->topLevelItemCount(), 0);
    QVERIFY(!dialog.findChild<QPushButton *>("slideshowNext")->isEnabled());
    dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    QVERIFY(f.settings().setIds.isEmpty());
    QVERIFY(!f.settings().enabled);
    QVERIFY(!f.scheduler.active);
    QVERIFY(!dialog.findChild<QCheckBox *>("slideshowEnabled")->isChecked());
    QVERIFY(!dialog.findChild<QPushButton *>("slideshowNext")->isEnabled());
}

void SlideshowTest::commandLineRejectsConflictsAndHonorsDisabledState() {
    QTemporaryDir temp;
    const QString executable = QCoreApplication::applicationDirPath() + "/wallpaper_splitter";
    auto run = [&](const QStringList &args) {
        QProcess process;
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("XDG_CONFIG_HOME", temp.filePath("config"));
        env.insert("XDG_DATA_HOME", temp.filePath("data"));
        env.insert("QT_QPA_PLATFORM", "offscreen");
        process.setProcessEnvironment(env);
        process.start(executable, args);
        if (!process.waitForFinished(10000)) return -1;
        return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    };
    QCOMPARE(run({"--slideshow-tick"}), 0);
    QCOMPARE(run({"--slideshow-next", "image.png"}), 2);
    QCOMPARE(run({"--slideshow-next", "--apply"}), 2);
    QCOMPARE(run({"--slideshow-next", "--slideshow-tick"}), 2);
    QVERIFY(run({"--slideshow-next"}) > 0); // No configured playlist, no GUI or hang.
}

void SlideshowTest::dialogKeepsManualPlaybackAvailableAfterSchedulerFailure() {
    Fixture f;
    const auto id = f.addSet(Qt::blue);
    f.scheduler.failEnable = true;
    SlideshowDialog dialog(nullptr, f.root, f.config, &f.scheduler);
    auto *list = dialog.findChild<WallpaperSetList *>("slideshowSets");
    list->topLevelItem(0)->setCheckState(0, Qt::Checked);
    auto *enabled = dialog.findChild<QCheckBox *>("slideshowEnabled");
    enabled->setChecked(true);
    QTimer::singleShot(0, [] {
        if (auto *warning = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            warning->accept();
    });
    dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
    QCOMPARE(f.settings().setIds, QStringList{id});
    QVERIFY(!enabled->isChecked());
    QVERIFY(dialog.findChild<QPushButton *>("slideshowNext")->isEnabled());
    const auto text = dialog.findChild<QLabel *>("slideshowStatus")->text();
    QVERIFY(text.contains("enable failed"));
    QVERIFY(!text.contains("Save changes before"));
}

QTEST_MAIN(SlideshowTest)
#include "slideshow_test.moc"
