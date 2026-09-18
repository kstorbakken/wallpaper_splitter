//
// Created by l0drex on 30.09.21.
//

#include <QMouseEvent>
#include <QMimeData>
#include <QGuiApplication>
#include <QDebug>
#include <QImageReader>
#include <QMimeDatabase>
#include <QScrollBar>
#include <QToolButton>
#include <QPainter>
#include "graphicsview.h"

namespace {
class FitImageButton final : public QToolButton {
public:
    explicit FitImageButton(QWidget *parent, bool stretch) : QToolButton(parent), stretch(stretch) {
        setFixedSize(36, 36);
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setCheckable(true);
    }

private:
    bool stretch;

protected:
    void paintEvent(QPaintEvent *) override {
        const bool active = underMouse() || hasFocus() || isChecked();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(20, 20, 20, isDown() ? 210 : active ? 170 : 55));
        painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 7, 7);
        painter.setPen(QPen(QColor(255, 255, 255, active ? 245 : 115), 1.8,
                            Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        // Keep diagonal expand arrows for panorama; axial arrows represent stretching.
        for (int i = 0; i < 4; ++i) {
            painter.save();
            painter.translate(18, 18);
            painter.rotate(i * 90);
            if (stretch) {
                painter.drawLine(QPointF(2, 0), QPointF(11, 0));
                painter.drawPolyline(QPolygonF{QPointF(7, -4), QPointF(11, 0), QPointF(7, 4)});
            } else {
                painter.drawLine(QPointF(3, -3), QPointF(9, -9));
                painter.drawPolyline(QPolygonF{QPointF(3, -9), QPointF(9, -9), QPointF(9, -3)});
            }
            painter.restore();
        }
        if (hasFocus()) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(Qt::white, 1, Qt::DotLine));
            painter.drawRoundedRect(QRectF(rect()).adjusted(2, 2, -2, -2), 6, 6);
        }
    }
};
}

GraphicsView::GraphicsView(WallpaperSplitter *parent) : QGraphicsView(parent) {
    this->parent = parent;
    setAcceptDrops(true);
    // WallpaperSplitter automatically fits the complete preview whenever the
    // window changes size. Automatic scrollbars can make fitInView recurse:
    // showing one scrollbar shrinks the viewport enough to require the other,
    // leaving the image clipped until the next resize event.
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Resize handles ignore the view transform so they stay easy to grab. Some
    // compositors do not invalidate their old viewport positions reliably;
    // repainting this small preview avoids visible handle trails while dragging.
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    stretchImageButton = new FitImageButton(viewport(), true);
    stretchImageButton->setObjectName(QStringLiteral("stretchImageButton"));
    stretchImageButton->setAccessibleName(tr("Stretch across screens"));
    stretchImageButton->setToolTip(tr("Stretch across screens — click again to restore the initially loaded image and layout"));
    fillPanoramaButton = new FitImageButton(viewport(), false);
    fillPanoramaButton->setObjectName(QStringLiteral("fillPanoramaButton"));
    fillPanoramaButton->setAccessibleName(tr("Fill panorama"));
    fillPanoramaButton->setToolTip(tr("Fill panorama — preserve proportions; click again to restore the initially loaded image and layout"));
    for (auto *button : {stretchImageButton, fillPanoramaButton}) {
        button->setEnabled(false);
        button->hide();
    }
    stretchImageButton->move(viewport()->width() - 88, 12);
    fillPanoramaButton->move(viewport()->width() - 48, 12);
}

bool GraphicsView::viewportEvent(QEvent *event) {
    const bool handled = QGraphicsView::viewportEvent(event);
    if (event->type() == QEvent::Resize && stretchImageButton && fillPanoramaButton) {
        stretchImageButton->move(qMax(0, viewport()->width() - 88), 12);
        fillPanoramaButton->move(qMax(0, viewport()->width() - 48), 12);
    }
    return handled;
}

void GraphicsView::wheelEvent(QWheelEvent *event) {
    if (QGuiApplication::keyboardModifiers() == Qt::ControlModifier) {
        // Manual zooming is the one case where scrollbars are useful. The next
        // window resize restores the automatically fitted, scrollbar-free view.
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        // 1 if zooming in, -1 if zooming out
        const auto scaleUp = 2*(event->angleDelta().y() < 0) - 1;
        const auto amount = 1 - ZOOM_AMOUNT * scaleUp;
        setTransformationAnchor(AnchorUnderMouse);
        scale(amount, amount);
        event->accept();
    } else
        QGraphicsView::wheelEvent(event);
}

void GraphicsView::mousePressEvent(QMouseEvent *event) {
    if (panning) {
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton && event->buttons() == Qt::MiddleButton) {
        panning = true;
        setCursor(Qt::ClosedHandCursor);
        lastCursorPosition = event->pos();
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void GraphicsView::mouseMoveEvent(QMouseEvent *event) {
    if (panning && event->buttons().testFlag(Qt::MiddleButton)) {
        // Scrollbar values use viewport pixels, so the scene follows the hand
        // at the same speed regardless of zoom or the scene's origin.
        const QPoint movement = event->pos() - lastCursorPosition;
        lastCursorPosition = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - movement.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - movement.y());
        event->accept();
    } else
        QGraphicsView::mouseMoveEvent(event);
}

void GraphicsView::mouseReleaseEvent(QMouseEvent *event) {
    if (panning) {
        if (event->button() == Qt::MiddleButton) {
            panning = false;
            unsetCursor();
        }
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

bool isLocalImageFile(const QUrl& url)
{
    if (!url.isLocalFile()) return false;

    QMimeDatabase mimeDatabase;
    QMimeType mimeType = mimeDatabase.mimeTypeForUrl(url);
    auto mimeTypeName = mimeType.name();

    // Check if the MIME type indicates an image
    return mimeTypeName.startsWith("image/");
}

bool checkDrop(QDragMoveEvent *event) {
    if (event->mimeData()->hasImage()) {
        return true;
    } else if (event->mimeData()->hasUrls()) {
        if (isLocalImageFile(event->mimeData()->urls().first())) {
            return true;
        }
    }

    return false;
}

void GraphicsView::dragEnterEvent(QDragEnterEvent *event) {
    if (checkDrop(event)) {
        event->acceptProposedAction();
    } else {
        QGraphicsView::dragEnterEvent(event);
    }
}

void GraphicsView::dragMoveEvent(QDragMoveEvent *event) {
    if (checkDrop(event)) {
        event->acceptProposedAction();
    } else {
        QGraphicsView::dragMoveEvent(event);
    }
}

void GraphicsView::dropEvent(QDropEvent *event) {
    if (event->mimeData()->hasImage()) {
        qDebug() << "New image dropped";
        auto image = qvariant_cast<QImage>(event->mimeData()->imageData());
        parent->addImage(image);
    } else if (event->mimeData()->hasUrls()) {
        auto url = event->mimeData()->urls().first();
        if (isLocalImageFile(url)) {
            qDebug() << "New url dropped";
            parent->addImage(url);
        }
    } else
        QGraphicsView::dropEvent(event);
}
