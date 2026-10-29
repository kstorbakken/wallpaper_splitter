#ifndef WALLPAPER_SPLITTER_SLIDESHOWDIALOG_H
#define WALLPAPER_SPLITTER_SLIDESHOWDIALOG_H

#include "slideshow.h"
#include <QDialog>
class QCheckBox;
class QLabel;
class QListWidget;
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
    QListWidget *list;
    QCheckBox *enabled;
    QCheckBox *shuffle;
    QSpinBox *interval;
    QLabel *status;
    QPushButton *nextButton;
    bool dirty{false};
    bool save();
    void updateStatus();
};
#endif
