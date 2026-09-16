#ifndef WALLPAPER_SPLITTER_WALLPAPERSPLITTER_H
#define WALLPAPER_SPLITTER_WALLPAPERSPLITTER_H

#include <QDialog>
#include <QFileInfo>
#include <QGraphicsItemGroup>

#include "appsettings.h"
#include "outputservice.h"
#include "screensitem.h"

class ResizableImageItem;

QT_BEGIN_NAMESPACE
namespace Ui { class WallpaperSplitter; }
QT_END_NAMESPACE

class WallpaperSplitter : public QDialog {
Q_OBJECT

public:
    explicit WallpaperSplitter(QWidget *parent = nullptr);
    ~WallpaperSplitter() override;
    static QStringList splitImage(const QImage &image, const QString &path,
                                  QPoint topLeft = {0, 0}, QPoint bottomRight = {0, 0},
                                  const QString &outputBaseName = "wallpaper");
    static QStringList splitImage(const QImage &image, const QList<QRect> &screens,
                                  const QString &path,
                                  const QString &outputBaseName = "wallpaper");
    void addImage(QImage &image);
    void addImage(const QUrl &url);

private:
    Ui::WallpaperSplitter *ui;
    ScreensItem *screenGroup{};
    ResizableImageItem *imageItem{};
    QFileInfo imageFile;
    QSize originalImageSize;
    UserPreferences preferences;

    void scaleView();
    void displayImage(const QImage &image);
    QList<ScreenCrop> currentScreenCrops() const;
    QString sourceName() const;
    void refreshMonitors();
    void showOperationError(const OperationResult &result);

private slots:
    void selectImage();
    void applyWallpaper();
    void exportWallpapers();
    void showSettings();

protected:
    void resizeEvent(QResizeEvent *event) override;
};

#endif
