#include <QColor>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <limits>
#include <cmath>
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
#include <QJsonArray>
#include <QListWidget>
#include <QJsonObject>
#include <QSettings>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QtTest>

#include "centeredtextitem.h"
#include "graphicsview.h"
#include "resizableimageitem.h"
#include "screensitem.h"
#include "wallpapersplitter.h"
#include "appsettings.h"
#include "outputservice.h"
#include "settingsdialog.h"
#include "setlibrary.h"
#include "setlibrarydialog.h"
#include "monitorlayout.h"
#include "monitorsdialog.h"

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

    OperationResult wallpaperReferences() override { return references; }
    OperationResult references = OperationResult::failure(OperationError::Plasma, "unavailable");
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
    void middleButtonPansPreview_data();
    void middleButtonPansPreview();
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
    void libraryReappliesAndPreservesMetadata();
    void libraryRejectsUnsafeDeletion();
    void libraryHandlesBrokenSets();
    void libraryDialogShowsSets();
    void physicalLayoutCompensatesForPixelDensity();
    void physicalMeasurementsHandleRotationAndInvalidData();
    void physicalMeasurementsPersistSeparately();
    void physicalDialogEditsDimensionsAndAlignment();
    void physicalLayoutFitsSmallImages();
    void commandLineUsesPhysicalMeasurements();
    void largestLayoutFit_data();
    void largestLayoutFit();
    void largestFitButtonUsesImageEdges();
    void stretchSpansOriginalImageAcrossScreens();
    void commandLineLargestFit();
    void panoramaPreservesProportionsAndContinuity();
    void panoramaActionRestoresOriginalAndSwitchesBack();
    void layoutButtonsToggleBackToLoadedState();
    void panoramaPreviewKeepsThreeMonitorsSeparate();
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

    const QSize requiredSize = screens->mapRectToParent(screens->layoutBounds()).size().toSize();
    image->beginResize(ResizableImageItem::Corner::TopLeft);
    image->resizeTo(image->mapToScene(image->boundingRect().bottomRight()));

    QVERIFY(image->image().width() >= requiredSize.width());
    QVERIFY(image->image().height() >= requiredSize.height());
    QVERIFY(image->boundingRect().contains(screens->mapRectToParent(screens->layoutBounds())));
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

void SplitImageTest::middleButtonPansPreview_data() {
    QTest::addColumn<qreal>("zoom");
    QTest::newRow("zoomed-out") << qreal(0.5);
    QTest::newRow("native") << qreal(1);
    QTest::newRow("zoomed-in") << qreal(2);
}

void SplitImageTest::middleButtonPansPreview() {
    QFETCH(qreal, zoom);
    QGraphicsScene scene;
    // A translated scene catches accidental use of scene coordinates as deltas.
    auto *item = scene.addRect(QRectF(-2000, -1000, 4000, 3000));
    item->setFlag(QGraphicsItem::ItemIsMovable);
    GraphicsView view;
    view.setScene(&scene);
    view.resize(400, 300);
    view.scale(zoom, zoom);
    view.show();
    QApplication::processEvents();
    view.centerOn(100, 200);

    QWidget *viewport = view.viewport();
    const QPoint start = viewport->rect().center();
    const QPointF scenePoint = view.mapToScene(start);
    const auto originalTransform = view.transform();
    const auto originalAnchor = view.transformationAnchor();
    const auto originalCursor = view.cursor().shape();
    auto move = [&](const QPoint &position, Qt::MouseButtons buttons) {
        QMouseEvent event(QEvent::MouseMove, QPointF(position),
                          QPointF(viewport->mapToGlobal(position)),
                          Qt::NoButton, buttons, Qt::NoModifier);
        QApplication::sendEvent(viewport, &event);
    };

    QTest::mousePress(viewport, Qt::MiddleButton, Qt::NoModifier, start);
    QCOMPARE(view.cursor().shape(), Qt::ClosedHandCursor);
    const QPoint first = start + QPoint(35, -20);
    move(first, Qt::MiddleButton);
    QCOMPARE(view.mapFromScene(scenePoint), first);
    const QPoint second = first + QPoint(-10, 30);
    move(second, Qt::MiddleButton);
    QCOMPARE(view.mapFromScene(scenePoint), second);
    QCOMPARE(item->pos(), QPointF());
    QCOMPARE(view.transform(), originalTransform);
    QCOMPARE(view.transformationAnchor(), originalAnchor);

    // Hitting the scene edge must not create a dead zone when reversing.
    view.horizontalScrollBar()->setValue(view.horizontalScrollBar()->minimum());
    move(second + QPoint(10, 0), Qt::MiddleButton);
    QCOMPARE(view.horizontalScrollBar()->value(), view.horizontalScrollBar()->minimum());
    move(second + QPoint(5, 0), Qt::MiddleButton);
    QCOMPARE(view.horizontalScrollBar()->value(), view.horizontalScrollBar()->minimum() + 5);

    QTest::mouseRelease(viewport, Qt::MiddleButton, Qt::NoModifier, second + QPoint(5, 0));
    QCOMPARE(view.cursor().shape(), originalCursor);
    const QPointF centerAfterRelease = view.mapToScene(start);
    move(start, Qt::NoButton);
    QCOMPARE(view.mapToScene(start), centerAfterRelease);
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
    auto *library = splitter.findChild<QPushButton *>(QStringLiteral("libraryButton"));

    auto *view = splitter.findChild<QGraphicsView *>();
    auto *exportButton = splitter.findChild<QPushButton *>(QStringLiteral("exportButton"));
    QVERIFY(library && view && exportButton);
    QVERIFY(library->geometry().left() - openBox->geometry().right() <= 12);
    QVERIFY(settings->geometry().left() - library->geometry().right() <= 12);
    QVERIFY(openBox->geometry().top() > view->geometry().bottom());
    QVERIFY(actions->geometry().top() > view->geometry().bottom());
    QVERIFY(exportButton->geometry().right() < actions->geometry().left());
    QVERIFY(actions->geometry().left() - exportButton->geometry().right() <= 12);
    QVERIFY(settings->menu() != nullptr);
    QVERIFY(actions->button(QDialogButtonBox::Cancel) == nullptr);
    QVERIFY(actions->button(QDialogButtonBox::Ok)->isDefault());
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
    QVERIFY(defaults.closeAfterApply);
    QCOMPARE(defaults.fileNameTemplate, QStringLiteral("{source}-{number}"));
    QCOMPARE(defaults.collisionPolicy, CollisionPolicy::Ask);
    const UserPreferences original = AppSettings::load();
    UserPreferences expected;
    expected.closeAfterApply = false;
    expected.inputDirectory = QStringLiteral("/tmp/input-wallpapers");
    expected.exportDirectory = QStringLiteral("/tmp/export-wallpapers");
    expected.fileNameTemplate = QStringLiteral("{screen}-{number}{revision}");
    expected.collisionPolicy = CollisionPolicy::Revision;
    AppSettings::save(expected);

    const UserPreferences actual = AppSettings::load();
    QVERIFY(!actual.closeAfterApply);
    QCOMPARE(actual.inputDirectory, expected.inputDirectory);
    QCOMPARE(actual.exportDirectory, expected.exportDirectory);
    QCOMPARE(actual.fileNameTemplate, expected.fileNameTemplate);
    QCOMPARE(actual.collisionPolicy, expected.collisionPolicy);
    AppSettings::reset();
    QVERIFY(AppSettings::load().closeAfterApply);
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

void SplitImageTest::libraryReappliesAndPreservesMetadata() {
    QTemporaryDir root;
    QImage image(20, 10, QImage::Format_RGB32);
    image.fill(Qt::red);
    const QList<ScreenCrop> screens{{"left", 1, QRect(-10, 0, 10, 10), QRect(0, 0, 10, 10)},
                                   {"right", 2, QRect(0, 0, 10, 10), QRect(10, 0, 10, 10)}};
    FakePlasmaApplicator plasma(true);
    QVERIFY(OutputService::applyManaged(image, image.size(), screens, "red.png", "/missing/red.png",
                                        plasma, root.path()).success);
    SetLibrary library(root.path());
    auto set = library.sets().first();
    const auto createdAt = set.manifest.value("createdAt");
    QVERIFY(library.rename(set.id, "Evening").success);
    QVERIFY(!library.rename(set.id, "  ").success);
    QVERIFY(OutputService::applyManaged(image, image.size(), screens, "red.png", "/missing/red.png",
                                        plasma, root.path()).success);
    set = library.sets().first();
    QCOMPARE(set.name, "Evening");
    QCOMPARE(set.manifest.value("createdAt"), createdAt);
    QTemporaryDir exportDirectory;
    ExportOptions exportOptions{exportDirectory.path(), "{source}-{number}", CollisionPolicy::Ask};
    auto exported = library.exportSet(set.id, exportOptions);
    QVERIFY2(exported.success, qPrintable(exported.message));
    QCOMPARE(exported.paths.size(), 2);
    QCOMPARE(QFileInfo(exported.paths[0]).fileName(), "red-1.png");
    QCOMPARE(QImage(exported.paths[0]).pixelColor(5, 5), QColor(Qt::red));
    QVERIFY(!library.exportSet(set.id, exportOptions).success);
    exportOptions.collisionPolicy = CollisionPolicy::Revision;
    exported = library.exportSet(set.id, exportOptions);
    QVERIFY(exported.success);
    QCOMPARE(QFileInfo(exported.paths[0]).fileName(), "red-1-r2.png");
    plasma.called = false;
    QVERIFY(!library.reapply(set.id, {QRect(0, 0, 20, 10)}, plasma).success);
    QVERIFY(!plasma.called);
    QVERIFY(library.reapply(set.id, {screens[1].desktopGeometry, screens[0].desktopGeometry}, plasma).success);
    QCOMPARE(plasma.receivedPaths, set.paths);
    const auto preview = SetLibrary::preview(set, QSize(200, 100));
    QCOMPARE(preview.pixelColor(50, 50), QColor(Qt::red));
    plasma.succeeds = false;
    QVERIFY(!library.reapply(set.id, {screens[0].desktopGeometry, screens[1].desktopGeometry}, plasma).success);
    QCOMPARE(library.sets().first().manifest.value("status").toString(), "failed");
    QVERIFY(QFile::remove(set.paths.first()));
    plasma.called = false;
    QVERIFY(!library.reapply(set.id, {screens[0].desktopGeometry, screens[1].desktopGeometry}, plasma).success);
    QVERIFY(!plasma.called);
}

void SplitImageTest::libraryRejectsUnsafeDeletion() {
    QTemporaryDir root;
    QImage image(10, 10, QImage::Format_RGB32);
    image.fill(Qt::blue);
    FakePlasmaApplicator plasma(true);
    const auto applied = OutputService::applyManaged(image, image.size(),
        {{"screen", 1, image.rect(), image.rect()}}, "blue.png", {}, plasma, root.path());
    QVERIFY(applied.success);
    SetLibrary library(root.path());
    const auto set = library.sets().first();
    QVERIFY(!library.remove(set.id, plasma).success); // Unknown Plasma state fails closed.
    QVERIFY(QFileInfo::exists(applied.paths.first()));
    plasma.references = OperationResult::ok({QUrl::fromLocalFile(applied.paths.first()).toString()});
    QVERIFY(!library.remove(set.id, plasma).success);
    plasma.references = OperationResult::ok({root.path()}); // Slideshow parent directory.
    QVERIFY(!library.remove(set.id, plasma).success);
    plasma.references = OperationResult::ok();
    QFile extra(QDir(set.directory).filePath("user-export.png"));
    QVERIFY(extra.open(QIODevice::WriteOnly));
    extra.write("keep me");
    extra.close();
    QVERIFY(!library.remove(set.id, plasma).success);
    QVERIFY(extra.remove());
    QVERIFY(library.remove(set.id, plasma).success);
    QVERIFY(!QFileInfo::exists(set.directory));
}

void SplitImageTest::libraryHandlesBrokenSets() {
    QTemporaryDir root;
    QImage image(10, 10, QImage::Format_RGB32);
    image.fill(Qt::green);
    FakePlasmaApplicator plasma(true);
    const auto applied = OutputService::applyManaged(image, image.size(),
        {{"screen", 1, image.rect(), image.rect()}}, "green.png", {}, plasma, root.path());
    QVERIFY(applied.success);
    SetLibrary library(root.path());
    auto set = library.sets().first();
    auto manifest = set.manifest;
    auto crops = manifest.value("crops").toArray();
    auto crop = crops.first().toObject();
    crop.insert("path", "../outside.png");
    crops[0] = crop;
    manifest.insert("crops", crops);
    QFile file(applied.manifestPath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(manifest).toJson());
    file.close();
    QVERIFY(!library.sets().first().problem.isEmpty());
    plasma.references = OperationResult::ok();
    QVERIFY(!library.remove(set.id, plasma).success);
    QVERIFY(!library.rename(set.id, "changed").success);
    QVERIFY(!library.load("../outside", &set).success);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("{ broken");
    file.close();
    QCOMPARE(library.sets().size(), 1);
    QVERIFY(!library.sets().first().problem.isEmpty());

    // Future schemas stay visible but cannot be modified.
    manifest = QJsonObject{{"schemaVersion", 99}, {"id", QFileInfo(applied.manifestPath).dir().dirName()}};
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(QJsonDocument(manifest).toJson());
    file.close();
    QVERIFY(!library.sets().first().problem.isEmpty());
    QVERIFY(!library.remove(manifest.value("id").toString(), plasma).success);

    // Restore the set, then replace one crop with a symlink to user-owned data.
    QVERIFY(OutputService::applyManaged(image, image.size(),
        {{"screen", 1, image.rect(), image.rect()}}, "green.png", {}, plasma, root.path()).success);
    const QString outside = root.filePath("source.png");
    QVERIFY(image.save(outside));
    QVERIFY(QFile::remove(applied.paths.first()));
    QVERIFY(QFile::link(outside, applied.paths.first()));
    set = library.sets().first();
    QVERIFY(!set.problem.isEmpty());
    QVERIFY(!library.remove(set.id, plasma).success);
    QVERIFY(QFileInfo::exists(outside));
}

void SplitImageTest::libraryDialogShowsSets() {
    QTemporaryDir root;
    SetLibraryDialog empty(nullptr, root.path());
    QCOMPARE(empty.findChild<QListWidget *>("setList")->count(), 0);
    auto *emptySelectAll = empty.findChild<QCheckBox *>("selectAllSets");
    QVERIFY(emptySelectAll);
    QVERIFY(!emptySelectAll->isEnabled());
    QImage image(10, 10, QImage::Format_RGB32);
    image.fill(Qt::green);
    FakePlasmaApplicator plasma(true);
    QVERIFY(OutputService::applyManaged(image, image.size(),
        {{"screen", 1, image.rect(), image.rect()}}, "green.png", {}, plasma, root.path()).success);
    SetLibraryDialog populated(nullptr, root.path());
    auto *list = populated.findChild<QListWidget *>("setList");
    QCOMPARE(list->count(), 1);
    QCOMPARE(list->currentItem()->text(), "green.png");
    QCOMPARE(list->currentItem()->checkState(), Qt::Unchecked);
    auto *selectAll = populated.findChild<QCheckBox *>("selectAllSets");
    auto *deleteSelected = populated.findChild<QPushButton *>("deleteSelectedSets");
    auto *rename = populated.findChild<QPushButton *>("renameSet");
    auto *exportSet = populated.findChild<QPushButton *>("exportSet");
    QVERIFY(selectAll);
    QVERIFY(deleteSelected);
    QVERIFY(rename);
    QVERIFY(exportSet);
    QVERIFY(deleteSelected->isEnabled());
    QVERIFY(rename->isEnabled());
    QVERIFY(exportSet->isEnabled());
    QCOMPARE(deleteSelected->text(), "Delete");
    selectAll->click();
    QCOMPARE(list->currentItem()->checkState(), Qt::Checked);
    QVERIFY(deleteSelected->isEnabled());
    QCOMPARE(deleteSelected->text(), "Delete selected (1)");
    list->currentItem()->setCheckState(Qt::Unchecked);
    QCOMPARE(selectAll->checkState(), Qt::Unchecked);
    QVERIFY(deleteSelected->isEnabled());
    QCOMPARE(deleteSelected->text(), "Delete");

    image.fill(Qt::blue);
    QVERIFY(OutputService::applyManaged(image, image.size(),
        {{"screen", 1, image.rect(), image.rect()}}, "blue.png", {}, plasma, root.path()).success);
    SetLibraryDialog multiple(nullptr, root.path());
    list = multiple.findChild<QListWidget *>("setList");
    selectAll = multiple.findChild<QCheckBox *>("selectAllSets");
    deleteSelected = multiple.findChild<QPushButton *>("deleteSelectedSets");
    rename = multiple.findChild<QPushButton *>("renameSet");
    exportSet = multiple.findChild<QPushButton *>("exportSet");
    QCOMPARE(list->count(), 2);
    list->item(0)->setCheckState(Qt::Checked);
    QCOMPARE(selectAll->checkState(), Qt::PartiallyChecked);
    QCOMPARE(deleteSelected->text(), "Delete selected (1)");
    selectAll->click();
    QCOMPARE(list->item(0)->checkState(), Qt::Checked);
    QCOMPARE(list->item(1)->checkState(), Qt::Checked);
    QCOMPARE(deleteSelected->text(), "Delete selected (2)");
    QVERIFY(!rename->isEnabled());
    QVERIFY(!exportSet->isEnabled());
    selectAll->click();
    QCOMPARE(list->item(0)->checkState(), Qt::Unchecked);
    QCOMPARE(list->item(1)->checkState(), Qt::Unchecked);
    QVERIFY(deleteSelected->isEnabled());
    QVERIFY(rename->isEnabled());
    QVERIFY(exportSet->isEnabled());
    list->setCurrentRow(list->currentRow() == 0 ? 1 : 0);
    QVERIFY(deleteSelected->isEnabled());
    QVERIFY(rename->isEnabled());
}

void SplitImageTest::physicalLayoutCompensatesForPixelDensity() {
    const QList<MonitorInfo> monitors{{"a", "left", QRect(0, 0, 800, 400), QSizeF(100, 50)},
                                       {"b", "right", QRect(800, 0, 1600, 800), QSizeF(100, 50)}};
    MonitorPreferences preferences;
    auto layout = MonitorLayout::rectangles(monitors, preferences);
    QCOMPARE(layout[0], QRectF(monitors[0].desktopGeometry));
    QCOMPARE(layout[1], QRectF(monitors[1].desktopGeometry));
    preferences.enabled = true;
    layout = MonitorLayout::rectangles(monitors, preferences);
    QCOMPARE(layout[0].right(), layout[1].left()); // Default adjacency follows physical widths.
    preferences.measurements.insert("a", {{100, 50}, {0, 0}, false});
    preferences.measurements.insert("b", {{100, 50}, {100, 0}, false});
    layout = MonitorLayout::rectangles(monitors, preferences);
    QCOMPARE(layout[0].size(), layout[1].size());
    QCOMPARE(layout[0].right(), layout[1].left());
    QImage image(800, 200, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixelColor(x, y, QColor(x % 256, y, 0));
    QList<ScreenCrop> screens;
    for (int i = 0; i < monitors.size(); ++i)
        screens.append({monitors[i].name, i + 1, monitors[i].desktopGeometry, layout[i].toAlignedRect()});
    QList<CropArtifact> crops;
    QVERIFY(OutputService::createCrops(image, screens, &crops).success);
    QCOMPARE(crops[0].image.size(), QSize(400, 200));
    QCOMPARE(crops[1].image.size(), QSize(400, 200));
    QCOMPARE(crops[0].image.pixelColor(399, 100), image.pixelColor(399, 100));
    QCOMPARE(crops[1].image.pixelColor(0, 100), image.pixelColor(400, 100));
    QTemporaryDir root;
    FakePlasmaApplicator plasma(true);
    QVERIFY(OutputService::applyManaged(image, image.size(), screens, "source.png", {}, plasma, root.path()).success);
    QCOMPARE(plasma.receivedScreens[1].desktopGeometry, monitors[1].desktopGeometry);
    const auto saved = SetLibrary(root.path()).sets().first();
    QCOMPARE(saved.screens[1].cropRect, QRect(400, 0, 400, 200));
    QCOMPARE(saved.screens[1].desktopGeometry, monitors[1].desktopGeometry);
    // Preview must use physical crop proportions, even though the second desktop is twice as wide.
    QCOMPARE(SetLibrary::preview(saved, QSize(800, 200)).pixelColor(400, 100), image.pixelColor(400, 100));
    preferences.measurements["b"].position = {-105, -10};
    layout = MonitorLayout::rectangles(monitors, preferences);
    QCOMPARE(layout[1].topLeft(), QPointF(-420, -40));
    QCOMPARE(layout[0].left() - layout[1].right(), 20.0); // 5 mm bezel gap.
}

void SplitImageTest::physicalMeasurementsHandleRotationAndInvalidData() {
    const QSizeF diagonal = MonitorLayout::sizeFromDiagonal(27, QSize(1920, 1080));
    QVERIFY(qAbs(diagonal.width() / diagonal.height() - 16.0 / 9.0) < 0.00001);
    QVERIFY(qAbs(std::hypot(diagonal.width(), diagonal.height()) - 27 * 25.4) < 0.00001);
    MonitorInfo monitor{"a", "portrait", QRect(0, 0, 1080, 1920), {}};
    MonitorPreferences preferences;
    preferences.measurements.insert("a", {{600, 337.5}, {-600, 50}, false});
    const auto rotated = MonitorLayout::measurement(monitor, preferences, 0.25);
    QCOMPARE(rotated.millimeters, QSizeF(337.5, 600));
    QCOMPARE(rotated.position, QPointF(-600, 50));
    QVERIFY(rotated.portrait);
    preferences.measurements["a"].millimeters = QSizeF(std::numeric_limits<double>::quiet_NaN(), 100);
    const auto fallback = MonitorLayout::measurement(monitor, preferences, 0.25);
    QVERIFY(MonitorLayout::validSize(fallback.millimeters));
    QVERIFY(fallback.millimeters.height() > fallback.millimeters.width());
    QVERIFY(!MonitorLayout::validSize(QSizeF(0, 10)));
    QCOMPARE(MonitorLayout::identity("vendor", "model", "serial", "DP-1"),
             MonitorLayout::identity("vendor", "model", "serial", "HDMI-1"));
    QVERIFY(MonitorLayout::identity("vendor", "model", "", "DP-1")
            != MonitorLayout::identity("vendor", "model", "", "DP-2"));
}

void SplitImageTest::physicalMeasurementsPersistSeparately() {
    QTemporaryDir settingsDirectory;
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsDirectory.path());
    QVERIFY(!AppSettings::loadMonitors().enabled);
    MonitorPreferences expected;
    expected.enabled = true;
    expected.measurements.insert("connected", {{600, 340}, {-605, 10}, false});
    expected.measurements.insert("disconnected", {{300, 530}, {0, -50}, true});
    AppSettings::saveMonitors(expected);
    AppSettings::save(UserPreferences{});
    AppSettings::reset(); // Output preferences must not erase monitor calibration.
    const auto actual = AppSettings::loadMonitors();
    QVERIFY(actual.enabled);
    QCOMPARE(actual.measurements.size(), 2);
    QCOMPARE(actual.measurements["connected"].millimeters, QSizeF(600, 340));
    QCOMPARE(actual.measurements["connected"].position, QPointF(-605, 10));
    QVERIFY(actual.measurements["disconnected"].portrait);
}

void SplitImageTest::physicalDialogEditsDimensionsAndAlignment() {
    const QList<MonitorInfo> monitors{{"a", "left", QRect(-1920, 0, 1920, 1080), QSizeF(600, 340)},
                                       {"b", "right", QRect(0, 0, 3840, 2160), QSizeF(600, 340)}};
    MonitorPreferences saved;
    saved.measurements.insert("disconnected", {{500, 300}, {}, false});
    MonitorsDialog dialog(monitors, saved);
    dialog.show();
    QApplication::processEvents();
    dialog.findChild<QCheckBox *>("physicalSizing")->setChecked(true);
    dialog.findChild<QDoubleSpinBox *>("diagonal-0")->setValue(24);
    auto edited = dialog.preferences();
    QVERIFY(qAbs(edited.measurements["a"].millimeters.width() - 531.3) < 0.2);
    dialog.findChild<QDoubleSpinBox *>("width-0")->setValue(550);
    dialog.findChild<QPushButton *>("arrangeMonitors")->click();
    edited = dialog.preferences();
    QVERIFY(edited.enabled);
    QCOMPARE(edited.measurements["b"].position, QPointF(550, 0));
    QVERIFY(edited.measurements.contains("disconnected"));
    QCOMPARE(saved.measurements.size(), 1); // Editing alone does not mutate saved preferences.
}

void SplitImageTest::physicalLayoutFitsSmallImages() {
    QGraphicsScene scene;
    QImage image(20, 20, QImage::Format_RGB32);
    image.fill(Qt::red);
    auto *parent = scene.addPixmap(QPixmap::fromImage(image));
    MonitorPreferences preferences;
    preferences.enabled = true;
    preferences.measurements.insert("a", {{600, 340}, {-600, 10}, false});
    auto *screens = new ScreensItem(parent, {{"a", "screen", QRect(0, 0, 1920, 1080), {}}}, preferences);
    screens->constrainToParent();
    const auto crop = screens->mapRectToParent(screens->getRectangles().first()->rect());
    QVERIFY(parent->boundingRect().contains(crop));
    QVERIFY(screens->scale() < 0.1);
}

void SplitImageTest::commandLineUsesPhysicalMeasurements() {
    QTemporaryDir temporary;
    const QString configRoot = temporary.filePath("config");
    QSettings settings(configRoot + "/wallpaper-splitter/settings.conf", QSettings::IniFormat);
    settings.setValue("monitors/physicalSizing", true);
    for (const auto &monitor : MonitorLayout::connectedMonitors()) {
        settings.setValue("monitors/" + monitor.id + "/millimeters", QSizeF(100, 50));
        settings.setValue("monitors/" + monitor.id + "/position", QPointF(-100, -20));
        settings.setValue("monitors/" + monitor.id + "/portrait",
                          monitor.desktopGeometry.height() > monitor.desktopGeometry.width());
    }
    settings.sync();
    QImage source(1200, 900, QImage::Format_RGB32);
    source.fill(Qt::cyan);
    const QString sourcePath = temporary.filePath("physical-cli.png");
    QVERIFY(source.save(sourcePath));
    const QString output = temporary.filePath("exports");
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("XDG_CONFIG_HOME", configRoot);
    process.setProcessEnvironment(environment);
    process.setProgram(QCoreApplication::applicationDirPath() + "/wallpaper_splitter");
    process.setArguments({"--destination", output, "--bottom-right", "300,200",
                          "--collision", "replace", sourcePath});
    process.start();
    QVERIFY(process.waitForFinished());
    QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
    const QImage crop(output + "/physical-cli-1.png");
    QCOMPARE(crop.size(), QSize(300, 150));
    QCOMPARE(crop.pixelColor(100, 100), QColor(Qt::cyan));
}

void SplitImageTest::largestLayoutFit_data() {
    QTest::addColumn<QSize>("imageSize");
    QTest::addColumn<QRectF>("expectedBounds");
    QTest::addColumn<bool>("physical");
    QTest::newRow("wide") << QSize(640, 400) << QRectF(0, 100, 640, 200) << false;
    QTest::newRow("portrait") << QSize(160, 800) << QRectF(0, 375, 160, 50) << false;
    QTest::newRow("height-limited") << QSize(800, 100) << QRectF(240, 0, 320, 100) << false;
    QTest::newRow("tiny") << QSize(16, 10) << QRectF(0, 2.5, 16, 5) << false;
    QTest::newRow("physical-gap") << QSize(640, 400) << QRectF(0, 100, 640, 200) << true;
}

void SplitImageTest::largestLayoutFit() {
    QFETCH(QSize, imageSize);
    QFETCH(QRectF, expectedBounds);
    QFETCH(bool, physical);
    const QList<MonitorInfo> monitors{
        {"a", "A", QRect(-100, -50, 100, 100), {}},
        {"b", "B", QRect(20, 0, 200, 50), {}}};
    MonitorPreferences preferences;
    preferences.enabled = physical;
    preferences.measurements.insert("a", {{100, 100}, {-100, -50}, false});
    preferences.measurements.insert("b", {{200, 50}, {20, 0}, false});
    QGraphicsScene scene;
    QImage source(imageSize, QImage::Format_RGB32);
    source.fill(Qt::cyan);
    auto *parent = scene.addPixmap(QPixmap::fromImage(source));
    auto *screens = new ScreensItem(parent, monitors, preferences);
    screens->fitAsLargeAsPossible();
    QCOMPARE(screens->mapRectToParent(screens->layoutBounds()), expectedBounds);
    const auto rectangles = screens->getRectangles();
    const auto first = screens->mapRectToParent(rectangles[0]->rect());
    const auto second = screens->mapRectToParent(rectangles[1]->rect());
    QCOMPARE(first.width(), second.width() / 2);
    QCOMPARE(first.height(), second.height() * 2);
    QCOMPARE(second.left() - first.right(), first.width() / 5);
    QCOMPARE(second.top() - first.top(), first.height() / 2);
    QVERIFY(parent->boundingRect().contains(first));
    QVERIFY(parent->boundingRect().contains(second));
    // A repeated fit also restores the layout after a manual adjustment.
    screens->setScale(screens->scale() / 2);
    screens->setPos(screens->pos() + QPointF(2, 2));
    screens->fitAsLargeAsPossible();
    QCOMPARE(screens->mapRectToParent(screens->layoutBounds()), expectedBounds);
}

void SplitImageTest::largestFitButtonUsesImageEdges() {
    WallpaperSplitter splitter;
    auto *button = splitter.findChild<QAbstractButton *>("stretchImageButton");
    QVERIFY(button);
    QVERIFY(!button->isEnabled());
    QImage source(1600, 1200, QImage::Format_RGB32);
    source.fill(Qt::red);
    splitter.addImage(source);
    QVERIFY(button->isEnabled());
    button->click();
    auto *view = splitter.findChild<QGraphicsView *>();
    ScreensItem *screens = nullptr;
    for (auto *item : view->scene()->items()) {
        if (auto *candidate = dynamic_cast<ScreensItem *>(item)) screens = candidate;
    }
    QVERIFY(screens);
    const QRectF fitted = screens->mapRectToParent(screens->layoutBounds());
    auto *image = dynamic_cast<ResizableImageItem *>(screens->parentItem());
    QVERIFY(image);
    QCOMPARE(image->image().size(), screens->layoutBounds().size().toSize());
    QCOMPARE(fitted, image->boundingRect());
    QCOMPARE(image->image(), source.scaled(image->image().size(), Qt::IgnoreAspectRatio,
                                         Qt::SmoothTransformation));
}

void SplitImageTest::stretchSpansOriginalImageAcrossScreens() {
    QImage source(60, 60, QImage::Format_RGB32);
    for (int y = 0; y < source.height(); ++y)
        for (int x = 0; x < source.width(); ++x)
            source.setPixelColor(x, y, QColor(x * 4, y * 4, 0));
    QGraphicsScene scene;
    auto *image = new ResizableImageItem(source);
    scene.addItem(image);
    auto *screens = new ScreensItem(image, {
        {"a", "Left", QRect(-100, -50, 100, 100), {}},
        {"b", "Right", QRect(0, -50, 100, 100), {}}}, {});
    image->setScreenGroup(screens);
    image->stretchToLayout();
    const QImage expected = source.scaled(200, 100, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QCOMPARE(image->image(), expected);
    QList<ScreenCrop> crops;
    for (const auto *rect : screens->getRectangles())
        crops.append({"screen", int(crops.size()) + 1, {},
                      screens->mapRectToParent(rect->rect()).toAlignedRect()});
    QList<CropArtifact> artifacts;
    QVERIFY(OutputService::createCrops(image->image(), crops, &artifacts).success);
    QCOMPARE(artifacts.size(), 2);
    QCOMPARE(artifacts[0].image, expected.copy(0, 0, 100, 100));
    QCOMPARE(artifacts[1].image, expected.copy(100, 0, 100, 100));
    // Fitting again discards manual stretching and uses the original pixels.
    image->beginResize(ResizableImageItem::Corner::BottomRight);
    image->resizeTo(image->mapToScene(QPointF(400, 300)));
    QVERIFY(image->image().size() != expected.size());
    screens->setScale(0.5);
    image->stretchToLayout();
    QCOMPARE(image->image(), expected);
    QCOMPARE(screens->mapRectToParent(screens->layoutBounds()), QRectF(expected.rect()));
}

void SplitImageTest::commandLineLargestFit() {
    QTemporaryDir temporary;
    QImage source(120, 90, QImage::Format_RGB32);
    for (int y = 0; y < source.height(); ++y)
        for (int x = 0; x < source.width(); ++x)
            source.setPixelColor(x, y, QColor(x, y, 0));
    const QString sourcePath = temporary.filePath("fit.png");
    QVERIFY(source.save(sourcePath));
    const QString configRoot = temporary.filePath("config");
    QSettings settings(configRoot + "/wallpaper-splitter/settings.conf", QSettings::IniFormat);
    settings.setValue("monitors/physicalSizing", true);
    for (const auto &monitor : MonitorLayout::connectedMonitors()) {
        settings.setValue("monitors/" + monitor.id + "/millimeters", QSizeF(100, 50));
        settings.setValue("monitors/" + monitor.id + "/position", QPointF(-100, -20));
        settings.setValue("monitors/" + monitor.id + "/portrait",
                          monitor.desktopGeometry.height() > monitor.desktopGeometry.width());
    }
    settings.sync();
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("XDG_CONFIG_HOME", configRoot);
    process.setProcessEnvironment(environment);
    process.setProgram(QCoreApplication::applicationDirPath() + "/wallpaper_splitter");
    const QString output = temporary.filePath("output");
    process.setArguments({"--stretch-across-screens", "--destination", output,
                          "--filename-template", "{source}-{number}", sourcePath});
    process.start();
    QVERIFY(process.waitForFinished());
    QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
    const QImage crop(output + "/fit-1.png");
    QCOMPARE(crop, source.scaled(400, 200, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    process.setArguments({"--fill-panorama", "--destination", output,
                          "--collision", "replace", "--filename-template", "{source}-{number}", sourcePath});
    process.start();
    QVERIFY(process.waitForFinished());
    QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
    const QImage panorama(output + "/fit-1.png");
    QCOMPARE(panorama.size(), QSize(400, 200));
    QVERIFY(qAbs(panorama.pixelColor(200, 100).red() - 60) <= 1);
    QVERIFY(qAbs(panorama.pixelColor(200, 100).green() - 45) <= 1);
    QVERIFY(qAbs(panorama.pixelColor(0, 0).green() - 15) <= 1);
    process.setArguments({"--fill-panorama", "--stretch-across-screens", sourcePath});
    process.start();
    QVERIFY(process.waitForFinished());
    QCOMPARE(process.exitCode(), 2);
    for (const auto &option : {"--top-left", "--bottom-right"}) {
        process.setArguments({"--stretch-across-screens", option, "0,0", sourcePath});
        process.start();
        QVERIFY(process.waitForFinished());
        QCOMPARE(process.exitCode(), 2);
        QVERIFY(process.readAllStandardError().contains("cannot be combined"));
    }
}

void SplitImageTest::panoramaPreservesProportionsAndContinuity() {
    QImage source(300, 200, QImage::Format_RGB32);
    for (int y = 0; y < source.height(); ++y)
        for (int x = 0; x < source.width(); ++x)
            source.setPixelColor(x, y, QColor(x % 256, y, 80));
    const QList<QRectF> layout{{-100, -10, 100, 100}, {0, -10, 100, 100}, {100, -10, 100, 100}};
    const auto rendered = MonitorLayout::renderPanorama(source, layout);
    QCOMPARE(rendered.convertToFormat(source.format()), source.copy(0, 50, 300, 100));
    const auto portrait = MonitorLayout::renderPanorama(source, {{0, 0, 100, 100}, {0, 100, 100, 100}});
    QCOMPARE(portrait.convertToFormat(source.format()), source.copy(100, 0, 100, 200));
    const auto gapped = MonitorLayout::renderPanorama(source, {{-100, -10, 100, 100}, {10, 0, 190, 100}});
    QCOMPARE(gapped.convertToFormat(source.format()), source.copy(0, 45, 300, 110));
    QVERIFY(MonitorLayout::renderPanorama(source, {}).isNull());
    QVERIFY(MonitorLayout::renderPanorama({}, layout).isNull());
}

void SplitImageTest::panoramaActionRestoresOriginalAndSwitchesBack() {
    WallpaperSplitter splitter;
    QImage source(320, 120, QImage::Format_RGB32);
    source.fill(Qt::cyan);
    splitter.addImage(source);
    auto *view = static_cast<GraphicsView *>(splitter.findChild<QGraphicsView *>());
    QVERIFY(view);
    auto *stretch = splitter.findChild<QAbstractButton *>("stretchImageButton");
    auto *maximize = splitter.findChild<QAbstractButton *>("fillPanoramaButton");
    QVERIFY(stretch && maximize);
    stretch->click();
    maximize->click();
    ResizableImageItem *image = nullptr;
    ScreensItem *screens = nullptr;
    for (auto *item : view->scene()->items()) {
        if (auto *candidate = dynamic_cast<ResizableImageItem *>(item)) image = candidate;
        if (auto *candidate = dynamic_cast<ScreensItem *>(item)) screens = candidate;
    }
    QVERIFY(image && screens);
    const auto layout = MonitorLayout::rectangles(screens->monitors(), AppSettings::loadMonitors());
    QRectF bounds;
    for (const auto &rect : layout) bounds = bounds.united(rect);
    QCOMPARE(image->image().size(), bounds.size().toSize());
    for (int i = 0; i < layout.size(); ++i)
        QCOMPARE(screens->getRectangles()[i]->rect(), layout[i]);
    stretch->click();
    QCOMPARE(image->image().size(), bounds.size().toSize());
    QCOMPARE(image->image(), source.scaled(bounds.size().toSize(), Qt::IgnoreAspectRatio,
                                           Qt::SmoothTransformation));
}

void SplitImageTest::layoutButtonsToggleBackToLoadedState() {
    WallpaperSplitter splitter;
    QImage source(320, 120, QImage::Format_RGB32);
    source.fill(Qt::cyan);
    source.setPixelColor(30, 40, Qt::red);
    splitter.addImage(source);
    auto *view = splitter.findChild<QGraphicsView *>();
    auto *stretch = splitter.findChild<QAbstractButton *>("stretchImageButton");
    auto *panorama = splitter.findChild<QAbstractButton *>("fillPanoramaButton");
    QVERIFY(view && stretch && panorama);
    ResizableImageItem *image = nullptr;
    for (auto *item : view->scene()->items())
        if (auto *candidate = dynamic_cast<ResizableImageItem *>(item)) image = candidate;
    QVERIFY(image);
    const auto cropRectangles = [&] {
        QList<QRectF> result;
        for (auto *item : view->scene()->items()) {
            if (auto *screens = dynamic_cast<ScreensItem *>(item))
                for (const auto *rect : screens->getRectangles())
                    result.append(screens->mapRectToParent(rect->rect()));
        }
        return result;
    };
    const auto originalCrops = cropRectangles();
    const auto originalPosition = image->pos();
    for (auto *button : {stretch, panorama}) {
        QVERIFY(!button->isChecked());
        button->click();
        QVERIFY(button->isChecked());
        image->beginResize(ResizableImageItem::Corner::TopLeft);
        image->resizeTo(image->mapToScene(QPointF(-100, -100)));
        button->click();
        QVERIFY(!stretch->isChecked());
        QVERIFY(!panorama->isChecked());
        QCOMPARE(image->image(), source);
        QCOMPARE(image->pos(), originalPosition);
        QCOMPARE(cropRectangles(), originalCrops);
    }
    stretch->click();
    panorama->click();
    QVERIFY(!stretch->isChecked());
    QVERIFY(panorama->isChecked());
    panorama->click();
    QCOMPARE(image->image(), source);
    QCOMPARE(cropRectangles(), originalCrops);
    stretch->click();
    splitter.addImage(source);
    QVERIFY(!stretch->isChecked());
    QVERIFY(!panorama->isChecked());
    QCOMPARE(cropRectangles(), originalCrops);
}

void SplitImageTest::panoramaPreviewKeepsThreeMonitorsSeparate() {
    QImage source(300, 200, QImage::Format_RGB32);
    source.fill(Qt::blue);
    source.setPixelColor(150, 100, Qt::red);
    const QList<MonitorInfo> monitors{
        {"a", "Left", QRect(-100, -10, 100, 100), {}},
        {"b", "Middle", QRect(0, -10, 100, 100), {}},
        {"c", "Right", QRect(100, -10, 100, 100), {}}};
    QGraphicsScene scene;
    auto *image = new ResizableImageItem(source);
    scene.addItem(image);
    auto *screens = new ScreensItem(image, monitors, {});
    image->setScreenGroup(screens);
    image->fillToLayout();
    QCOMPARE(image->image().size(), QSize(300, 100));
    QList<ScreenCrop> crops;
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(screens->getRectangles()[i]->rect(), QRectF(monitors[i].desktopGeometry));
        const QRect mapped = screens->mapRectToParent(screens->getRectangles()[i]->rect()).toAlignedRect();
        QCOMPARE(mapped, QRect(i * 100, 0, 100, 100));
        crops.append({monitors[i].name, i + 1, monitors[i].desktopGeometry, mapped});
    }
    QList<CropArtifact> artifacts;
    QVERIFY(OutputService::createCrops(image->image(), crops, &artifacts).success);
    for (int i = 0; i < 3; ++i)
        QCOMPARE(artifacts[i].image.convertToFormat(source.format()), source.copy(i * 100, 50, 100, 100));
    const QImage firstRender = image->image();
    image->fillToLayout();
    QCOMPARE(image->image(), firstRender);
    image->beginResize(ResizableImageItem::Corner::BottomRight);
    image->resizeTo(image->mapToScene(QPointF(600, 200)));
    QCOMPARE(image->image(), firstRender.scaled(600, 200, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    image->stretchToLayout();
    QCOMPARE(image->image(), source.scaled(300, 100, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    const QImage gapped = MonitorLayout::renderPanorama(source, {{-100, -10, 100, 100}, {10, 0, 100, 100}});
    QCOMPARE(gapped.size(), QSize(210, 110));
    QCOMPARE(gapped.pixelColor(105, 50), QColor(Qt::blue));
}

QTEST_MAIN(SplitImageTest)
#include "splitimage_test.moc"
