#include "wallpapersplitter.h"

#include <QApplication>
#include <QComboBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QLabel>
#include <QProgressDialog>
#include <QTimer>
#include "batchsplitter.h"
#include "wallpaperpreview.h"
#include <QFileDialog>
#include <QGraphicsScene>
#include <QMessageBox>
#include <QMenu>
#include <QStyle>
#include <QPushButton>
#include <QToolButton>
#include <QScreen>
#include <QStandardPaths>

#include "graphicsview.h"
#include "resizableimageitem.h"
#include "settingsdialog.h"
#include "setlibrarydialog.h"
#include "monitorsdialog.h"
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

    auto *openButton = ui->buttonBoxOpen->button(QDialogButtonBox::Open);
    openButton->setText(tr("Open"));
    ui->settingsButton->setIcon(QIcon::fromTheme(QStringLiteral("configure"),
                                               style()->standardIcon(QStyle::SP_FileDialogDetailedView)));
    auto *openMenu = new QMenu(openButton);
    openMenu->addAction(tr("Open image…"), this, &WallpaperSplitter::selectImage);
    auto *folderAction = openMenu->addAction(tr("Split folder…"), this, &WallpaperSplitter::splitFolder);
    folderAction->setObjectName(QStringLiteral("splitFolderAction"));
    openButton->setMenu(openMenu);
    auto *applyButton = ui->buttonBox->button(QDialogButtonBox::Ok);
    auto *exportButton = ui->exportButton;
    previewButton = new QPushButton(tr("Preview"), this);
    previewButton->setObjectName(QStringLiteral("previewButton"));
    previewButton->setAutoDefault(false);
    previewButton->setEnabled(false);
    previewButton->setToolTip(tr("Temporarily try this wallpaper, then restore the previous one."));
    ui->footerLayout->insertWidget(ui->footerLayout->count() - 2, previewButton);
    connect(previewButton, &QPushButton::clicked, this, &WallpaperSplitter::previewWallpaper);
    applyButton->setText(tr("Apply"));
    exportButton->setText(tr("Export"));
    exportButton->setIcon(QIcon::fromTheme(QStringLiteral("document-save-as"),
                                         style()->standardIcon(QStyle::SP_DialogSaveButton)));
    exportButton->setObjectName(QStringLiteral("exportButton"));
    exportButton->setAutoDefault(false);
    applyButton->setDefault(true);
    applyButton->setEnabled(false);
    exportButton->setEnabled(false);

    connect(ui->buttonBoxOpen, &QDialogButtonBox::accepted,
            this, &WallpaperSplitter::selectImage);
    connect(applyButton, &QPushButton::pressed, this, &WallpaperSplitter::applyWallpaper);
    connect(exportButton, &QPushButton::pressed, this, &WallpaperSplitter::exportWallpapers);
    connect(graphicsView->stretchButton(), &QToolButton::clicked, this, [this] {
        toggleLayoutMode(LayoutMode::Stretch);
    });
    connect(graphicsView->panoramaButton(), &QToolButton::clicked, this, [this] {
        toggleLayoutMode(LayoutMode::Panorama);
    });
    auto *libraryButton = new QPushButton(tr("Library"), this);
    libraryButton->setObjectName("libraryButton");
    libraryButton->setIcon(QIcon::fromTheme(QStringLiteral("folder-pictures"),
                                          style()->standardIcon(QStyle::SP_DirIcon)));
    ui->footerLayout->insertWidget(1, libraryButton);
    connect(libraryButton, &QPushButton::clicked, this, [this] {
        SetLibraryDialog dialog(this);
        dialog.exec();
    });
    auto *settingsMenu = new QMenu(ui->settingsButton);
    ui->settingsButton->setMenu(settingsMenu);
    settingsMenu->addAction(ui->settingsButton->icon(), tr("General"), this, &WallpaperSplitter::showSettings);
    auto *monitorsAction = settingsMenu->addAction(QIcon::fromTheme(QStringLiteral("video-display"),
            style()->standardIcon(QStyle::SP_ComputerIcon)), tr("Monitors"));
    monitorsAction->setObjectName(QStringLiteral("monitorsAction"));
    connect(monitorsAction, &QAction::triggered, this, [this] {
        MonitorsDialog dialog(MonitorLayout::connectedMonitors(), AppSettings::loadMonitors(), this);
        if (dialog.exec() != QDialog::Accepted) return;
        AppSettings::saveMonitors(dialog.preferences());
        refreshMonitors();
    });
    const auto watchScreen = [this](QScreen *screen) {
        connect(screen, &QScreen::geometryChanged, this, [this] { refreshMonitors(); });
        connect(screen, &QScreen::physicalSizeChanged, this, [this] { refreshMonitors(); });
    };
    for (auto *screen : QApplication::screens()) watchScreen(screen);
    connect(qApp, &QGuiApplication::screenAdded, this, [this, watchScreen](QScreen *screen) {
        watchScreen(screen);
        refreshMonitors();
    });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this] { refreshMonitors(); });
    for (auto *button : {ui->buttonBoxOpen->button(QDialogButtonBox::Open),
                         libraryButton, ui->settingsButton}) {
        button->setAutoDefault(false);
        button->setDefault(false);
    }
    applyButton->setDefault(true);

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
    screenGroup = nullptr;
    layoutMode = LayoutMode::Original;
    refreshMonitors();
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(true);
    ui->exportButton->setEnabled(true);
    previewButton->setEnabled(true);
    auto *view = static_cast<GraphicsView *>(ui->graphicsView);
    for (auto *button : {view->stretchButton(), view->panoramaButton()}) {
        button->setEnabled(true);
        button->show();
    }
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
    const auto &screens = screenGroup->monitors();
    for (int index = 0; index < rectangles.size(); ++index) {
        result.append({screens[index].name, index + 1, screens[index].desktopGeometry,
                       screenGroup->mapRectToParent(rectangles.at(index)->rect()).toAlignedRect()
                           .intersected(imageItem->image().rect())});
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

    ExportOptions options{directory, preferences.fileNameTemplate, preferences.collisionPolicy,
                          preferences.outputFormat, preferences.jpegQuality};
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
    if (preferences.closeAfterApply) accept();
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
                                CollisionPolicy::Replace, OutputFormat::Png};
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

void WallpaperSplitter::refreshMonitors() {
    if (imageItem == nullptr) return;
    delete screenGroup;
    const auto monitors = MonitorLayout::connectedMonitors();
    const auto settings = AppSettings::loadMonitors();
    screenGroup = new ScreensItem(imageItem, monitors, settings);
    imageItem->setScreenGroup(screenGroup);
    if (layoutMode == LayoutMode::Panorama) {
        imageItem->fillToLayout();
    } else if (layoutMode == LayoutMode::Stretch) {
        imageItem->stretchToLayout();
    } else {
        screenGroup->constrainToParent();
        screenGroup->setPos(imageItem->boundingRect().center() - screenGroup->layoutBounds().center());
    }
    auto *view = static_cast<GraphicsView *>(ui->graphicsView);
    view->stretchButton()->setChecked(layoutMode == LayoutMode::Stretch);
    view->panoramaButton()->setChecked(layoutMode == LayoutMode::Panorama);
    scaleView();
}

void WallpaperSplitter::toggleLayoutMode(LayoutMode mode) {
    if (imageItem == nullptr) return;
    layoutMode = layoutMode == mode ? LayoutMode::Original : mode;
    imageItem->restoreOriginal();
    refreshMonitors();
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

void WallpaperSplitter::splitFolder() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Split folder"));
    dialog.resize(580, 300);
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    const auto folderRow = [&dialog, form](const QString &label, const QString &initial) {
        auto *row = new QWidget(&dialog);
        auto *line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        auto *edit = new QLineEdit(initial, row);
        auto *browse = new QPushButton(QObject::tr("Browse"), row);
        browse->setAutoDefault(false);
        line->addWidget(edit);
        line->addWidget(browse);
        QObject::connect(browse, &QPushButton::clicked, &dialog, [&dialog, edit] {
            const auto folder = QFileDialog::getExistingDirectory(&dialog, QObject::tr("Select folder"), edit->text());
            if (!folder.isEmpty()) edit->setText(folder);
        });
        form->addRow(label, row);
        return edit;
    };
    auto *input = folderRow(tr("Source folder:"), preferences.inputDirectory);
    auto *target = new QComboBox(&dialog);
    target->setObjectName(QStringLiteral("batchDestination"));
    target->addItems({tr("Export"), tr("Library (save without applying)")});
    form->addRow(tr("Save to:"), target);
    auto *output = folderRow(tr("Export folder:"), preferences.exportDirectory);
    auto *mode = new QComboBox(&dialog);
    mode->setObjectName(QStringLiteral("batchLayout"));
    mode->addItems({tr("Centered largest fit"), tr("Stretch across screens"), tr("Fill panorama")});
    mode->setCurrentIndex(int(preferences.batchLayout));
    form->addRow(tr("Layout for each image:"), mode);
    auto *collision = new QComboBox(&dialog);
    collision->setObjectName(QStringLiteral("batchCollisionPolicy"));
    collision->addItem(tr("Create a new revision"), int(CollisionPolicy::Revision));
    collision->addItem(tr("Replace existing files"), int(CollisionPolicy::Replace));
    collision->addItem(tr("Skip existing files"), int(CollisionPolicy::Fail));
    collision->setCurrentIndex(qMax(0, collision->findData(int(preferences.batchCollisionPolicy))));
    form->addRow(tr("When export files exist:"), collision);
    connect(target, &QComboBox::currentIndexChanged, &dialog, [output, collision](int index) {
        output->parentWidget()->setEnabled(index == 0);
        collision->setEnabled(index == 0);
    });
    target->setCurrentIndex(preferences.batchToLibrary ? 1 : 0);
    layout->addLayout(form);
    auto *help = new QLabel(tr("Batch choices are remembered for next time. Processes images directly inside the folder using your monitor settings, image format, "
                               "JPEG quality and filename template. Subfolders are excluded. "
                               "Library destination: %1").arg(OutputService::defaultManagedRoot()), &dialog);
    help->setWordWrap(true);
    layout->addWidget(help);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Split folder"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (!QFileInfo(input->text()).isDir()) {
            QMessageBox::warning(&dialog, tr("Invalid folder"), tr("Choose an existing source folder."));
            return;
        }
        if (target->currentIndex() == 0 && (output->text().trimmed().isEmpty()
            || !QDir().mkpath(output->text())
            || QFileInfo(input->text()).canonicalFilePath() == QFileInfo(output->text()).canonicalFilePath())) {
            QMessageBox::warning(&dialog, tr("Invalid export folder"),
                                 tr("Choose a writable export folder different from the source folder."));
            return;
        }
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted) return;
    preferences.batchLayout = static_cast<BatchLayout>(mode->currentIndex());
    preferences.batchToLibrary = target->currentIndex() == 1;
    preferences.batchCollisionPolicy = static_cast<CollisionPolicy>(collision->currentData().toInt());
    preferences.inputDirectory = QDir(input->text()).absolutePath();
    if (!preferences.batchToLibrary) preferences.exportDirectory = QDir(output->text()).absolutePath();
    AppSettings::save(preferences);
    const auto files = BatchSplitter::imagesInFolder(input->text());
    if (files.isEmpty()) {
        QMessageBox::information(this, tr("Split folder"), tr("No supported images were found in this folder."));
        return;
    }
    const auto monitors = MonitorLayout::connectedMonitors();
    const auto monitorPreferences = AppSettings::loadMonitors();
    const auto batchMode = static_cast<BatchLayout>(mode->currentIndex());
    ExportOptions options{output->text(), preferences.fileNameTemplate,
                          static_cast<CollisionPolicy>(collision->currentData().toInt()),
                          preferences.outputFormat, preferences.jpegQuality};
    QProgressDialog progress(tr("Splitting images…"), tr("Cancel"), 0, files.size(), this);
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    int completed = 0;
    int processed = 0;
    QStringList problems;
    for (const auto &path : files) {
        progress.setValue(processed);
        progress.setLabelText(tr("Splitting %1").arg(QFileInfo(path).fileName()));
        QApplication::processEvents();
        if (progress.wasCanceled()) break;
        const QImage source(path);
        QImage image;
        QList<ScreenCrop> screens;
        auto result = BatchSplitter::prepare(source, monitors, monitorPreferences, batchMode, &image, &screens);
        if (result.success) {
            const QFileInfo file(path);
            result = target->currentIndex() == 0
                ? OutputService::exportCrops(image, screens, file.fileName(), options)
                : OutputService::saveManaged(image, source.size(), screens, file.fileName(), path, {},
                                              preferences.outputFormat, preferences.jpegQuality);
        }
        if (result.success) ++completed;
        else problems.append(tr("%1: %2").arg(QFileInfo(path).fileName(), result.message));
        ++processed;
    }
    const bool canceled = progress.wasCanceled();
    progress.setValue(files.size());
    QMessageBox summary(QMessageBox::Information, tr("Split folder"),
        tr("%1 of %2 images saved. %3 failed or skipped.%4")
            .arg(completed).arg(files.size()).arg(problems.size())
            .arg(canceled ? tr(" Processing was canceled; completed images were kept.") : QString()),
        QMessageBox::Ok, this);
    if (!problems.isEmpty()) summary.setDetailedText(problems.join('\n'));
    summary.exec();
}

void WallpaperSplitter::previewWallpaper() {
    if (!imageItem) return;
    DBusPlasmaApplicator applicator;
    WallpaperPreview preview(applicator);
    const auto started = preview.start(imageItem->image(), currentScreenCrops());
    if (!started.success) {
        showOperationError(started);
        if (!preview.active()) return;
    }
    // Every way of closing the dialog passes through restoration. A failure
    // leaves the dialog and temporary files available for another attempt.
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Wallpaper preview"));
    dialog.setWindowFlag(Qt::WindowStaysOnTopHint);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setSizeConstraint(QLayout::SetFixedSize);
    auto *label = new QLabel(&dialog);
    label->setWordWrap(true);
    label->setMinimumWidth(label->fontMetrics().averageCharWidth() * 48);
    layout->addWidget(label);
    auto *end = new QPushButton(tr("End Preview"), &dialog);
    end->setObjectName(QStringLiteral("endPreviewButton"));
    layout->addWidget(end);
    int remaining = preferences.previewSeconds;
    const auto updateLabel = [&] { label->setText(tr("Your previous wallpaper will return in %1 seconds.").arg(remaining)); };
    updateLabel();
    QTimer timer;
    timer.setInterval(1000);
    connect(end, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(&timer, &QTimer::timeout, &dialog, [&] {
        if (--remaining <= 0) dialog.accept();
        else updateLabel();
    });
    timer.start();
    const bool wasMaximized = isMaximized();
    showMinimized();
    for (;;) {
        dialog.exec();
        timer.stop();
        const auto restored = preview.finish();
        if (restored.success) break;
        showOperationError(restored);
        label->setText(tr("The previous wallpaper could not be restored. Click End Preview to retry."));
    }
    if (wasMaximized) showMaximized();
    else showNormal();
    raise();
    activateWindow();
}
