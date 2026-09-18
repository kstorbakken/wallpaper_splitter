//
// Created by l0drex on 16.09.21.
//

#ifndef WALLPAPER_SPLITTER_SCREENSITEM_H
#define WALLPAPER_SPLITTER_SCREENSITEM_H


#include <QGraphicsItemGroup>
#include "monitorlayout.h"

enum ScalingMode {none, vertical, horizontal, diagonal};

class ScreensItem : public QGraphicsItemGroup {
public:
    explicit ScreensItem(QGraphicsItem *parent);
    ScreensItem(QGraphicsItem *parent, const QList<MonitorInfo> &monitors,
                const MonitorPreferences &preferences);
    const QList<MonitorInfo> &monitors() const { return monitorList; }

    const QList<QGraphicsRectItem *> &getRectangles() const;
    void constrainToParent();
    QRectF layoutBounds() const;
    void fitAsLargeAsPossible();

private:
    ScalingMode scalingMode = ScalingMode::none;
    QList<QGraphicsRectItem*> rectangles{};
    qreal maxScale;

    QList<MonitorInfo> monitorList;
    MonitorPreferences preferences;
    void addScreens();
    void updateMaximumScale();
    QPointF constrainedPosition(const QPointF &position) const;

protected:
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
};


#endif //WALLPAPER_SPLITTER_SCREENSITEM_H
