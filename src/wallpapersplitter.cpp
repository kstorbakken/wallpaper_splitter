#include "wallpapersplitter.h"

#include <QApplication>
#include <QFileDialog>
#include <QGraphicsScene>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QStandardPaths>

#include "graphicsview.h"
#include "resizableimageitem.h"
#include "settingsdialog.h"
#include "ui_wallpapersplitter.h"

WallpaperSplitter::WallpaperSplitter(QWidget *parent)
        : QDialog(parent), ui(new Ui::WallpaperSplitter), preferences(AppSettings::load()) {
    ui->setupUi(this);
    auto *graphicsView = new GraphicsView(this);
    delete ui->verticalLayout->replaceWidget(ui->graphicsView, graphicsView)->widget();
    ui->graphicsView = graphicsView;

    auto *scene = new QGraphicsScene(this);
    ui->graphicsView->setScene(scene);
    auto *text = scene->addText(tr("Drop an image here"));
    text->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    const QRectF rect = text->boundingRect();
    text->setTransformOriginPoint(rect.center());
    text->setPos(-rect.width() / 2.0, -rect.height() / 2.0);
    ui->graphicsView->centerOn(text);

    auto *applyButton = ui->buttonBox->button(QDialogButtonBox::Ok);
    auto *exportButton = ui->buttonBox->button(QDialogButtonBox::Save);
    applyButton->setText(tr("Apply"));
    exportButton->setText(tr("Export"));
    applyButton->setEnabled(false);
    exportButton->setEnabled(false);

    connect(ui->buttonBoxOpen, &QDialogButtonBox::accepted,
            this, &WallpaperSplitter::selectImage);
    connect(applyButton, &QPushButton::pressed, this, &WallpaperSplitter::applyWallpaper);
    connect(exportButton, &QPushButton::pressed, this, &WallpaperSplitter::exportWallpapers);
    connect(ui->settingsButton, &QPushButton::pressed, this, &WallpaperSplitter::showSettings);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

WallpaperSplitter::~WallpaperSplitter() {
    delete ui;
}

void WallpaperSplitter::selectImage() {
    const QUrl url = QFileDialog::getOpenFileUrl(
            this, tr("Select a wallpaper image"), QUrl::fromLocalFile(preferences.inputDirectory),
            tr("Images (*.jpg *.jpeg *.png *.bmp *.webp)"));
    addImage(url);
}

void WallpaperSplitter::displayImage(const QImage &image) {
    ui->graphicsView->scene()->clear();
    imageItem = new ResizableImageItem(image);
    ui->graphicsView->scene()->addItem(imageItem);
    screenGroup = new ScreensItem(imageItem);
    imageItem->setScreenGroup(screenGroup);

    const QSize screenSize = totalScreenSize();
    if (image.width() < screenSize.width() || image.height() < screenSize.height()) {
        const QImage scaled = image.scaled(screenSize, Qt::KeepAspectRatioByExpanding,
                                           Qt::SmoothTransformation);
        const qreal scale = static_cast<qreal>(scaled.width()) / image.width();
        screenGroup->setScale(1 / scale);
        screenGroup->setPos(imageItem->scenePos());
    }
    screenGroup->setPos(imageItem->boundingRect().center()
                        - screenGroup->boundingRect().center());
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(true);
    ui->buttonBox->button(QDialogButtonBox::Save)->setEnabled(true);
    scaleView();
}

void WallpaperSplitter::addImage(QImage &image) {
    if (image.isNull()) {
        QMessageBox::warning(this, tr("Could not open image"),
                             tr("The dropped image could not be loaded."));
        return;
    }
    imageFile = QFileInfo();
    originalImageSize = image.size();
    displayImage(image);
}

void WallpaperSplitter::addImage(const QUrl &url) {
    if (url.isEmpty()) return;
    if (!url.isLocalFile()) {
        QMessageBox::warning(this, tr("Could not open image"),
                             tr("Only local image files are supported."));
        return;
    }
    const QFileInfo candidate(url.toLocalFile());
    const QImage image(candidate.filePath());
    if (image.isNull()) {
        QMessageBox::warning(this, tr("Could not open image"),
                             tr("The image %1 could not be loaded.").arg(candidate.filePath()));
        return;
    }
    imageFile = candidate;
    originalImageSize = image.size();
    preferences.inputDirectory = candidate.absolutePath();
    AppSettings::save(preferences);
    displayImage(image);
}

QList<ScreenCrop> WallpaperSplitter::currentScreenCrops() const {
    QList<ScreenCrop> result;
    if (screenGroup == nullptr) return result;
    const auto rectangles = screenGroup->getRectangles();
    const auto screens = QApplication::screens();
    for (int index = 0; index < rectangles.size(); ++index) {
        const QScreen *screen = screens.value(index);
        const QString screenName = screen == nullptr || screen->name().isEmpty()
                ? QStringLiteral("screen-%1").arg(index + 1) : screen->name();
        result.append({screenName, index + 1,
                       screen == nullptr ? QRect() : screen->geometry(),
                       screenGroup->mapRectToParent(rectangles.at(index)->rect()).toAlignedRect()});
    }
    return result;
}

QString WallpaperSplitter::sourceName() const {
    return imageFile.isFile() ? imageFile.fileName() : QStringLiteral("wallpaper");
}

void WallpaperSplitter::exportWallpapers() {
    const QString directory = QFileDialog::getExistingDirectory(
            this, tr("Export wallpaper crops"), preferences.exportDirectory,
            QFileDialog::ShowDirsOnly);
    if (directory.isEmpty()) return;

    ExportOptions options{directory, preferences.fileNameTemplate, preferences.collisionPolicy};
    OperationResult result = OutputService::exportCrops(
            imageItem->image(), currentScreenCrops(), sourceName(), options);
    if (!result.success && result.error == OperationError::Collision
        && options.collisionPolicy == CollisionPolicy::Ask) {
        QMessageBox question(QMessageBox::Question, tr("Export files already exist"),
                             tr("Replace the existing crop files or create a new revision?"),
                             QMessageBox::Cancel, this);
        QPushButton *replace = question.addButton(tr("Replace"), QMessageBox::DestructiveRole);
        QPushButton *revision = question.addButton(tr("New Revision"), QMessageBox::AcceptRole);
        question.exec();
        if (question.clickedButton() == replace) options.collisionPolicy = CollisionPolicy::Replace;
        else if (question.clickedButton() == revision) options.collisionPolicy = CollisionPolicy::Revision;
        else return;
        result = OutputService::exportCrops(
                imageItem->image(), currentScreenCrops(), sourceName(), options);
    }
    if (!result.success) {
        showOperationError(result);
        return;
    }
    preferences.exportDirectory = directory;
    AppSettings::save(preferences);
}

void WallpaperSplitter::applyWallpaper() {
    DBusPlasmaApplicator applicator;
    const OperationResult result = OutputService::applyManaged(
            imageItem->image(), originalImageSize, currentScreenCrops(), sourceName(),
            imageFile.isFile() ? imageFile.absoluteFilePath() : QString(), applicator);
    if (!result.success) {
        showOperationError(result);
        return;
    }
    accept();
}

void WallpaperSplitter::showSettings() {
    SettingsDialog dialog(preferences, this);
    if (dialog.exec() != QDialog::Accepted) return;
    preferences = dialog.preferences();
    AppSettings::save(preferences);
}

void WallpaperSplitter::showOperationError(const OperationResult &result) {
    QMessageBox::critical(this, tr("Wallpaper operation failed"), result.message);
}

QStringList WallpaperSplitter::splitImage(const QImage &image, const QList<QRect> &screens,
                                          const QString &path, const QString &outputBaseName) {
    QList<ScreenCrop> crops;
    for (int index = 0; index < screens.size(); ++index) {
        crops.append({QStringLiteral("screen-%1").arg(index + 1), index + 1, {}, screens.at(index)});
    }
    const ExportOptions options{path, QStringLiteral("{source}-{number}-{digest}"),
                                CollisionPolicy::Replace};
    const OperationResult result = OutputService::exportCrops(image, crops, outputBaseName, options);
    if (!result.success) qWarning().noquote() << result.message;
    return result.paths;
}

QStringList WallpaperSplitter::splitImage(const QImage &image, const QString &path,
                                          const QPoint topLeft, const QPoint bottomRight,
                                          const QString &outputBaseName) {
    QList<QRect> geometries;
    const auto screens = QApplication::screens();
    for (const QScreen *screen : screens) {
        QRect geometry = screen->geometry();
        const QPoint delta = screen->geometry().topLeft() - screens.first()->geometry().topLeft();
        geometry.moveTopLeft(topLeft + delta);
        if (bottomRight.manhattanLength() > 0) {
            geometry.setSize(geometry.size().scaled(bottomRight.x(), bottomRight.y(),
                                                    Qt::KeepAspectRatio));
        }
        geometries.append(geometry);
    }
    return splitImage(image, geometries, path, outputBaseName);
}

QSize WallpaperSplitter::totalScreenSize() {
    QRect screensRect;
    for (const QScreen *screen : QApplication::screens()) {
        screensRect = screensRect.isNull() ? screen->geometry() : screensRect.united(screen->geometry());
    }
    return screensRect.size();
}

void WallpaperSplitter::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    scaleView();
}

void WallpaperSplitter::scaleView() {
    QGraphicsScene *scene = ui->graphicsView->scene();
    if (scene == nullptr || scene->items().isEmpty()) return;
    ui->graphicsView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->graphicsView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (imageItem == nullptr) {
        ui->graphicsView->resetTransform();
        ui->graphicsView->centerOn(scene->itemsBoundingRect().center());
        return;
    }
    ui->graphicsView->fitInView(scene->itemsBoundingRect(), Qt::KeepAspectRatio);
    ui->graphicsView->centerOn(imageItem);
}
