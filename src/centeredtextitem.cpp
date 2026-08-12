#include <QPainter>
#include <QTransform>

#include "centeredtextitem.h"

CenteredTextItem::CenteredTextItem(const QString &text, const QPointF &center,
                                   QGraphicsItem *parent)
        : QGraphicsTextItem(text, parent) {
    adjustSize();
    setFlag(ItemIgnoresTransformations, true);
    setPos(center);
}

QPointF CenteredTextItem::contentOffset() const {
    return -QGraphicsTextItem::boundingRect().center();
}

QRectF CenteredTextItem::boundingRect() const {
    return QGraphicsTextItem::boundingRect().translated(contentOffset());
}

QPainterPath CenteredTextItem::shape() const {
    return QTransform::fromTranslate(contentOffset().x(), contentOffset().y())
            .map(QGraphicsTextItem::shape());
}

void CenteredTextItem::paint(QPainter *painter,
                             const QStyleOptionGraphicsItem *option,
                             QWidget *widget) {
    painter->save();
    painter->translate(contentOffset());
    QGraphicsTextItem::paint(painter, option, widget);
    painter->restore();
}
