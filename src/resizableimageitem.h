#ifndef WALLPAPER_SPLITTER_RESIZABLEIMAGEITEM_H
#define WALLPAPER_SPLITTER_RESIZABLEIMAGEITEM_H

#include <QGraphicsPixmapItem>
#include <QImage>

class ScreensItem;
class QGraphicsRectItem;

class ResizableImageItem : public QGraphicsPixmapItem {
public:
    enum class Corner { TopLeft, TopRight, BottomLeft, BottomRight };

    explicit ResizableImageItem(const QImage &image);

    const QImage &image() const;
    void setScreenGroup(ScreensItem *screens);
    void beginResize(Corner corner);
    void resizeTo(const QPointF &scenePosition, bool keepAspectRatio = false);

private:
    QImage sourceImage;
    QImage scaledImage;
    ScreensItem *screenGroup{};
    Corner activeCorner{Corner::BottomRight};
    QPointF fixedSceneCorner;
    QGraphicsRectItem *frame{};

    QSize minimumSize() const;
    QPointF cornerPosition(Corner corner) const;
    void setSize(const QSize &size);
    void updateHandles();
};

#endif // WALLPAPER_SPLITTER_RESIZABLEIMAGEITEM_H
