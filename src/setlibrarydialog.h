#ifndef WALLPAPER_SPLITTER_SETLIBRARYDIALOG_H
#define WALLPAPER_SPLITTER_SETLIBRARYDIALOG_H
#include <QDialog>
#include "setlibrary.h"
class QListWidget;
class QLabel;
class QPlainTextEdit;
class QPushButton;

class SetLibraryDialog : public QDialog {
public:
    explicit SetLibraryDialog(QWidget *parent = nullptr, const QString &root = {});
private:
    SetLibrary library;
    QList<WallpaperSet> entries;
    QListWidget *list;
    QLabel *previewLabel;
    QLabel *status;
    QPlainTextEdit *details;
    QPushButton *applyButton;
    QPushButton *renameButton;
    QPushButton *deleteButton;
    void refresh(const QString &selectedId = {});
    void select();
    void report(const OperationResult &result);
};
#endif
