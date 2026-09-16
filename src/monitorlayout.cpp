#include "monitorlayout.h"
#include <QCryptographicHash>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScreen>
#include <cmath>
#include <limits>

QString MonitorLayout::identity(const QString &manufacturer, const QString &model,
                                const QString &serial, const QString &connector) {
    // A serial follows the display across connectors. Without one, the connector
    // disambiguates otherwise identical models.
    const QJsonArray identity{manufacturer, model, serial.isEmpty() ? connector : serial};
    return QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(identity).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}

QList<MonitorInfo> MonitorLayout::connectedMonitors() {
    QList<MonitorInfo> result;
    for (const auto *screen : QGuiApplication::screens()) {
        result.append({identity(screen->manufacturer(), screen->model(), screen->serialNumber(), screen->name()),
                       screen->name(), screen->geometry(), screen->physicalSize(),
                       screen->model().isEmpty() ? screen->name()
                           : screen->name() + " — " + screen->model()});
    }
    // Some displays report the same nonempty serial; avoid sharing their settings.
    for (int i = 0; i < result.size(); ++i) {
        const auto *screen = QGuiApplication::screens().at(i);
        int duplicates = 0;
        for (const auto *other : QGuiApplication::screens())
            if (identity(other->manufacturer(), other->model(), other->serialNumber(), other->name())
                == identity(screen->manufacturer(), screen->model(), screen->serialNumber(), screen->name()))
                ++duplicates;
        if (duplicates > 1)
            result[i].id = identity(screen->manufacturer(), screen->model(), {}, screen->name());
    }
    return result;
}

QSizeF MonitorLayout::sizeFromDiagonal(double inches, const QSize &aspect) {
    if (aspect.isEmpty() || !std::isfinite(inches) || inches <= 0) return {};
    const double factor = inches * 25.4 / std::hypot(aspect.width(), aspect.height());
    return QSizeF(aspect) * factor;
}

bool MonitorLayout::validSize(const QSizeF &size) {
    return std::isfinite(size.width()) && std::isfinite(size.height())
        && size.width() >= 10 && size.height() >= 10
        && size.width() <= 5000 && size.height() <= 5000;
}

MonitorMeasurement MonitorLayout::measurement(const MonitorInfo &monitor,
        const MonitorPreferences &preferences, double millimetersPerPixel) {
    const bool portrait = monitor.desktopGeometry.height() > monitor.desktopGeometry.width();
    if (preferences.measurements.contains(monitor.id)) {
        auto saved = preferences.measurements.value(monitor.id);
        if (validSize(saved.millimeters) && std::isfinite(saved.position.x())
            && std::isfinite(saved.position.y()) && qAbs(saved.position.x()) <= 20000
            && qAbs(saved.position.y()) <= 20000) {
            if (saved.portrait != portrait) saved.millimeters.transpose();
            saved.portrait = portrait;
            return saved;
        }
    }
    QSizeF size = monitor.reportedMillimeters;
    if (!validSize(size)) size = sizeFromDiagonal(24, monitor.desktopGeometry.size());
    if ((size.height() > size.width()) != portrait) size.transpose();
    return {size, QPointF(monitor.desktopGeometry.topLeft()) * millimetersPerPixel, portrait};
}

QList<MonitorMeasurement> MonitorLayout::measurements(const QList<MonitorInfo> &monitors,
                                                      const MonitorPreferences &preferences) {
    QList<MonitorMeasurement> result;
    QList<bool> placed;
    for (const auto &monitor : monitors) {
        const auto dimensions = measurement(monitor, preferences, 0.25);
        result.append(dimensions);
        const auto saved = preferences.measurements.value(monitor.id);
        placed.append(preferences.measurements.contains(monitor.id)
                      && validSize(saved.millimeters) && saved.position == dimensions.position);
    }
    if (result.isEmpty()) return result;
    if (!placed.contains(true)) {
        placed[0] = true;
        result[0].position = {};
    }
    // Extend the nearest known panel's position, keeping shared edges aligned.
    // Desktop coordinates supply adjacency; physical sizes supply distances.
    while (placed.contains(false)) {
        int next = -1, anchor = -1;
        double closest = std::numeric_limits<double>::max();
        for (int i = 0; i < monitors.size(); ++i) {
            if (placed[i]) continue;
            for (int j = 0; j < monitors.size(); ++j) {
                if (!placed[j]) continue;
                const QRectF a = monitors[i].desktopGeometry, b = monitors[j].desktopGeometry;
                const double dx = qMax(0.0, qMax(a.left() - b.right(), b.left() - a.right()));
                const double dy = qMax(0.0, qMax(a.top() - b.bottom(), b.top() - a.bottom()));
                const double distance = std::hypot(dx, dy);
                if (distance < closest) { closest = distance; next = i; anchor = j; }
            }
        }
        const auto axis = [](double start, double length, double anchorStart, double anchorLength,
                             double physicalLength, double anchorPosition, double anchorPhysicalLength) {
            const double density = anchorPhysicalLength / qMax(1.0, anchorLength);
            if (start >= anchorStart + anchorLength)
                return anchorPosition + anchorPhysicalLength + (start - anchorStart - anchorLength) * density;
            if (start + length <= anchorStart)
                return anchorPosition - physicalLength - (anchorStart - start - length) * density;
            if (start == anchorStart) return anchorPosition;
            if (start + length == anchorStart + anchorLength)
                return anchorPosition + anchorPhysicalLength - physicalLength;
            return anchorPosition + (start + length / 2 - anchorStart) * density - physicalLength / 2;
        };
        const QRectF rect = monitors[next].desktopGeometry, base = monitors[anchor].desktopGeometry;
        const auto &known = result[anchor];
        result[next].position = {
            axis(rect.x(), rect.width(), base.x(), base.width(), result[next].millimeters.width(),
                 known.position.x(), known.millimeters.width()),
            axis(rect.y(), rect.height(), base.y(), base.height(), result[next].millimeters.height(),
                 known.position.y(), known.millimeters.height())};
        placed[next] = true;
    }
    return result;
}

QList<QRectF> MonitorLayout::rectangles(const QList<MonitorInfo> &monitors,
                                      const MonitorPreferences &preferences) {
    QList<QRectF> result;
    const auto physical = measurements(monitors, preferences);
    for (int i = 0; i < monitors.size(); ++i) {
        if (!preferences.enabled) {
            result.append(monitors[i].desktopGeometry);
        } else {
            // One shared scene scale keeps equal physical lengths equal even when
            // monitor resolutions or desktop scale factors differ.
            const auto dimensions = physical[i];
            result.append(QRectF(dimensions.position * 4, dimensions.millimeters * 4));
        }
    }
    return result;
}
