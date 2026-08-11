#include <QBrush>
#include <QCursor>
#include <QGraphicsSceneMouseEvent>
#include <QPen>

#include "resizableimageitem.h"
#include "screensitem.h"

namespace {
constexpr qreal HANDLE_SIZE = 12.0;

class ResizeHandle final : public QGraphicsRectItem {
public:
    ResizeHandle(ResizableImageItem::Corner corner, ResizableImageItem *parent)
            : QGraphicsRectItem(-HANDLE_SIZE / 2, -HANDLE_SIZE / 2, HANDLE_SIZE, HANDLE_SIZE, parent),
              corner(corner), image(parent) {
        setBrush(Qt::white);
        setPen(QPen(Qt::black, 1));
        setZValue(1000);
        setFlag(ItemIgnoresTransformations);
        setAcceptedMouseButtons(Qt::LeftButton);
        setCursor(QCursor(corner == ResizableImageItem::Corner::TopRight
                                  || corner == ResizableImageItem::Corner::BottomLeft
                          ? Qt::SizeBDiagCursor : Qt::SizeFDiagCursor));
    }

    ResizableImageItem::Corner handleCorner() const {
        return corner;
    }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override {
        image->beginResize(corner);
        event->accept();
    }

    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override {
        image->resizeTo(event->scenePos(), event->modifiers().testFlag(Qt::ShiftModifier));
        event->accept();
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override {
        event->accept();
    }

private:
    ResizableImageItem::Corner corner;
    ResizableImageItem *image;
};
}

ResizableImageItem::ResizableImageItem(const QImage &image)
        : QGraphicsPixmapItem(QPixmap::fromImage(image)), sourceImage(image), scaledImage(image) {
    frame = new QGraphicsRectItem(this);
    frame->setBrush(Qt::NoBrush);
    frame->setPen(QPen(Qt::white, 2, Qt::DashLine));
    frame->setZValue(999);
    frame->setAcceptedMouseButtons(Qt::NoButton);
    for (const auto corner : {Corner::TopLeft, Corner::TopRight, Corner::BottomLeft, Corner::BottomRight}) {
        new ResizeHandle(corner, this);
    }
    updateHandles();
}

const QImage &ResizableImageItem::image() const {
    return scaledImage;
}

void ResizableImageItem::setScreenGroup(ScreensItem *screens) {
    screenGroup = screens;
}

void ResizableImageItem::beginResize(Corner corner) {
    activeCorner = corner;
    const Corner fixedCorner = corner == Corner::TopLeft ? Corner::BottomRight
            : corner == Corner::TopRight ? Corner::BottomLeft
            : corner == Corner::BottomLeft ? Corner::TopRight : Corner::TopLeft;
    fixedSceneCorner = mapToScene(cornerPosition(fixedCorner));
}

void ResizableImageItem::resizeTo(const QPointF &scenePosition, bool keepAspectRatio) {
    const bool resizeFromLeft = activeCorner == Corner::TopLeft
            || activeCorner == Corner::BottomLeft;
    const bool resizeFromTop = activeCorner == Corner::TopLeft
            || activeCorner == Corner::TopRight;
    const qreal width = qMax(1.0, resizeFromLeft
            ? fixedSceneCorner.x() - scenePosition.x()
            : scenePosition.x() - fixedSceneCorner.x());
    const qreal height = qMax(1.0, resizeFromTop
            ? fixedSceneCorner.y() - scenePosition.y()
            : scenePosition.y() - fixedSceneCorner.y());
    const QSize minSize = minimumSize();
    QSize requestedSize(qMax(1, qRound(width)), qMax(1, qRound(height)));
    if (keepAspectRatio) {
        const qreal requestedScale = qMax(width / sourceImage.width(), height / sourceImage.height());
        const qreal minimumScale = qMax(
                static_cast<qreal>(minSize.width()) / sourceImage.width(),
                static_cast<qreal>(minSize.height()) / sourceImage.height());
        const qreal scale = qMax(requestedScale, minimumScale);
        requestedSize = QSize(qMax(1, qRound(sourceImage.width() * scale)),
                              qMax(1, qRound(sourceImage.height() * scale)));
    } else {
        requestedSize.setWidth(qMax(requestedSize.width(), minSize.width()));
        requestedSize.setHeight(qMax(requestedSize.height(), minSize.height()));
    }
    setSize(requestedSize);

    const Corner fixedCorner = activeCorner == Corner::TopLeft ? Corner::BottomRight
            : activeCorner == Corner::TopRight ? Corner::BottomLeft
            : activeCorner == Corner::BottomLeft ? Corner::TopRight : Corner::TopLeft;
    setPos(fixedSceneCorner - cornerPosition(fixedCorner));
    if (screenGroup != nullptr) screenGroup->constrainToParent();
}

QSize ResizableImageItem::minimumSize() const {
    if (screenGroup == nullptr) return QSize(1, 1);
    const QSizeF size = screenGroup->mapRectToParent(screenGroup->boundingRect()).size();
    return QSize(qCeil(size.width()), qCeil(size.height()));
}

QPointF ResizableImageItem::cornerPosition(Corner corner) const {
    const QRectF rect = boundingRect();
    switch (corner) {
        case Corner::TopLeft: return rect.topLeft();
        case Corner::TopRight: return rect.topRight();
        case Corner::BottomLeft: return rect.bottomLeft();
        case Corner::BottomRight: return rect.bottomRight();
    }
    return {};
}

void ResizableImageItem::setSize(const QSize &size) {
    if (size == scaledImage.size()) return;
    scaledImage = sourceImage.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    setPixmap(QPixmap::fromImage(scaledImage));
    updateHandles();
}

void ResizableImageItem::updateHandles() {
    frame->setRect(boundingRect());
    for (QGraphicsItem *child : childItems()) {
        auto *handle = dynamic_cast<ResizeHandle *>(child);
        if (handle == nullptr) continue;
        handle->setPos(cornerPosition(handle->handleCorner()));
    }
}
