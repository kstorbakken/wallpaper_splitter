#include "setlibrarydialog.h"

#include "monitorlayout.h"
#include "appsettings.h"
#include "slideshowdialog.h"
#include "wallpapersetlist.h"
#include <QBitmap>
#include <QCheckBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegion>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QScreen>
#include <QSignalBlocker>
#include <QSplitter>
#include <QSet>
#include <QTextLayout>
#include <QVBoxLayout>
#include <algorithm>

namespace {
class LibraryNameDelegate final : public QStyledItemDelegate {
public:
    explicit LibraryNameDelegate(WallpaperSetList *list) : QStyledItemDelegate(list), list(list) {}

private:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        option->decorationPosition = QStyleOptionViewItem::Top;
        option->decorationAlignment = Qt::AlignLeading | Qt::AlignTop;
        option->displayAlignment = Qt::AlignLeading | Qt::AlignTop;
        option->textElideMode = Qt::ElideNone;

        // Qt also asks for a size hint with no cell rect, so use the column width for wrapping.
        auto cell = *option;
        cell.rect.setWidth(list->columnWidth(WallpaperSetList::Name));
        const auto *style = list->style();
        const auto textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &cell, list);
        const int margin = style->pixelMetric(QStyle::PM_FocusFrameHMargin, &cell, list) + 1;
        const int width = std::max(1, textRect.width() - 2 * margin);
        if (option->fontMetrics.horizontalAdvance(option->text) <= width) return;

        QTextLayout layout(option->text, option->font, list);
        QTextOption wrapping;
        wrapping.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(wrapping);
        layout.beginLayout();
        QStringList lines;
        for (auto line = layout.createLine(); line.isValid(); line = layout.createLine()) {
            line.setLineWidth(width);
            lines.append(option->text.mid(line.textStart(), line.textLength()));
        }
        layout.endLayout();
        option->text = lines.join(QChar::LineSeparator);
    }

    const WallpaperSetList *list;
};

QIcon libraryThumbnail(const WallpaperSet &set, const QSize &size) {
    const auto preview = QPixmap::fromImage(SetLibrary::preview(set, size));
    const QRect content = QRegion(preview.mask()).boundingRect();
    if (content.isEmpty()) return QIcon(preview);
    return QIcon(preview.copy(content.adjusted(0, -4, 0, 4).intersected(preview.rect())));
}
}

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
    auto *slideshowButton = new QPushButton(tr("Slideshow…"), this);
    slideshowButton->setObjectName("configureSlideshow");
    selectionControls->addWidget(slideshowButton);
    connect(slideshowButton, &QPushButton::clicked, this, [this, root] {
        SlideshowDialog dialog(this, root);
        dialog.exec();
        refresh();
    });
    layout->addLayout(selectionControls);
    auto *splitter = new QSplitter(this);
    list = new WallpaperSetList(splitter);
    list->setObjectName("setList");
    list->setIconSize(QSize(120, 70));
    list->setMinimumWidth(230);
    list->setUniformRowHeights(false);
    list->setItemDelegateForColumn(WallpaperSetList::Name, new LibraryNameDelegate(list));
    connect(list->header(), &QHeaderView::sectionResized, list, [this](int column) {
        if (column == WallpaperSetList::Name) list->doItemsLayout();
    }, Qt::QueuedConnection);
    list->sortBy(WallpaperSetList::DateAdded, Qt::DescendingOrder);
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
    lockScreenButton = buttons->addButton(tr("Set as Lock Screen"), QDialogButtonBox::ActionRole);
    lockScreenButton->setObjectName("applyLockScreen");
    lockScreenButton->setToolTip(tr("Apply this set across all connected lock screen displays."));
    lockScreenButton->setAutoDefault(false);
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
    connect(list, &WallpaperSetList::currentItemChanged, this, [this] { select(); });
    connect(list, &WallpaperSetList::itemChanged, this, [this] { updateBulkControls(); });
    connect(selectAllCheckBox, &QCheckBox::clicked, this, [this](bool checked) {
        const auto state = checked ? Qt::Checked : Qt::Unchecked;
        const QSignalBlocker blocker(list);
        for (int i = 0; i < list->topLevelItemCount(); ++i)
            list->topLevelItem(i)->setCheckState(WallpaperSetList::Name, state);
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
        ExportOptions options{directory, preferences.fileNameTemplate, preferences.collisionPolicy,
                              preferences.outputFormat, preferences.jpegQuality};
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
    connect(lockScreenButton, &QPushButton::clicked, this, &SetLibraryDialog::applyLockScreen);
    connect(renameButton, &QPushButton::clicked, this, [this] {
        const auto *selected = actionSet(actionIds());
        if (!selected) return;
        const auto set = *selected;
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
        QStringList deletedIds;
        for (int i = 0; i < ids.size(); ++i) {
            const auto result = library.remove(ids[i], applicator);
            if (result.success) deletedIds.append(ids[i]);
            else failures.append(tr("%1: %2").arg(names.value(i), result.message));
        }
        SystemdSlideshowScheduler scheduler;
        const auto updated = Slideshow().removeSets(deletedIds, scheduler);
        if (!updated.success)
            QMessageBox::warning(this, tr("Could not update slideshow"),
                tr("The deleted sets could not be removed from the slideshow playlists. %1").arg(updated.message));
        refresh();
        if (failures.isEmpty()) {
            status->setText(tr("Deleted %n wallpaper set(s).", nullptr, deletedIds.size()));
        } else {
            status->setText(tr("Deleted %1 of %2 selected wallpaper sets.").arg(deletedIds.size()).arg(ids.size()));
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
    const QString currentId = selectedId.isEmpty() ? WallpaperSetList::setId(list->currentItem()) : selectedId;
    const QStringList checkedList = checkedIds();
    const QSet<QString> checked(checkedList.cbegin(), checkedList.cend());
    const QSignalBlocker blocker(list);
    list->clear();
    entries = library.sets();
    QTreeWidgetItem *selected = nullptr;
    for (const auto &set : entries) {
        auto *item = list->addSet(set, libraryThumbnail(set, list->iconSize()),
                                  checked.contains(set.id) ? Qt::Checked : Qt::Unchecked);
        if (set.id == currentId) selected = item;
    }
    list->refreshSort();
    status->setText(entries.isEmpty()
        ? tr("No wallpaper sets yet. Open an image and choose Save to Library or Apply.")
        : tr("%n saved set(s). Reapply uses the current Plasma activity.", nullptr, entries.size()));
    if (!entries.isEmpty()) list->setCurrentItem(selected ? selected : list->topLevelItem(0));
    select();
    updateBulkControls();
}

void SetLibraryDialog::select() {
    const auto *selected = actionSet({WallpaperSetList::setId(list->currentItem())});
    details->clear();
    previewLabel->clear();
    if (!selected) {
        updateBulkControls();
        return;
    }
    const auto &set = *selected;
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
    return list->setIds(true);
}

QStringList SetLibraryDialog::actionIds() const {
    QStringList ids = checkedIds();
    if (ids.isEmpty() && list->currentItem())
        ids.append(WallpaperSetList::setId(list->currentItem()));
    return ids;
}

const WallpaperSet *SetLibraryDialog::actionSet(const QStringList &ids) const {
    if (ids.size() != 1) return nullptr;
    const auto found = std::find_if(entries.cbegin(), entries.cend(), [&ids](const auto &set) {
        return set.id == ids.first();
    });
    return found == entries.cend() ? nullptr : &*found;
}

void SetLibraryDialog::applyLockScreen() {
    const auto *selected = actionSet(actionIds());
    if (!selected || !selected->problem.isEmpty()) return;
    const auto result = library.applyLockScreen(selected->id, MonitorLayout::connectedMonitors());
    report(result);
    if (result.success) status->setText(tr("This set will span your displays the next time you lock your screen."));
}

void SetLibraryDialog::updateBulkControls() {
    const int checked = checkedIds().size();
    const QStringList ids = actionIds();
    const QSignalBlocker blocker(selectAllCheckBox);
    selectAllCheckBox->setEnabled(list->topLevelItemCount() > 0);
    selectAllCheckBox->setCheckState(checked == 0 ? Qt::Unchecked
                                     : checked == list->topLevelItemCount() ? Qt::Checked
                                     : Qt::PartiallyChecked);
    const auto *set = actionSet(ids);
    const bool usable = set && set->problem.isEmpty();
    applyButton->setEnabled(usable);
    lockScreenButton->setEnabled(usable);
    exportButton->setEnabled(usable);
    renameButton->setEnabled(set != nullptr);
    deleteButton->setEnabled(!ids.isEmpty());
    deleteButton->setText(checked > 0
        ? tr("Delete selected (%1)").arg(checked)
        : tr("Delete"));
}

void SetLibraryDialog::report(const OperationResult &result) {
    if (!result.success) QMessageBox::warning(this, tr("Wallpaper set operation failed"), result.message);
}
