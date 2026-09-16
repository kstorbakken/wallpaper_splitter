#include "setlibrarydialog.h"
#include <QDateTime>
#include <QDialogButtonBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QScreen>
#include <QSplitter>
#include <QVBoxLayout>

SetLibraryDialog::SetLibraryDialog(QWidget *parent, const QString &root)
    : QDialog(parent), library(root) {
    setWindowTitle(tr("Wallpaper Set Library"));
    resize(850, 580);
    auto *layout = new QVBoxLayout(this);
    status = new QLabel(this);
    status->setWordWrap(true);
    status->setTextFormat(Qt::PlainText);
    layout->addWidget(status);
    auto *splitter = new QSplitter(this);
    list = new QListWidget(splitter);
    list->setObjectName("setList");
    list->setIconSize(QSize(120, 70));
    list->setMinimumWidth(230);
    auto *panel = new QWidget(splitter);
    auto *panelLayout = new QVBoxLayout(panel);
    previewLabel = new QLabel(panel);
    previewLabel->setAlignment(Qt::AlignCenter);
    previewLabel->setMinimumHeight(180);
    panelLayout->addWidget(previewLabel);
    details = new QPlainTextEdit(panel);
    details->setReadOnly(true);
    panelLayout->addWidget(details);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    applyButton = buttons->addButton(tr("Reapply"), QDialogButtonBox::ActionRole);
    renameButton = buttons->addButton(tr("Rename"), QDialogButtonBox::ActionRole);
    renameButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-rename"),
                                          style()->standardIcon(QStyle::SP_FileDialogDetailedView)));
    deleteButton = buttons->addButton(tr("Delete"), QDialogButtonBox::ActionRole);
    deleteButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete"),
                                          style()->standardIcon(QStyle::SP_TrashIcon)));
    auto *refreshButton = buttons->addButton(tr("Refresh"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(list, &QListWidget::currentRowChanged, this, [this] { select(); });
    connect(refreshButton, &QPushButton::clicked, this, [this] { refresh(); });
    connect(applyButton, &QPushButton::clicked, this, [this] {
        if (list->currentRow() < 0) return;
        const QString id = entries.at(list->currentRow()).id;
        QList<QRect> geometries;
        for (const auto *screen : QGuiApplication::screens()) geometries.append(screen->geometry());
        DBusPlasmaApplicator applicator;
        const auto result = library.reapply(id, geometries, applicator);
        refresh(id);
        report(result);
        if (result.success) status->setText(tr("Set applied to the current Plasma activity."));
    });
    connect(renameButton, &QPushButton::clicked, this, [this] {
        if (list->currentRow() < 0) return;
        const auto set = entries.at(list->currentRow());
        bool accepted = false;
        const QString name = QInputDialog::getText(this, tr("Rename set"), tr("Name:"),
                                                   QLineEdit::Normal, set.name, &accepted);
        if (!accepted) return;
        report(library.rename(set.id, name));
        refresh(set.id);
    });
    connect(deleteButton, &QPushButton::clicked, this, [this] {
        if (list->currentRow() < 0) return;
        const auto set = entries.at(list->currentRow());
        QMessageBox question(QMessageBox::Warning, tr("Delete wallpaper set"),
            tr("Delete “%1” and its generated images? Your source image and exports will be kept. "
               "Sets still referenced by Plasma cannot be deleted.").arg(set.name),
            QMessageBox::Yes | QMessageBox::Cancel, this);
        question.setTextFormat(Qt::PlainText);
        question.setDefaultButton(QMessageBox::Cancel);
        question.button(QMessageBox::Yes)->setText(tr("Delete"));
        if (question.exec() != QMessageBox::Yes) return;
        DBusPlasmaApplicator applicator;
        report(library.remove(set.id, applicator));
        refresh();
    });
    refresh();
}

void SetLibraryDialog::refresh(const QString &selectedId) {
    list->clear();
    entries = library.sets();
    int row = 0;
    for (int i = 0; i < entries.size(); ++i) {
        const auto &set = entries[i];
        auto *item = new QListWidgetItem(QIcon(QPixmap::fromImage(SetLibrary::preview(set, QSize(120, 70)))),
                                        set.name, list);
        item->setToolTip(set.problem.isEmpty() ? set.name : set.problem);
        if (set.id == selectedId) row = i;
    }
    status->setText(entries.isEmpty()
        ? tr("No wallpaper sets yet. Open an image and choose Apply to add your first set.")
        : tr("%n saved set(s). Reapply uses the current Plasma activity.", nullptr, entries.size()));
    if (!entries.isEmpty()) list->setCurrentRow(row);
    select();
}

void SetLibraryDialog::select() {
    const int row = list->currentRow();
    const bool selected = row >= 0 && row < entries.size();
    applyButton->setEnabled(selected && entries[row].problem.isEmpty());
    renameButton->setEnabled(selected);
    deleteButton->setEnabled(selected);
    details->clear();
    previewLabel->clear();
    if (!selected) return;
    const auto &set = entries[row];
    previewLabel->setPixmap(QPixmap::fromImage(SetLibrary::preview(set, QSize(480, 200))));
    const auto &m = set.manifest;
    const auto date = QDateTime::fromString(m.value("createdAt").toString(), Qt::ISODateWithMs);
    QString text = tr("%1\nSource: %2\nCreated: %3\nLast apply result: %4\n")
        .arg(set.name, m.value("sourcePath").toString(m.value("sourceName").toString()),
             date.isValid() ? date.toLocalTime().toString(Qt::TextDate) : tr("Unknown"),
             m.value("status").toString());
    if (!set.problem.isEmpty()) text += '\n' + set.problem + '\n';
    if (!m.value("error").toString().isEmpty()) text += '\n' + m.value("error").toString() + '\n';
    for (int i = 0; i < set.screens.size(); ++i) {
        const auto &s = set.screens[i];
        text += tr("\nScreen %1 — %2\nLayout: %3 × %4 at (%5, %6)\nCrop: %7 × %8 at (%9, %10)\nFile: %11\n")
            .arg(s.number).arg(s.name).arg(s.desktopGeometry.width()).arg(s.desktopGeometry.height())
            .arg(s.desktopGeometry.x()).arg(s.desktopGeometry.y()).arg(s.cropRect.width()).arg(s.cropRect.height())
            .arg(s.cropRect.x()).arg(s.cropRect.y()).arg(set.paths.value(i));
    }
    details->setPlainText(text);
}

void SetLibraryDialog::report(const OperationResult &result) {
    if (!result.success) QMessageBox::warning(this, tr("Wallpaper set operation failed"), result.message);
}
