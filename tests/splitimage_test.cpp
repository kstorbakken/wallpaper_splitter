#include <QColor>
#include <QDir>
#include <QImage>
#include <QGraphicsScene>
#include <QTemporaryDir>
#include <QtTest>

#include "resizableimageitem.h"
#include "screensitem.h"
#include "wallpapersplitter.h"

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

QTEST_MAIN(SplitImageTest)
#include "splitimage_test.moc"
