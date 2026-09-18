#include "setlibrarydialog.h"
#include "appsettings.h"
#include <QCheckBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
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
#include <QSignalBlocker>
#include <QSplitter>
#include <QSet>
#include <QVBoxLayout>
#include <algorithm>

SetLibraryDialog::SetLibraryDialog(QWidget *parent, const QString &root)
    : QDialog(parent), library(root) {
    setWindowTitle(tr("Wallpaper Set Library"));
    resize(850, 580);
    auto *layout = new QVBoxLayout(this);
    status = new QLabel(this);
    status->setWordWrap(true);
    status->setTextFormat(Qt::PlainText);
    layout->addWidget(status);
    auto *selectionControls = new QHBoxLayout;
    selectAllCheckBox = new QCheckBox(tr("Select all"), this);
    selectAllCheckBox->setObjectName("selectAllSets");
    selectAllCheckBox->setTristate(true);
    selectionControls->addWidget(selectAllCheckBox);
    selectionControls->addStretch();
    layout->addLayout(selectionControls);
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
    applyButton->setObjectName("reapplySet");
    renameButton = buttons->addButton(tr("Rename"), QDialogButtonBox::ActionRole);
    renameButton->setObjectName("renameSet");
    renameButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-rename"),
                                          style()->standardIcon(QStyle::SP_FileDialogDetailedView)));
    deleteButton = buttons->addButton(tr("Delete selected"), QDialogButtonBox::ActionRole);
    deleteButton->setObjectName("deleteSelectedSets");
    deleteButton->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete"),
                                          style()->standardIcon(QStyle::SP_TrashIcon)));
    exportButton = buttons->addButton(tr("Export"), QDialogButtonBox::ActionRole);
    exportButton->setObjectName("exportSet");
    exportButton->setIcon(QIcon::fromTheme(QStringLiteral("document-save-as"),
                                          style()->standardIcon(QStyle::SP_DialogSaveButton)));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(list, &QListWidget::currentRowChanged, this, [this] { select(); });
    connect(list, &QListWidget::itemChanged, this, [this] { updateBulkControls(); });
    connect(selectAllCheckBox, &QCheckBox::clicked, this, [this](bool checked) {
        const auto state = checked ? Qt::Checked : Qt::Unchecked;
        const QSignalBlocker blocker(list);
        for (int i = 0; i < list->count(); ++i)
            list->item(i)->setCheckState(state);
        updateBulkControls();
    });
    connect(exportButton, &QPushButton::clicked, this, [this] {
        const QStringList ids = actionIds();
        if (ids.size() != 1) return;
        auto preferences = AppSettings::load();
        const QString directory = QFileDialog::getExistingDirectory(
            this, tr("Export wallpaper crops"), preferences.exportDirectory,
            QFileDialog::ShowDirsOnly);
        if (directory.isEmpty()) return;
        ExportOptions options{directory, preferences.fileNameTemplate, preferences.collisionPolicy};
        auto result = library.exportSet(ids.first(), options);
        if (!result.success && result.error == OperationError::Collision
            && options.collisionPolicy == CollisionPolicy::Ask) {
            QMessageBox question(QMessageBox::Question, tr("Export files already exist"),
                tr("Replace the existing crop files or create a new revision?"),
                QMessageBox::Cancel, this);
            auto *replace = question.addButton(tr("Replace"), QMessageBox::DestructiveRole);
            auto *revision = question.addButton(tr("New Revision"), QMessageBox::AcceptRole);
            question.exec();
            if (question.clickedButton() == replace)
                options.collisionPolicy = CollisionPolicy::Replace;
            else if (question.clickedButton() == revision)
                options.collisionPolicy = CollisionPolicy::Revision;
            else
                return;
            result = library.exportSet(ids.first(), options);
        }
        if (!result.success) {
            report(result);
            return;
        }
        preferences.exportDirectory = directory;
        AppSettings::save(preferences);
        status->setText(tr("Exported %n wallpaper crop(s) to %1.", nullptr, result.paths.size())
                            .arg(directory));
    });
    connect(applyButton, &QPushButton::clicked, this, [this] {
        const QStringList ids = actionIds();
        if (ids.size() != 1) return;
        const QString id = ids.first();
        QList<QRect> geometries;
        for (const auto *screen : QGuiApplication::screens()) geometries.append(screen->geometry());
        DBusPlasmaApplicator applicator;
        const auto result = library.reapply(id, geometries, applicator);
        refresh(id);
        report(result);
        if (result.success) status->setText(tr("Set applied to the current Plasma activity."));
    });
    connect(renameButton, &QPushButton::clicked, this, [this] {
        const QStringList ids = actionIds();
        if (ids.size() != 1) return;
        const auto found = std::find_if(entries.cbegin(), entries.cend(), [&ids](const auto &set) {
            return set.id == ids.first();
        });
        if (found == entries.cend()) return;
        const auto set = *found;
        bool accepted = false;
        const QString name = QInputDialog::getText(this, tr("Rename set"), tr("Name:"),
                                                   QLineEdit::Normal, set.name, &accepted);
        if (!accepted) return;
        report(library.rename(set.id, name));
        refresh(set.id);
    });
    connect(deleteButton, &QPushButton::clicked, this, [this] {
        const QStringList ids = actionIds();
        if (ids.isEmpty()) return;
        QStringList names;
        for (const auto &set : entries)
            if (ids.contains(set.id)) names.append(set.name);
        const QString prompt = ids.size() == 1
            ? tr("Delete “%1” and its generated images? Your source image and exports will be kept. "
                 "Sets still referenced by Plasma cannot be deleted.").arg(names.value(0))
            : tr("Delete %n selected wallpaper set(s) and their generated images? Your source images "
                 "and exports will be kept. Sets still referenced by Plasma cannot be deleted.",
                 nullptr, ids.size());
        QMessageBox question(QMessageBox::Warning, tr("Delete wallpaper set"),
            prompt, QMessageBox::Yes | QMessageBox::Cancel, this);
        question.setTextFormat(Qt::PlainText);
        question.setDefaultButton(QMessageBox::Cancel);
        question.button(QMessageBox::Yes)->setText(
            ids.size() == 1 ? tr("Delete") : tr("Delete selected"));
        if (question.exec() != QMessageBox::Yes) return;
        DBusPlasmaApplicator applicator;
        QStringList failures;
        int deleted = 0;
        for (int i = 0; i < ids.size(); ++i) {
            const auto result = library.remove(ids[i], applicator);
            if (result.success) ++deleted;
            else failures.append(tr("%1: %2").arg(names.value(i), result.message));
        }
        refresh();
        if (failures.isEmpty()) {
            status->setText(tr("Deleted %n wallpaper set(s).", nullptr, deleted));
        } else {
            status->setText(tr("Deleted %1 of %2 selected wallpaper sets.").arg(deleted).arg(ids.size()));
            QMessageBox warning(QMessageBox::Warning, tr("Some wallpaper sets were not deleted"),
                tr("%n selected set(s) could not be deleted.", nullptr, failures.size()),
                QMessageBox::Ok, this);
            warning.setDetailedText(failures.join('\n'));
            warning.exec();
        }
    });
    refresh();
}

void SetLibraryDialog::refresh(const QString &selectedId) {
    QString currentId = selectedId;
    if (currentId.isEmpty() && list->currentItem())
        currentId = list->currentItem()->data(Qt::UserRole).toString();
    const QStringList checkedList = checkedIds();
    const QSet<QString> checked(checkedList.cbegin(), checkedList.cend());
    const QSignalBlocker blocker(list);
    list->clear();
    entries = library.sets();
    int row = 0;
    for (int i = 0; i < entries.size(); ++i) {
        const auto &set = entries[i];
        auto *item = new QListWidgetItem(QIcon(QPixmap::fromImage(SetLibrary::preview(set, QSize(120, 70)))),
                                        set.name, list);
        item->setData(Qt::UserRole, set.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(checked.contains(set.id) ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(set.problem.isEmpty() ? set.name : set.problem);
        if (set.id == currentId) row = i;
    }
    status->setText(entries.isEmpty()
        ? tr("No wallpaper sets yet. Open an image and choose Apply to add your first set.")
        : tr("%n saved set(s). Reapply uses the current Plasma activity.", nullptr, entries.size()));
    if (!entries.isEmpty()) list->setCurrentRow(row);
    select();
    updateBulkControls();
}

void SetLibraryDialog::select() {
    const int row = list->currentRow();
    const bool selected = row >= 0 && row < entries.size();
    details->clear();
    previewLabel->clear();
    if (!selected) {
        updateBulkControls();
        return;
    }
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
    updateBulkControls();
}

QStringList SetLibraryDialog::checkedIds() const {
    QStringList ids;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->checkState() == Qt::Checked)
            ids.append(list->item(i)->data(Qt::UserRole).toString());
    return ids;
}

QStringList SetLibraryDialog::actionIds() const {
    QStringList ids = checkedIds();
    if (ids.isEmpty() && list->currentItem())
        ids.append(list->currentItem()->data(Qt::UserRole).toString());
    return ids;
}

void SetLibraryDialog::updateBulkControls() {
    const int checked = checkedIds().size();
    const QStringList ids = actionIds();
    const QSignalBlocker blocker(selectAllCheckBox);
    selectAllCheckBox->setEnabled(list->count() > 0);
    selectAllCheckBox->setCheckState(checked == 0 ? Qt::Unchecked
                                     : checked == list->count() ? Qt::Checked
                                     : Qt::PartiallyChecked);
    const bool single = ids.size() == 1;
    const auto found = single
        ? std::find_if(entries.cbegin(), entries.cend(), [&ids](const auto &set) {
              return set.id == ids.first();
          })
        : entries.cend();
    applyButton->setEnabled(single && found != entries.cend() && found->problem.isEmpty());
    exportButton->setEnabled(single && found != entries.cend() && found->problem.isEmpty());
    renameButton->setEnabled(single);
    deleteButton->setEnabled(!ids.isEmpty());
    deleteButton->setText(checked > 0
        ? tr("Delete selected (%1)").arg(checked)
        : tr("Delete"));
}

void SetLibraryDialog::report(const OperationResult &result) {
    if (!result.success) QMessageBox::warning(this, tr("Wallpaper set operation failed"), result.message);
}
