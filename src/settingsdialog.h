#ifndef WALLPAPER_SPLITTER_SETTINGSDIALOG_H
#define WALLPAPER_SPLITTER_SETTINGSDIALOG_H

#include <QDialog>

#include "appsettings.h"

class QComboBox;
class QLineEdit;

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(const UserPreferences &preferences, QWidget *parent = nullptr);
    UserPreferences preferences() const;

private slots:
    void accept() override;
    void resetDefaults();

private:
    QLineEdit *inputDirectory{};
    QLineEdit *exportDirectory{};
    QLineEdit *fileNameTemplate{};
    QComboBox *collisionPolicy{};
};

#endif
