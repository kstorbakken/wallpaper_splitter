#ifndef WALLPAPER_SPLITTER_CENTEREDTEXTITEM_H
#define WALLPAPER_SPLITTER_CENTEREDTEXTITEM_H

#include <QGraphicsTextItem>

class CenteredTextItem final : public QGraphicsTextItem {
public:
    explicit CenteredTextItem(const QString &text, const QPointF &center,
                              QGraphicsItem *parent = nullptr);

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget = nullptr) override;

private:
    QPointF contentOffset() const;
};

#endif // WALLPAPER_SPLITTER_CENTEREDTEXTITEM_H
