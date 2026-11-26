#ifndef WALLPAPER_SPLITTER_SLIDESHOWDIALOG_H
#define WALLPAPER_SPLITTER_SLIDESHOWDIALOG_H

#include "slideshow.h"
#include <QDialog>
class QCheckBox;
class QComboBox;
class QLabel;
class WallpaperSetList;
class QPushButton;
class QSpinBox;

class SlideshowDialog : public QDialog {
public:
    explicit SlideshowDialog(QWidget *parent = nullptr, const QString &managedRoot = {},
                             const QString &configFile = {}, SlideshowScheduler *scheduler = nullptr);
    void reject() override;
private:
    Slideshow slideshow;
    SystemdSlideshowScheduler systemScheduler;
    SlideshowScheduler *scheduler;
    WallpaperSetList *list;
    WallpaperSetList *lockScreenList;
    QComboBox *lockScreenMode;
    QCheckBox *enabled;
    QCheckBox *shuffle;
    QSpinBox *interval;
    QLabel *status;
    QPushButton *nextButton;
    QStringList desktopOrder;
    QStringList lockScreenOrder;
    bool dirty{false};
    bool save();
    void updateStatus();
};
#endif
