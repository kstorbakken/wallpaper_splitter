//
// Created by l0drex on 15.09.21.
//

// You may need to build the project (run Qt uic code generator) to get "ui_WallpaperSplitter.h" resolved

#include <QFileDialog>
#include <QStandardPaths>
#include <QDebug>
#include <QApplication>
#include <QScreen>
#include <QDBusMessage>
#include <QDBusConnection>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QPushButton>
#include "wallpapersplitter.h"
#include "ui_wallpapersplitter.h"
#include "screensitem.h"
#include "graphicsview.h"
#include "resizableimageitem.h"


WallpaperSplitter::WallpaperSplitter(QWidget *parent) :
        QDialog(parent), ui(new Ui::WallpaperSplitter) {
    ui->setupUi(this);

    // replace the standard graphics view with my subclass
    auto graphicsView = new GraphicsView(this);
    delete ui->verticalLayout->replaceWidget(ui->graphicsView, graphicsView)->widget();
    ui->graphicsView = graphicsView;

    auto scene = new QGraphicsScene();
    ui->graphicsView->setScene(scene);
    auto text = ui->graphicsView->scene()->addText(tr("Drop an image here"));
    // make sure its fixed size
    text->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    // center it
    auto rect = text->boundingRect();
    text->setTransformOriginPoint(rect.center());
    text->setPos(-rect.width() / 2.0, -rect.height() / 2.0);
    ui->graphicsView->centerOn(text);
    imageFile = new QFileInfo();

    connect(ui->buttonBoxOpen, &QDialogButtonBox::accepted,
            this, &WallpaperSplitter::selectImage);
    connect(ui->buttonBox->button(QDialogButtonBox::StandardButton::Ok), &QPushButton::pressed,
            this, &WallpaperSplitter::applyWallpaper);
    connect(ui->buttonBox->button(QDialogButtonBox::StandardButton::Save), &QPushButton::pressed,
            this, &WallpaperSplitter::saveWallpapers);
    connect(ui->buttonBox, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
}

/**
 * Opens a dialog that asks the user to select an image.
 *
 * Then sets the file info and image attribute, displays the image and calls addScreens().
 * The image will be scaled to fit on all screens, also the graphics view will be scaled to show the whole image.
 */
void WallpaperSplitter::selectImage() {
    const auto url = QFileDialog::getOpenFileUrl(
            this,
            tr("Select a wallpaper image"),
            "file://" + QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
            QString("Images (*.jpg *.png *.bmp)")
    );
    addImage(url);
}

/**
 * Splits the selected image and returns a list to all paths where the images were saved.
 */
QStringList WallpaperSplitter::splitImage(const QImage &image, const QList<QRect> &screens,
                                          const QString &path, const QString &outputBaseName) {
    if (screens.isEmpty()) {
        qFatal("No area to cut out provided!");
    }
    if (image.isNull() || image.sizeInBytes() < 0) {
        qFatal("Image could not be loaded");
    }

    QRect cropBounds;
    for (const QRect &screen : screens) {
        cropBounds = cropBounds.isNull() ? screen : cropBounds.united(screen);
    }
    if (cropBounds.width() > image.width() || cropBounds.height() > image.height()) {
        qFatal("Combined crop area is larger than the source image");
    }

    QPoint correction;
    if (cropBounds.left() < image.rect().left()) {
        correction.setX(image.rect().left() - cropBounds.left());
    } else if (cropBounds.right() > image.rect().right()) {
        correction.setX(image.rect().right() - cropBounds.right());
    }
    if (cropBounds.top() < image.rect().top()) {
        correction.setY(image.rect().top() - cropBounds.top());
    } else if (cropBounds.bottom() > image.rect().bottom()) {
        correction.setY(image.rect().bottom() - cropBounds.bottom());
    }
    if (!correction.isNull()) {
        qWarning() << "Moving crop layout by" << correction
                   << "to fit source" << image.rect();
    }

    const QString safeBaseName = QFileInfo(outputBaseName).completeBaseName();

    QImage wallpaper;
    QString fileName;
    QStringList paths{};
    QDir().mkpath(path);
    int index = 0;

    std::for_each(screens.begin(), screens.end(), [&](QRect screen){
        screen.translate(correction);
        qDebug() << "Cropping" << screen << "from source" << image.rect();
        if (!image.rect().contains(screen)) {
            qFatal("Crop rectangle is outside the source image");
        }

        // copy a rectangle with size and position of the screen
        wallpaper = image.copy(screen);
        qDebug() << "Generated crop" << index << wallpaper.size();

        const QByteArrayView pixels(
                reinterpret_cast<const char *>(wallpaper.constBits()),
                wallpaper.sizeInBytes());
        const QString digest = QString::fromLatin1(
                QCryptographicHash::hash(pixels, QCryptographicHash::Sha256)
                        .toHex().left(16));
        // Keep all output in the selected directory. The crop-content digest
        // makes Plasma reload changed pixels even if the source name is reused.
        fileName = path + '/' + safeBaseName + '-'
                + QString::number(index + 1) + '-' + digest + ".png";
        paths.append(fileName);

        // if this returns false, the save failed and the assertion fails
        bool success = wallpaper.save(fileName);
        assert(success);
        index++;
    });

    return paths;
}

QStringList WallpaperSplitter::splitImage(const QImage &image, const QString &path,
                                          const QPoint topLeft, const QPoint bottomRight,
                                          const QString &outputBaseName) {
    QList<QRect> screenGeometries{};
    const auto screens = QApplication::screens();
    std::for_each(screens.begin(), screens.end(), [&](const QScreen* screen){
        // set top-left corner
        QRect geometry = screen->geometry();
        QPoint delta = screen->geometry().topLeft() - screens.first()->geometry().topLeft();
        geometry.moveTopLeft(topLeft + delta);
        // set bottom-right to desired position, if possible
        if (bottomRight.manhattanLength() > 0) {
            geometry.setSize(geometry.size().scaled(bottomRight.x(), bottomRight.y(), Qt::KeepAspectRatio));
        }
        screenGeometries.append(geometry);
    });

    return splitImage(image, screenGeometries, path, outputBaseName);
}

QStringList WallpaperSplitter::splitImage() {
    setCursor(Qt::WaitCursor);

    QList<QRect> screens = {};
    const auto screenItems = screenGroup->getRectangles();
    std::for_each(screenItems.begin(), screenItems.end(), [&](const QGraphicsRectItem *screen){
        screens.append(screenGroup->mapRectToParent(screen->rect()).toAlignedRect());
    });

    QString path;
    if (imageFile->isFile()) {
        path = QFileDialog::getExistingDirectory(
                this, "",
                imageFile->absolutePath(), QFileDialog::ShowDirsOnly);
    } else {
        path = QFileDialog::getExistingDirectory(
                this, "",
                QStandardPaths::standardLocations(QStandardPaths::PicturesLocation)[0], QFileDialog::ShowDirsOnly);
    }

    unsetCursor();
    return WallpaperSplitter::splitImage(
            imageItem->image(), screens, path, imageFile->completeBaseName());
}

/**
 * Applies the selected image to all screens in the current activity.
 */
void WallpaperSplitter::applyWallpaper() {
    auto paths = splitImage();
    assert(!paths.isEmpty());

    const auto screens = QApplication::screens();
    assert(paths.size() == screens.size());

    // Qt and Plasma can assign different numeric indices to the same physical
    // screen. Pass each crop's virtual-desktop geometry so Plasma can match it
    // to the containment's screenGeometry() without relying on index order.
    QVariantList crops;
    for (int index = 0; index < paths.size(); ++index) {
        const QRect geometry = screens.at(index)->geometry();
        crops.append(QVariantMap{
                {"path", paths.at(index)},
                {"x", geometry.x()},
                {"y", geometry.y()},
                {"width", geometry.width()},
                {"height", geometry.height()}
        });
    }
    const QString cropArray = QString::fromUtf8(
            QJsonDocument::fromVariant(crops).toJson(QJsonDocument::Compact));
    const QString script = QStringLiteral(R"(
const crops = %1;
function sameGeometry(crop, geometry) {
    return crop.x === geometry.x && crop.y === geometry.y
        && crop.width === geometry.width && crop.height === geometry.height;
}
function describeGeometry(geometry) {
    return geometry.x + ',' + geometry.y + ' '
        + geometry.width + 'x' + geometry.height;
}
for (const desktop of desktopsForActivity(currentActivity())) {
    if (desktop.screen < 0) {
        print('Skipping desktop ' + desktop.id + ' screen=' + desktop.screen);
        continue;
    }

    const geometry = screenGeometry(desktop.screen);
    const crop = crops.find(candidate => sameGeometry(candidate, geometry));
    if (crop === undefined) {
        print('No crop for desktop ' + desktop.id + ' screen=' + desktop.screen
              + ' geometry=' + describeGeometry(geometry));
        continue;
    }

    print('Desktop ' + desktop.id + ' screen=' + desktop.screen
          + ' geometry=' + describeGeometry(geometry) + ' path=' + crop.path);
    desktop.wallpaperPlugin = 'org.kde.image';
    desktop.currentConfigGroup = ['Wallpaper', 'org.kde.image', 'General'];
    desktop.writeConfig('Image', crop.path);
}
)").arg(cropArray);

    auto message = QDBusMessage::createMethodCall(
            "org.kde.plasmashell",
            "/PlasmaShell", "org.kde.PlasmaShell",
            "evaluateScript");
    message.setArguments(QVariantList() << script);

    qDebug() << "Applying crops by virtual-desktop geometry:" << crops;
    const auto reply = QDBusConnection::sessionBus().call(message);
    if(reply.type() == QDBusMessage::ErrorMessage) {
        qCritical() << "Something went wrong.";
        qCritical() << reply.errorMessage();
    } else if (!reply.arguments().isEmpty()) {
        qDebug().noquote() << "Plasma assignment:\n"
                           << reply.arguments().constFirst().toString();
    }

    QApplication::quit();
}

/**
 * Splits the image and saves the resulting wallpapers in a subdirectory
 */
void WallpaperSplitter::saveWallpapers() {
    splitImage();
}

/**
 * Calculates the total size of all screens combined.
 */
QSize WallpaperSplitter::totalScreenSize() {
    auto screens = QApplication::screens();
    // get combined height and width of all screens
    auto *screensRect = new QGraphicsItemGroup();
    std::for_each(screens.begin(), screens.end(), [&](const QScreen* item){
        screensRect->addToGroup(new QGraphicsRectItem(item->geometry()));
    });

    // subtract the width of the stroke
    return screensRect->sceneBoundingRect().size().toSize() - QSize(1, 1);
}

void WallpaperSplitter::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    scaleView();
}

void WallpaperSplitter::scaleView() {
    auto scene = ui->graphicsView->scene();
    if (scene == nullptr || scene->items().isEmpty()) return;

    if (imageItem == nullptr) {
        // The empty-state label ignores view transforms so it stays legible.
        // Scaling its scene bounds would move it as the viewport changes.
        ui->graphicsView->resetTransform();
        ui->graphicsView->centerOn(scene->itemsBoundingRect().center());
        return;
    }

    ui->graphicsView->fitInView(scene->itemsBoundingRect(), Qt::KeepAspectRatio);
    ui->graphicsView->centerOn(imageItem);
}

WallpaperSplitter::~WallpaperSplitter() {
    delete ui;
}

void WallpaperSplitter::addImage(QImage &image) {
    ui->graphicsView->scene()->clear();

    imageItem = new ResizableImageItem(image);
    ui->graphicsView->scene()->addItem(imageItem);

    screenGroup = new ScreensItem(imageItem);
    imageItem->setScreenGroup(screenGroup);

    // scale the desktops so that the image fits
    const auto screenSize = totalScreenSize();
    if(image.width() < screenSize.width() || image.height() < screenSize.height()) {
        // this is hacky and not performant at all
        auto imageScaled = image.scaled(screenSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        qreal scale = (float) imageScaled.width() / (float) image.width();
        screenGroup->setScale(1 / scale);
        screenGroup->setPos(imageItem->scenePos());
    }

    // Screen rectangles use virtual-desktop coordinates and may have a non-zero
    // or negative origin. Position the group's bounding-rectangle center on the
    // image center instead of adding the image center as an offset.
    screenGroup->setPos(imageItem->boundingRect().center()
                        - screenGroup->boundingRect().center());

    scaleView();
}

void WallpaperSplitter::addImage(const QUrl &url) {
    if(url.isEmpty()) return;

    imageFile = new QFileInfo(url.path());
    auto image = new QImage(imageFile->filePath());
    qDebug() << "Image" << imageFile->fileName() << "selected.";
    addImage(*image);
}
