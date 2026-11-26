#include "slideshowdialog.h"
#include "wallpapersetlist.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
QStringList checkedPlaybackOrder(const WallpaperSetList *list, const QStringList &previous) {
    const auto checked = list->setIds(true);
    QSet<QString> remaining(checked.cbegin(), checked.cend());
    QStringList ordered;
    for (const auto &id : previous)
        if (remaining.remove(id)) ordered.append(id);
    for (const auto &id : checked)
        if (remaining.contains(id)) ordered.append(id);
    return ordered;
}
}

SlideshowDialog::SlideshowDialog(QWidget *parent, const QString &managedRoot,
                                 const QString &configFile, SlideshowScheduler *customScheduler)
    : QDialog(parent), slideshow(configFile, managedRoot),
      scheduler(customScheduler ? customScheduler : &systemScheduler) {
    setWindowTitle(tr("Library Slideshow"));
    resize(620, 620);
    auto *layout = new QVBoxLayout(this);
    auto *description = new QLabel(tr("Choose saved sets to rotate across all monitors. "
        "Automatic rotation continues while this app is closed. Desktop changes apply to the "
        "active Plasma activity. Missing sets and sets that do not match your monitors are skipped."), this);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *destination = new QFormLayout;
    lockScreenMode = new QComboBox(this);
    lockScreenMode->setObjectName("slideshowLockScreenMode");
    lockScreenMode->addItem(tr("Off"), static_cast<int>(LockScreenRotation::Off));
    lockScreenMode->addItem(tr("Match desktop"), static_cast<int>(LockScreenRotation::MatchDesktop));
    lockScreenMode->addItem(tr("Separate playlist"), static_cast<int>(LockScreenRotation::SeparatePlaylist));
    destination->addRow(tr("Lock screen:"), lockScreenMode);
    layout->addLayout(destination);

    auto *playlists = new QTabWidget(this);
    playlists->setObjectName("slideshowPlaylists");
    layout->addWidget(playlists, 1);
    const auto markDirty = [this] {
        dirty = true;
        nextButton->setEnabled(false);
    };
    const auto addPlaylist = [this, playlists](const QString &title, const QString &name) {
        auto *page = new QWidget(playlists);
        auto *pageLayout = new QVBoxLayout(page);
        auto *items = new WallpaperSetList(page);
        items->setObjectName(name);
        items->setDragDropMode(QAbstractItemView::InternalMove);
        items->setDefaultDropAction(Qt::MoveAction);
        items->setIconSize(QSize(90, 50));
        pageLayout->addWidget(items);

        auto *controls = new QHBoxLayout;
        auto *all = new QPushButton(tr("Select all"), page);
        auto *none = new QPushButton(tr("Clear selection"), page);
        auto *up = new QPushButton(tr("Move up"), page);
        auto *down = new QPushButton(tr("Move down"), page);
        up->setObjectName("moveSetUp");
        down->setObjectName("moveSetDown");
        controls->addWidget(all);
        controls->addWidget(none);
        controls->addStretch();
        controls->addWidget(up);
        controls->addWidget(down);
        pageLayout->addLayout(controls);
        playlists->addTab(page, title);

        const auto select = [items](Qt::CheckState state) {
            for (int i = 0; i < items->topLevelItemCount(); ++i)
                items->topLevelItem(i)->setCheckState(WallpaperSetList::Name, state);
        };
        connect(all, &QPushButton::clicked, this, [select] { select(Qt::Checked); });
        connect(none, &QPushButton::clicked, this, [select] { select(Qt::Unchecked); });
        connect(up, &QPushButton::clicked, this, [items] { items->moveCurrent(-1); });
        connect(down, &QPushButton::clicked, this, [items] { items->moveCurrent(1); });
        return items;
    };
    list = addPlaylist(tr("Desktop"), "slideshowSets");
    lockScreenList = addPlaylist(tr("Lock Screen"), "slideshowLockScreenSets");
    auto *form = new QFormLayout;
    interval = new QSpinBox(this);
    interval->setObjectName("slideshowInterval");
    interval->setRange(1, 10080);
    interval->setSuffix(tr(" minutes"));
    form->addRow(tr("Change every:"), interval);
    shuffle = new QCheckBox(tr("Shuffle (avoid consecutive repeats)"), this);
    shuffle->setObjectName("slideshowShuffle");
    form->addRow(shuffle);
    enabled = new QCheckBox(tr("Rotate automatically while logged in"), this);
    enabled->setObjectName("slideshowEnabled");
    form->addRow(enabled);
    layout->addLayout(form);
    status = new QLabel(this);
    status->setObjectName("slideshowStatus");
    status->setWordWrap(true);
    status->setTextFormat(Qt::PlainText);
    layout->addWidget(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, this);
    nextButton = buttons->addButton(tr("Next now"), QDialogButtonBox::ActionRole);
    nextButton->setObjectName("slideshowNext");
    auto *refresh = buttons->addButton(tr("Refresh status"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    SlideshowSettings settings;
    const auto loaded = slideshow.load(&settings);
    desktopOrder = settings.setIds;
    lockScreenOrder = settings.lockScreenSetIds;
    const auto sets = SetLibrary(managedRoot).sets();
    const QSet<QString> desktopSelected(desktopOrder.cbegin(), desktopOrder.cend());
    const QSet<QString> lockSelected(lockScreenOrder.cbegin(), lockScreenOrder.cend());
    for (const auto &set : sets) {
        const QIcon preview(QPixmap::fromImage(SetLibrary::preview(set, QSize(90, 50))));
        list->addSet(set, preview, desktopSelected.contains(set.id) ? Qt::Checked : Qt::Unchecked);
        lockScreenList->addSet(set, preview, lockSelected.contains(set.id) ? Qt::Checked : Qt::Unchecked);
    }
    for (auto *items : {list, lockScreenList}) {
        items->refreshSort();
        connect(items, &WallpaperSetList::itemChanged, this, markDirty);
        auto *other = items == list ? lockScreenList : list;
        auto *order = items == list ? &desktopOrder : &lockScreenOrder;
        const auto captureOrder = [items, order, markDirty] {
            if (checkedPlaybackOrder(items, *order) != items->setIds(true)) markDirty();
            *order = items->setIds();
        };
        connect(items, &WallpaperSetList::orderEdited, this, captureOrder);
        connect(items, &WallpaperSetList::headerSorted, this, [other, captureOrder](int column, Qt::SortOrder direction) {
            captureOrder();
            other->sortBy(column, direction);
        });
        auto *header = items->header();
        header->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(header, &QHeaderView::customContextMenuRequested, this, [items, order, header](const QPoint &point) {
            QMenu menu;
            auto *showOrder = menu.addAction(tr("Show playback order"));
            if (menu.exec(header->viewport()->mapToGlobal(point)) == showOrder)
                items->showPlaybackOrder(checkedPlaybackOrder(items, *order));
        });
    }
    lockScreenMode->setCurrentIndex(lockScreenMode->findData(static_cast<int>(settings.lockScreenMode)));
    const auto updatePlaylistTabs = [this, playlists] {
        const bool separate = lockScreenMode->currentData().toInt() == static_cast<int>(LockScreenRotation::SeparatePlaylist);
        playlists->setTabVisible(1, separate);
    };
    updatePlaylistTabs();
    interval->setValue(settings.intervalMinutes);
    enabled->setChecked(settings.enabled);
    shuffle->setChecked(settings.shuffle);
    if (!loaded.success) {
        status->setText(loaded.message);
        buttons->button(QDialogButtonBox::Save)->setEnabled(false);
        nextButton->setEnabled(false);
    } else updateStatus();

    connect(interval, &QSpinBox::valueChanged, this, markDirty);
    connect(enabled, &QCheckBox::toggled, this, markDirty);
    connect(shuffle, &QCheckBox::toggled, this, markDirty);
    connect(lockScreenMode, &QComboBox::currentIndexChanged, this, [playlists, updatePlaylistTabs, markDirty] {
        updatePlaylistTabs();
        if (playlists->isTabVisible(1)) playlists->setCurrentIndex(1);
        markDirty();
    });
    connect(buttons->button(QDialogButtonBox::Save), &QPushButton::clicked, this, [this] { save(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(refresh, &QPushButton::clicked, this, &SlideshowDialog::updateStatus);
    connect(nextButton, &QPushButton::clicked, this, [this] {
        DBusPlasmaApplicator applicator;
        const auto result = slideshow.next(MonitorLayout::connectedMonitors(), applicator);
        if (!result.success) QMessageBox::warning(this, tr("Could not advance slideshow"), result.message);
        updateStatus();
    });
    nextButton->setToolTip(tr("Save playlist changes before advancing."));
}

bool SlideshowDialog::save() {
    SlideshowSettings settings;
    settings.intervalMinutes = interval->value();
    settings.shuffle = shuffle->isChecked();
    desktopOrder = checkedPlaybackOrder(list, desktopOrder);
    lockScreenOrder = checkedPlaybackOrder(lockScreenList, lockScreenOrder);
    settings.setIds = desktopOrder;
    settings.lockScreenSetIds = lockScreenOrder;
    settings.lockScreenMode = static_cast<LockScreenRotation>(lockScreenMode->currentData().toInt());
    settings.enabled = enabled->isChecked() && settings.hasPlaylist();
    const auto result = slideshow.configure(settings, *scheduler, MonitorLayout::connectedMonitors());
    if (!result.success) {
        // Scheduler failures can happen after the playlist has been saved.
        // Reflect that persisted state and keep manual advancement available.
        SlideshowSettings saved;
        if (slideshow.load(&saved).success && saved.setIds == settings.setIds
            && saved.lockScreenMode == settings.lockScreenMode && saved.lockScreenSetIds == settings.lockScreenSetIds
            && saved.intervalMinutes == settings.intervalMinutes && saved.shuffle == settings.shuffle) {
            const QSignalBlocker blocker(enabled);
            enabled->setChecked(saved.enabled);
            dirty = false;
        }
        QMessageBox::warning(this, tr("Could not save slideshow"), result.message);
        updateStatus();
        return false;
    }
    const QSignalBlocker blocker(enabled);
    enabled->setChecked(settings.enabled);
    dirty = false;
    updateStatus();
    return true;
}

void SlideshowDialog::reject() {
    if (dirty) {
        const auto choice = QMessageBox::question(this, tr("Unsaved slideshow changes"),
            tr("Save your playlist and rotation settings before closing?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (choice == QMessageBox::Cancel || (choice == QMessageBox::Save && !save())) return;
    }
    QDialog::reject();
}

void SlideshowDialog::updateStatus() {
    SlideshowSettings settings;
    const auto result = slideshow.load(&settings);
    const bool selected = !list->setIds(true).isEmpty()
        || (settings.lockScreenMode == LockScreenRotation::SeparatePlaylist && !lockScreenList->setIds(true).isEmpty());
    nextButton->setEnabled(result.success && !dirty && selected && settings.hasPlaylist());
    if (!result.success) { status->setText(result.message); return; }
    QString text;
    if (settings.enabled) {
        const auto active = scheduler->status();
        text = active.success
            ? tr("Automatic rotation is on. You can close the app. Changes start after the selected interval.")
            : tr("Automatic rotation needs attention. Save again to repair its configuration.\n%1")
                  .arg(active.message);
    } else text = tr("Automatic rotation is off.");
    if (dirty) text += tr("\nSave changes before using Next now.");
    if (!settings.lastAttempt.isEmpty()) {
        const auto date = QDateTime::fromString(settings.lastAttempt, Qt::ISODateWithMs);
        text += tr("\nLast switch attempt: %1").arg(date.toLocalTime().toString(Qt::TextDate));
    }
    if (!settings.lastError.isEmpty()) text += '\n' + settings.lastError;
    status->setText(text);
}
