//
// Created by l0drex on 30.09.21.
//

#ifndef WALLPAPER_SPLITTER_GRAPHICSVIEW_H
#define WALLPAPER_SPLITTER_GRAPHICSVIEW_H


#include <QGraphicsView>
#include "wallpapersplitter.h"

static const qreal ZOOM_AMOUNT = .1;

class QToolButton;

class GraphicsView : public QGraphicsView {
public:
    explicit GraphicsView(WallpaperSplitter *parent = nullptr);
    QToolButton *stretchButton() const { return stretchImageButton; }
    QToolButton *panoramaButton() const { return fillPanoramaButton; }

private:
    WallpaperSplitter* parent;
    QPoint lastCursorPosition;
    bool panning{false};
    QToolButton *stretchImageButton{};
    QToolButton *fillPanoramaButton{};

protected:
    bool viewportEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

    void dragMoveEvent(QDragMoveEvent *event) override;
};


#endif //WALLPAPER_SPLITTER_GRAPHICSVIEW_H
