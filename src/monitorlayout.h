#ifndef WALLPAPER_SPLITTER_MONITORLAYOUT_H
#define WALLPAPER_SPLITTER_MONITORLAYOUT_H

#include <QList>
#include <QMap>
#include <QRectF>
#include <QString>

struct MonitorInfo {
    QString id;
    QString name;
    QRect desktopGeometry;
    QSizeF reportedMillimeters;
    QString displayName;
};

struct MonitorMeasurement {
    QSizeF millimeters;
    QPointF position;
    bool portrait{false};
};

struct MonitorPreferences {
    bool enabled{false};
    QMap<QString, MonitorMeasurement> measurements;
};

class MonitorLayout {
public:
    static QList<MonitorInfo> connectedMonitors();
    static QString identity(const QString &manufacturer, const QString &model,
                            const QString &serial, const QString &connector);
    static QSizeF sizeFromDiagonal(double inches, const QSize &aspect);
    static bool validSize(const QSizeF &size);
    static MonitorMeasurement measurement(const MonitorInfo &monitor,
                                          const MonitorPreferences &preferences,
                                          double millimetersPerPixel);
    static QList<MonitorMeasurement> measurements(const QList<MonitorInfo> &monitors,
                                                   const MonitorPreferences &preferences);
    static QList<QRectF> rectangles(const QList<MonitorInfo> &monitors,
                                    const MonitorPreferences &preferences);
};
#endif
