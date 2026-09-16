#ifndef WALLPAPER_SPLITTER_MONITORSDIALOG_H
#define WALLPAPER_SPLITTER_MONITORSDIALOG_H
#include <QDialog>
#include "monitorlayout.h"
class QCheckBox;
class QDoubleSpinBox;
class QGraphicsView;

class MonitorsDialog : public QDialog {
public:
    MonitorsDialog(const QList<MonitorInfo> &monitors, const MonitorPreferences &preferences,
                   QWidget *parent = nullptr);
    MonitorPreferences preferences() const;
private:
    struct Editors {
        QDoubleSpinBox *diagonal;
        QDoubleSpinBox *width;
        QDoubleSpinBox *height;
        QDoubleSpinBox *left;
        QDoubleSpinBox *top;
    };
    QList<MonitorInfo> monitors;
    MonitorPreferences saved;
    QList<Editors> rows;
    QCheckBox *enabled;
    QGraphicsView *preview{};
    void updatePreview();
    void arrange();
protected:
    void resizeEvent(QResizeEvent *event) override;
};
#endif
