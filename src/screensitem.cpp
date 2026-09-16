//
// Created by l0drex on 16.09.21.
//

#include <KColorScheme>
#include <QScreen>
#include <QPen>
#include <QApplication>
#include <QGraphicsSceneMouseEvent>
#include <QDebug>
#include <QGraphicsScene>
#include <QGraphicsView>
#include "centeredtextitem.h"
#include "screensitem.h"
#include "appsettings.h"

ScreensItem::ScreensItem(QGraphicsItem *parent)
    : ScreensItem(parent, MonitorLayout::connectedMonitors(), AppSettings::loadMonitors()) {}

ScreensItem::ScreensItem(QGraphicsItem *parent, const QList<MonitorInfo> &monitors,
                         const MonitorPreferences &preferences)
    : QGraphicsItemGroup(parent), monitorList(monitors), preferences(preferences) {
    addScreens();

    // make the screen item movable by the user
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
    setFlag(ItemIsMovable);
    setFlag(ItemSendsGeometryChanges);
    setTransformOriginPoint(boundingRect().center());

    updateMaximumScale();
}

void ScreensItem::addScreens() {
    // get the currently used color scheme
    const auto colorScheme = KColorScheme();
    const auto pen = QPen(colorScheme.foreground(KColorScheme::ForegroundRole::ActiveText), 10);

    const auto layout = MonitorLayout::rectangles(monitorList, preferences);
    for (int index = 0; index < layout.size(); ++index) {
        const auto rect = new QGraphicsRectItem(layout[index]);
        rect->setPen(pen);
        rect->setBrush(colorScheme.background(KColorScheme::BackgroundRole::ActiveBackground));
        rect->setOpacity(0.75);
        addToGroup(rect);
        rectangles.append(rect);
        auto *name = new CenteredTextItem(monitorList[index].displayName.isEmpty()
            ? monitorList[index].name : monitorList[index].displayName, layout[index].center());
        addToGroup(name);
    }
}

const QList<QGraphicsRectItem *> &ScreensItem::getRectangles() const {
    return rectangles;
}

void ScreensItem::updateMaximumScale() {
    const QRectF screens = childrenBoundingRect();
    const QRectF image = parentItem()->boundingRect();
    maxScale = screens.isEmpty() ? 1.0 : qMin(image.width() / screens.width(), image.height() / screens.height());
}

QPointF ScreensItem::constrainedPosition(const QPointF &position) const {
    QRectF screens = mapRectToParent(boundingRect());
    screens.translate(position - pos());
    const QRectF image = parentItem()->boundingRect();
    QPointF correction;
    if (screens.left() < image.left()) correction.setX(image.left() - screens.left());
    else if (screens.right() > image.right()) correction.setX(image.right() - screens.right());
    if (screens.top() < image.top()) correction.setY(image.top() - screens.top());
    else if (screens.bottom() > image.bottom()) correction.setY(image.bottom() - screens.bottom());
    return position + correction;
}

void ScreensItem::constrainToParent() {
    updateMaximumScale();
    if (scale() > maxScale) setScale(maxScale);
    setPos(constrainedPosition(pos()));
}

void ScreensItem::mousePressEvent(QGraphicsSceneMouseEvent *event) {
    if(event->button() == Qt::RightButton) {
        const auto pos = transformOriginPoint() - event->pos();

        // check if cursor is far enough to the edge  before setting a scaling mode
        // this prevents uncontrollable behaviour in the middle, where the mode cannot be detected
        if (qAbs(pos.x()) > qAbs(transformOriginPoint().x() * .6) || qAbs(pos.y()) > qAbs(transformOriginPoint().y() * .6)) {
            if (-.3 * sceneBoundingRect().height() < pos.y() && pos.y() < .3 * sceneBoundingRect().height()) {
                setCursor(Qt::SizeHorCursor);
                scalingMode = horizontal;
            } else if (-.3 * sceneBoundingRect().width() < pos.x() && pos.x() < .3 * sceneBoundingRect().width()) {
                setCursor(Qt::SizeVerCursor);
                scalingMode = vertical;
            } else {
                scalingMode = diagonal;
                if (pos.x() * pos.y() > 0) setCursor(Qt::SizeFDiagCursor);
                else setCursor(Qt::SizeBDiagCursor);
            }
            event->accept();
        }
    } else if (event->button() == Qt::LeftButton) {
        setCursor(Qt::DragMoveCursor);
    }
    QGraphicsItem::mousePressEvent(event);
}

void ScreensItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
    if (event->buttons() == Qt::RightButton) {
        const auto mouseMovement = event->pos() - event->buttonDownPos(Qt::RightButton);
        // this is the start position of the action relative to the transform origin (the center) of this item
        const auto mouseStart = event->buttonDownPos(Qt::RightButton) - transformOriginPoint();

        // diagonal scaling uses the max difference
        if (scalingMode == diagonal) {
            if (mouseMovement.x() >= mouseMovement.y())
                scalingMode = horizontal;
            else
                scalingMode = vertical;
        }

        qreal newScale;
        switch (scalingMode) {
            case horizontal:
                newScale = mouseMovement.x() / transformOriginPoint().x();
                if (mouseStart.x() > 0) newScale *= -1;
                break;

            case vertical:
                newScale = mouseMovement.y() / transformOriginPoint().y();
                if (mouseStart.y() > 0) newScale *= -1;
                break;

            default:
                event->ignore();
                return;
        }
        setScale(scale() * (1 - newScale));
        constrainToParent();
        event->accept();
    }
    QGraphicsItem::mouseMoveEvent(event);
}

void ScreensItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
    if(event->button() == Qt::MouseButton::RightButton) scalingMode = ScalingMode::none;
    unsetCursor();
    event->accept();
    QGraphicsItem::mouseReleaseEvent(event);
}

QVariant ScreensItem::itemChange(QGraphicsItem::GraphicsItemChange change, const QVariant &value) {
    switch (change) {
        case QGraphicsItem::ItemPositionChange: {
            return constrainedPosition(value.toPointF());
        }
        case QGraphicsItem::ItemScaleChange: {
            updateMaximumScale();
            qreal newScale = value.toDouble();
            if(newScale > maxScale) return maxScale;
            if(newScale < qMin(0.1, maxScale)) return qMin(0.1, maxScale);
            break;
        }
    }

    return QGraphicsItem::itemChange(change, value);
}
