#include <QColor>
#include <QDialogButtonBox>
#include <QDir>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QImage>
#include <QGraphicsScene>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QtTest>

#include "centeredtextitem.h"
#include "resizableimageitem.h"
#include "screensitem.h"
#include "wallpapersplitter.h"
#include "appsettings.h"
#include "outputservice.h"
#include "settingsdialog.h"

namespace {
class FakePlasmaApplicator final : public PlasmaApplicator {
public:
    explicit FakePlasmaApplicator(bool succeeds) : succeeds(succeeds) {}

    OperationResult apply(const QList<ScreenCrop> &screens,
                          const QStringList &paths) override {
        called = true;
        receivedScreens = screens;
        receivedPaths = paths;
        return succeeds ? OperationResult::ok(paths)
                        : OperationResult::failure(OperationError::Plasma,
                                                   QStringLiteral("simulated failure"));
    }

    bool succeeds;
    bool called{false};
    QList<ScreenCrop> receivedScreens;
    QStringList receivedPaths;
};
}

class SplitImageTest : public QObject {
    Q_OBJECT

private slots:
    void preservesRectangleOrderAndPixels();
    void changesPathsWhenCropContentChanges();
    void movesTranslatedLayoutInsideSource();
    void resizeStretchesAndKeepsOppositeCorner();
    void resizeKeepsAspectRatioWithShift();
    void resizeStopsAtOppositeCorner();
    void resizeCannotExposeMonitorLayout();
    void screenControlsAcceptMoveAndScaleButtons();
    void emptyStateRemainsCenteredWhenWindowResizes();
    void imageRemainsFullyVisibleAcrossWindowResizes();
    void footerControlsStayGroupedWhenWindowWidens();
    void monitorLabelRemainsCenteredAtDifferentZoomLevels();
    void validatesFilenameTemplates();
    void handlesSetWideExportCollisions();
    void persistsOutputSettings();
    void outputSettingsDialogHasRoomyDefaultSize();
    void migratesLegacySettingsToNeutralNamespace();
    void writesManagedManifestAndRetainsFailures();
    void reusesIdenticalManagedSet();
    void plasmaScriptUsesDesktopGeometry();
    void commandLineExportsAndRejectsConflicts();
};

void SplitImageTest::preservesRectangleOrderAndPixels() {
    QImage source(30, 10, QImage::Format_RGB32);
    source.fill(Qt::black);
    for (int x = 0; x < 10; ++x) {
        for (int y = 0; y < source.height(); ++y) {
            source.setPixelColor(x, y, Qt::red);
            source.setPixelColor(x + 10, y, Qt::green);
            source.setPixelColor(x + 20, y, Qt::blue);
        }
    }

    QTemporaryDir outputDirectory;
    QVERIFY(outputDirectory.isValid());

    // Deliberately request middle, left, right to prove output order follows
    // the supplied screen list rather than geometry sorting.
    const QList<QRect> screens{
            QRect(10, 0, 10, 10),
            QRect(0, 0, 10, 10),
            QRect(20, 0, 10, 10),
    };
    const QStringList paths = WallpaperSplitter::splitImage(
            source, screens, outputDirectory.path(), "example.image.jpg");

    QCOMPARE(paths.size(), 3);
    const QList<QColor> expected{Qt::green, Qt::red, Qt::blue};
    for (int index = 0; index < paths.size(); ++index) {
        const QString fileName = QFileInfo(paths.at(index)).fileName();
        QVERIFY(QRegularExpression(
                QStringLiteral("^example\\.image-%1-[0-9a-f]{16}\\.png$")
                        .arg(index + 1)).match(fileName).hasMatch());
        QCOMPARE(QFileInfo(paths.at(index)).absolutePath(), outputDirectory.path());
        const QImage crop(paths.at(index));
        QCOMPARE(crop.size(), QSize(10, 10));
        QCOMPARE(crop.pixelColor(5, 5), expected.at(index));
    }
}

void SplitImageTest::changesPathsWhenCropContentChanges() {
    QImage firstSource(20, 10, QImage::Format_RGB32);
    firstSource.fill(Qt::red);
    QImage secondSource(20, 10, QImage::Format_RGB32);
    secondSource.fill(Qt::blue);

    QTemporaryDir outputDirectory;
    QVERIFY(outputDirectory.isValid());
    const QList<QRect> screens{QRect(0, 0, 10, 10), QRect(10, 0, 10, 10)};

    const QStringList firstPaths = WallpaperSplitter::splitImage(
            firstSource, screens, outputDirectory.path());
    const QStringList repeatedPaths = WallpaperSplitter::splitImage(
            firstSource, screens, outputDirectory.path());
    const QStringList secondPaths = WallpaperSplitter::splitImage(
            secondSource, screens, outputDirectory.path());

    QCOMPARE(firstPaths, repeatedPaths);
    QCOMPARE(firstPaths.size(), secondPaths.size());
    for (int index = 0; index < firstPaths.size(); ++index) {
        QVERIFY(firstPaths.at(index) != secondPaths.at(index));
        QVERIFY(QFileInfo::exists(firstPaths.at(index)));
        QVERIFY(QFileInfo::exists(secondPaths.at(index)));
    }
}

void SplitImageTest::movesTranslatedLayoutInsideSource() {
    QImage source(30, 10, QImage::Format_RGB32);
    source.fill(Qt::black);
    for (int x = 0; x < 10; ++x) {
        for (int y = 0; y < source.height(); ++y) {
            source.setPixelColor(x, y, Qt::red);
            source.setPixelColor(x + 10, y, Qt::green);
            source.setPixelColor(x + 20, y, Qt::blue);
        }
    }

    QTemporaryDir outputDirectory;
    QVERIFY(outputDirectory.isValid());

    // The complete layout has the same size as the source but is translated
    // beyond its bottom-right edge, matching the live 5760x1080 failure.
    const QList<QRect> translatedScreens{
            QRect(5, 5, 10, 10),
            QRect(15, 5, 10, 10),
            QRect(25, 5, 10, 10),
    };
    const QStringList paths = WallpaperSplitter::splitImage(
            source, translatedScreens, outputDirectory.path());

    QCOMPARE(paths.size(), 3);
    const QList<QColor> expected{Qt::red, Qt::green, Qt::blue};
    for (int index = 0; index < paths.size(); ++index) {
        const QImage crop(paths.at(index));
        QCOMPARE(crop.size(), QSize(10, 10));
        QCOMPARE(crop.pixelColor(5, 5), expected.at(index));
    }
}

void SplitImageTest::resizeStretchesAndKeepsOppositeCorner() {
    QImage source(100, 50, QImage::Format_RGB32);
    source.fill(Qt::red);
    QGraphicsScene scene;
    auto *image = new ResizableImageItem(source);
    scene.addItem(image);

    const QPointF fixedCorner = image->mapToScene(image->boundingRect().bottomRight());
    image->beginResize(ResizableImageItem::Corner::TopLeft);
    image->resizeTo(QPointF(-100, -25));

    QCOMPARE(image->image().size(), QSize(200, 75));
    QCOMPARE(image->mapToScene(image->boundingRect().bottomRight()), fixedCorner);
}

void SplitImageTest::resizeKeepsAspectRatioWithShift() {
    QImage source(100, 50, QImage::Format_RGB32);
    source.fill(Qt::red);
    QGraphicsScene scene;
    auto *image = new ResizableImageItem(source);
    scene.addItem(image);

    image->beginResize(ResizableImageItem::Corner::TopLeft);
    image->resizeTo(QPointF(-100, -25), true);

    QCOMPARE(image->image().size(), QSize(200, 100));
}

void SplitImageTest::resizeStopsAtOppositeCorner() {
    QImage source(100, 50, QImage::Format_RGB32);
    source.fill(Qt::red);
    QGraphicsScene scene;
    auto *image = new ResizableImageItem(source);
    scene.addItem(image);

    const QPointF fixedCorner = image->mapToScene(image->boundingRect().bottomRight());
    image->beginResize(ResizableImageItem::Corner::TopLeft);
    image->resizeTo(fixedCorner + QPointF(50, 25));

    QCOMPARE(image->image().size(), QSize(1, 1));
    QCOMPARE(image->mapToScene(image->boundingRect().bottomRight()), fixedCorner);
}

void SplitImageTest::resizeCannotExposeMonitorLayout() {
    QImage source(100, 50, QImage::Format_RGB32);
    source.fill(Qt::red);
    QGraphicsScene scene;
    auto *image = new ResizableImageItem(source);
    scene.addItem(image);
    auto *screens = new ScreensItem(image);
    image->setScreenGroup(screens);

    const QSize requiredSize = screens->mapRectToParent(screens->boundingRect()).size().toSize();
    image->beginResize(ResizableImageItem::Corner::TopLeft);
    image->resizeTo(image->mapToScene(image->boundingRect().bottomRight()));

    QVERIFY(image->image().width() >= requiredSize.width());
    QVERIFY(image->image().height() >= requiredSize.height());
    QVERIFY(image->boundingRect().contains(screens->mapRectToParent(screens->boundingRect())));
}

void SplitImageTest::screenControlsAcceptMoveAndScaleButtons() {
    QImage source(100, 50, QImage::Format_RGB32);
    QGraphicsScene scene;
    auto *image = new ResizableImageItem(source);
    scene.addItem(image);
    auto *screens = new ScreensItem(image);

    QVERIFY(screens->acceptedMouseButtons().testFlag(Qt::LeftButton));
    QVERIFY(screens->acceptedMouseButtons().testFlag(Qt::RightButton));
}

void SplitImageTest::emptyStateRemainsCenteredWhenWindowResizes() {
    WallpaperSplitter splitter;
    splitter.resize(400, 300);
    splitter.show();
    QApplication::processEvents();

    auto *view = splitter.findChild<QGraphicsView *>();
    QVERIFY(view != nullptr);
    auto *label = qgraphicsitem_cast<QGraphicsTextItem *>(view->scene()->items().constFirst());
    QVERIFY(label != nullptr);

    splitter.resize(800, 600);
    QApplication::processEvents();

    const QPoint labelCenter = view->mapFromScene(label->sceneBoundingRect().center());
    const QPoint viewportCenter = view->viewport()->rect().center();
    QVERIFY(qAbs(labelCenter.x() - viewportCenter.x()) <= 1);
    QVERIFY(qAbs(labelCenter.y() - viewportCenter.y()) <= 1);
}

void SplitImageTest::imageRemainsFullyVisibleAcrossWindowResizes() {
    WallpaperSplitter splitter;
    QImage image(1920, 1200, QImage::Format_RGB32);
    image.fill(Qt::black);
    splitter.addImage(image);
    splitter.show();

    auto *view = splitter.findChild<QGraphicsView *>();
    QVERIFY(view != nullptr);

    const QList<QSize> windowSizes{
            QSize(420, 340), QSize(530, 417), QSize(650, 480), QSize(900, 650)};
    for (const QSize &windowSize : windowSizes) {
        splitter.resize(windowSize);
        QApplication::processEvents();

        QVERIFY(!view->horizontalScrollBar()->isVisible());
        QVERIFY(!view->verticalScrollBar()->isVisible());

        const QRect mappedSceneBounds =
                view->mapFromScene(view->scene()->itemsBoundingRect()).boundingRect();
        const QRect viewportBounds = view->viewport()->rect().adjusted(-1, -1, 1, 1);
        QVERIFY2(viewportBounds.contains(mappedSceneBounds),
                 qPrintable(QStringLiteral("Scene %1,%2 %3x%4 is outside viewport %5x%6 at window %7x%8")
                                    .arg(mappedSceneBounds.x())
                                    .arg(mappedSceneBounds.y())
                                    .arg(mappedSceneBounds.width())
                                    .arg(mappedSceneBounds.height())
                                    .arg(view->viewport()->width())
                                    .arg(view->viewport()->height())
                                    .arg(windowSize.width())
                                    .arg(windowSize.height())));
    }
}

void SplitImageTest::footerControlsStayGroupedWhenWindowWidens() {
    WallpaperSplitter splitter;
    splitter.resize(1200, 700);
    splitter.show();
    QApplication::processEvents();

    auto *openBox = splitter.findChild<QDialogButtonBox *>(QStringLiteral("buttonBoxOpen"));
    auto *settings = splitter.findChild<QPushButton *>(QStringLiteral("settingsButton"));
    auto *actions = splitter.findChild<QDialogButtonBox *>(QStringLiteral("buttonBox"));
    QVERIFY(openBox != nullptr);
    QVERIFY(settings != nullptr);
    QVERIFY(actions != nullptr);

    const auto *openButton = openBox->button(QDialogButtonBox::Open);
    QVERIFY(openButton != nullptr);
    QVERIFY(openBox->width() <= openBox->sizeHint().width());
    QVERIFY(settings->geometry().left() - openBox->geometry().right() <= 12);
    QVERIFY(openButton->geometry().left() <= 1);
    QVERIFY(splitter.width() - actions->geometry().right() <= 20);
}

void SplitImageTest::monitorLabelRemainsCenteredAtDifferentZoomLevels() {
    QGraphicsScene scene;
    auto *group = new QGraphicsItemGroup();
    scene.addItem(group);
    auto *screen = new QGraphicsRectItem(QRectF(0, 0, 1920, 1080));
    group->addToGroup(screen);
    auto *label = new CenteredTextItem(QStringLiteral("PHL 241E1"),
                                       screen->rect().center());
    label->setDefaultTextColor(Qt::black);
    group->addToGroup(label);

    QGraphicsView view(&scene);
    view.setBackgroundBrush(Qt::white);
    view.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view.show();

    for (const QSize &viewSize : {QSize(400, 260), QSize(600, 400), QSize(1000, 700)}) {
        view.resize(viewSize);
        view.fitInView(screen->sceneBoundingRect(), Qt::KeepAspectRatio);
        QApplication::processEvents();

        const QRectF screenInView = screen->deviceTransform(view.viewportTransform())
                                           .mapRect(screen->boundingRect());
        const QRectF labelInView = label->deviceTransform(view.viewportTransform())
                                          .mapRect(label->boundingRect());
        QVERIFY(qAbs(screenInView.center().x() - labelInView.center().x()) <= 1.0);
        QVERIFY(qAbs(screenInView.center().y() - labelInView.center().y()) <= 1.0);

        QImage rendered(view.viewport()->size(), QImage::Format_RGB32);
        rendered.fill(Qt::white);
        QPainter painter(&rendered);
        view.viewport()->render(&painter);
        painter.end();

        const QRect sample = labelInView.toAlignedRect().intersected(rendered.rect());
        int firstTextRow = sample.bottom();
        int lastTextRow = sample.top();
        for (int y = sample.top(); y <= sample.bottom(); ++y) {
            for (int x = sample.left(); x <= sample.right(); ++x) {
                if (qGray(rendered.pixel(x, y)) < 128) {
                    firstTextRow = qMin(firstTextRow, y);
                    lastTextRow = qMax(lastTextRow, y);
                }
            }
        }
        QVERIFY2(lastTextRow - firstTextRow >= 6,
                 "The monitor label was clipped vertically");
    }
}

void SplitImageTest::validatesFilenameTemplates() {
    QVERIFY(OutputService::validateFileNameTemplate(
            QStringLiteral("{source}-{screen}-{number}-{revision}-{digest}")).success);
    QCOMPARE(OutputService::sanitizeFileComponent(QStringLiteral("DP/1:*?")),
             QStringLiteral("DP_1___"));
    QVERIFY(!OutputService::validateFileNameTemplate(QString()).success);
    QVERIFY(!OutputService::validateFileNameTemplate(QStringLiteral("../{source}")).success);
    QVERIFY(!OutputService::validateFileNameTemplate(QStringLiteral("{unknown}-{number}")).success);
    QVERIFY(!OutputService::validateFileNameTemplate(QStringLiteral("{source}")).success);
}

void SplitImageTest::handlesSetWideExportCollisions() {
    QImage source(20, 10, QImage::Format_RGB32);
    source.fill(Qt::blue);
    const QList<ScreenCrop> screens{
            {QStringLiteral("DP-1"), 1, QRect(0, 0, 10, 10), QRect(0, 0, 10, 10)},
            {QStringLiteral("HDMI-1"), 2, QRect(10, 0, 10, 10), QRect(10, 0, 10, 10)}};
    QTemporaryDir output;
    QVERIFY(output.isValid());
    ExportOptions options{output.path(), QStringLiteral("{source}-{number}"),
                          CollisionPolicy::Fail};

    OperationResult result = OutputService::exportCrops(source, screens,
                                                        QStringLiteral("lake.jpg"), options);
    QVERIFY2(result.success, qPrintable(result.message));
    QCOMPARE(QFileInfo(result.paths.at(0)).fileName(), QStringLiteral("lake-1.png"));
    result = OutputService::exportCrops(source, screens, QStringLiteral("lake.jpg"), options);
    QCOMPARE(result.error, OperationError::Collision);
    options.collisionPolicy = CollisionPolicy::Ask;
    result = OutputService::exportCrops(source, screens, QStringLiteral("lake.jpg"), options);
    QCOMPARE(result.error, OperationError::Collision);

    options.collisionPolicy = CollisionPolicy::Replace;
    QVERIFY(OutputService::exportCrops(source, screens, QStringLiteral("lake.jpg"), options).success);
    QFile unrelated(output.filePath(QStringLiteral("notes.txt")));
    QVERIFY(unrelated.open(QIODevice::WriteOnly));
    unrelated.write("keep");
    unrelated.close();

    options.collisionPolicy = CollisionPolicy::Revision;
    result = OutputService::exportCrops(source, screens, QStringLiteral("lake.jpg"), options);
    QVERIFY2(result.success, qPrintable(result.message));
    QCOMPARE(QFileInfo(result.paths.at(0)).fileName(), QStringLiteral("lake-1-r2.png"));
    QVERIFY(QFileInfo::exists(output.filePath(QStringLiteral("notes.txt"))));

    options.fileNameTemplate = QStringLiteral("{source}{revision}-{number}");
    result = OutputService::exportCrops(source, screens, QStringLiteral("lake.jpg"), options);
    QVERIFY2(result.success, qPrintable(result.message));
    QCOMPARE(QFileInfo(result.paths.at(0)).fileName(), QStringLiteral("lake-r2-1.png"));
}

void SplitImageTest::persistsOutputSettings() {
    QTemporaryDir settingsDirectory;
    QVERIFY(settingsDirectory.isValid());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope,
                       settingsDirectory.path());
    AppSettings::reset();
    const UserPreferences defaults = AppSettings::load();
    QCOMPARE(defaults.fileNameTemplate, QStringLiteral("{source}-{number}"));
    QCOMPARE(defaults.collisionPolicy, CollisionPolicy::Ask);
    const UserPreferences original = AppSettings::load();
    UserPreferences expected;
    expected.inputDirectory = QStringLiteral("/tmp/input-wallpapers");
    expected.exportDirectory = QStringLiteral("/tmp/export-wallpapers");
    expected.fileNameTemplate = QStringLiteral("{screen}-{number}{revision}");
    expected.collisionPolicy = CollisionPolicy::Revision;
    AppSettings::save(expected);

    const UserPreferences actual = AppSettings::load();
    QCOMPARE(actual.inputDirectory, expected.inputDirectory);
    QCOMPARE(actual.exportDirectory, expected.exportDirectory);
    QCOMPARE(actual.fileNameTemplate, expected.fileNameTemplate);
    QCOMPARE(actual.collisionPolicy, expected.collisionPolicy);
    AppSettings::save(original);
}

void SplitImageTest::outputSettingsDialogHasRoomyDefaultSize() {
    SettingsDialog dialog(UserPreferences{});
    QCOMPARE(dialog.size(), QSize(650, 360));
    QCOMPARE(dialog.minimumSize(), QSize(560, 300));
}

void SplitImageTest::migratesLegacySettingsToNeutralNamespace() {
    QTemporaryDir settingsDirectory;
    QVERIFY(settingsDirectory.isValid());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope,
                       settingsDirectory.path());

    QSettings current(QSettings::NativeFormat, QSettings::UserScope,
                      QStringLiteral("wallpaper-splitter"), QStringLiteral("settings"));
    current.clear();
    QSettings legacy(QSettings::NativeFormat, QSettings::UserScope,
                     QStringLiteral("kstorbakken"), QStringLiteral("Wallpaper Splitter"));
    legacy.clear();
    legacy.setValue(QStringLiteral("folders/input"), QStringLiteral("/legacy/input"));
    legacy.setValue(QStringLiteral("folders/export"), QStringLiteral("/legacy/export"));
    legacy.setValue(QStringLiteral("output/fileNameTemplate"), QStringLiteral("{screen}-{number}"));
    legacy.setValue(QStringLiteral("output/collisionPolicy"), QStringLiteral("revision"));
    legacy.sync();

    const UserPreferences migrated = AppSettings::load();
    QCOMPARE(migrated.inputDirectory, QStringLiteral("/legacy/input"));
    QCOMPARE(migrated.exportDirectory, QStringLiteral("/legacy/export"));
    QCOMPARE(migrated.fileNameTemplate, QStringLiteral("{screen}-{number}"));
    QCOMPARE(migrated.collisionPolicy, CollisionPolicy::Revision);
    QVERIFY(current.fileName().endsWith(QStringLiteral("wallpaper-splitter/settings.conf")));

    AppSettings::reset();
    legacy.clear();
}

void SplitImageTest::writesManagedManifestAndRetainsFailures() {
    QImage source(20, 10, QImage::Format_RGB32);
    source.fill(Qt::green);
    const QList<ScreenCrop> screens{
            {QStringLiteral("DP-1"), 1, QRect(-10, 0, 10, 10), QRect(0, 0, 10, 10)},
            {QStringLiteral("HDMI-1"), 2, QRect(0, 0, 10, 10), QRect(10, 0, 10, 10)}};
    QTemporaryDir managedRoot;
    QVERIFY(managedRoot.isValid());

    FakePlasmaApplicator success(true);
    OperationResult result = OutputService::applyManaged(
            source, source.size(), screens, QStringLiteral("forest.png"),
            QStringLiteral("/pictures/forest.png"), success, managedRoot.path());
    QVERIFY2(result.success, qPrintable(result.message));
    QVERIFY(success.called);
    QVERIFY(QFileInfo::exists(result.manifestPath));
    QFile manifestFile(result.manifestPath);
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    QCOMPARE(manifest.value(QStringLiteral("schemaVersion")).toInt(), 1);
    QCOMPARE(manifest.value(QStringLiteral("status")).toString(), QStringLiteral("applied"));
    QCOMPARE(manifest.value(QStringLiteral("sourcePath")).toString(),
             QStringLiteral("/pictures/forest.png"));
    QCOMPARE(manifest.value(QStringLiteral("crops")).toArray().size(), 2);
    manifestFile.close();

    FakePlasmaApplicator failure(false);
    result = OutputService::applyManaged(
            source, source.size(), screens, QStringLiteral("forest.png"), {}, failure,
            managedRoot.path());
    QVERIFY(!result.success);
    QCOMPARE(result.error, OperationError::Plasma);
    QVERIFY(QFileInfo::exists(result.manifestPath));
    manifestFile.setFileName(result.manifestPath);
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    QCOMPARE(manifest.value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
    QCOMPARE(manifest.value(QStringLiteral("error")).toString(),
             QStringLiteral("simulated failure"));
    for (const QString &path : result.paths) QVERIFY(QFileInfo::exists(path));
}

void SplitImageTest::reusesIdenticalManagedSet() {
    QImage source(20, 10, QImage::Format_RGB32);
    source.fill(Qt::green);
    const QList<ScreenCrop> screens{
            {QStringLiteral("DP-1"), 1, QRect(-10, 0, 10, 10), QRect(0, 0, 10, 10)},
            {QStringLiteral("HDMI-1"), 2, QRect(0, 0, 10, 10), QRect(10, 0, 10, 10)}};
    QTemporaryDir managedRoot;
    QVERIFY(managedRoot.isValid());

    FakePlasmaApplicator applicator(true);
    const OperationResult first = OutputService::applyManaged(
            source, source.size(), screens, QStringLiteral("forest.png"),
            QStringLiteral("/pictures/forest.png"), applicator, managedRoot.path());
    QVERIFY2(first.success, qPrintable(first.message));
    const OperationResult repeated = OutputService::applyManaged(
            source, source.size(), screens, QStringLiteral("forest.png"),
            QStringLiteral("/pictures/forest.png"), applicator, managedRoot.path());
    QVERIFY2(repeated.success, qPrintable(repeated.message));

    QCOMPARE(repeated.manifestPath, first.manifestPath);
    QCOMPARE(repeated.paths, first.paths);
    QCOMPARE(QDir(managedRoot.path()).entryList(
                     QDir::Dirs | QDir::NoDotAndDotDot).size(), 1);

    source.fill(Qt::blue);
    const OperationResult changed = OutputService::applyManaged(
            source, source.size(), screens, QStringLiteral("forest.png"),
            QStringLiteral("/pictures/forest.png"), applicator, managedRoot.path());
    QVERIFY2(changed.success, qPrintable(changed.message));
    QVERIFY(changed.manifestPath != first.manifestPath);
    QCOMPARE(QDir(managedRoot.path()).entryList(
                     QDir::Dirs | QDir::NoDotAndDotDot).size(), 2);
}

void SplitImageTest::plasmaScriptUsesDesktopGeometry() {
    const QList<ScreenCrop> screens{
            {QStringLiteral("DP-1"), 1, QRect(-1920, 0, 1920, 1080), QRect(0, 0, 1, 1)},
            {QStringLiteral("HDMI-1"), 2, QRect(0, -200, 2560, 1440), QRect(1, 0, 1, 1)}};
    const QString script = DBusPlasmaApplicator::buildScript(
            screens, {QStringLiteral("/tmp/left.png"), QStringLiteral("/tmp/right.png")});
    QVERIFY(script.contains(QStringLiteral("\"x\":-1920")));
    QVERIFY(script.contains(QStringLiteral("\"y\":-200")));
    QVERIFY(script.contains(QStringLiteral("sameGeometry")));
    QVERIFY(script.contains(QStringLiteral("/tmp/right.png")));
    QVERIFY(script.contains(QStringLiteral("WALLPAPER_SPLITTER_RESULT")));
}

void SplitImageTest::commandLineExportsAndRejectsConflicts() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString sourcePath = temporary.filePath(QStringLiteral("cli-source.png"));
    QImage source(1200, 900, QImage::Format_RGB32);
    source.fill(Qt::cyan);
    QVERIFY(source.save(sourcePath));
    const QString destination = temporary.filePath(QStringLiteral("output"));
    const QString executable = QCoreApplication::applicationDirPath()
            + QStringLiteral("/wallpaper_splitter");

    QProcess process;
    process.setProgram(executable);
    process.setArguments({QStringLiteral("--destination"), destination,
                          QStringLiteral("--filename-template"), QStringLiteral("{source}-{number}"),
                          QStringLiteral("--collision"), QStringLiteral("replace"), sourcePath});
    process.start();
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitCode(), 0);
    QVERIFY2(!process.readAllStandardOutput().isEmpty(),
             qPrintable(QString::fromUtf8(process.readAllStandardError())));
    QVERIFY(QFileInfo::exists(QDir(destination).filePath(QStringLiteral("cli-source-1.png"))));

    process.setArguments({QStringLiteral("--apply"), QStringLiteral("--destination"), destination,
                          sourcePath});
    process.start();
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitCode(), 2);
}

QTEST_MAIN(SplitImageTest)
#include "splitimage_test.moc"
