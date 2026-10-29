#include "slideshowdialog.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

SlideshowDialog::SlideshowDialog(QWidget *parent, const QString &managedRoot,
                                 const QString &configFile, SlideshowScheduler *customScheduler)
    : QDialog(parent), slideshow(configFile, managedRoot),
      scheduler(customScheduler ? customScheduler : &systemScheduler) {
    setWindowTitle(tr("Library Slideshow"));
    resize(620, 620);
    auto *layout = new QVBoxLayout(this);
    auto *description = new QLabel(tr("Choose saved sets to rotate across all monitors. "
        "Drag sets to change their order. Rotation continues while this app is closed and "
        "uses the active Plasma activity at each switch. Missing sets and sets that do not "
        "match your monitors are skipped."), this);
    description->setWordWrap(true);
    layout->addWidget(description);
    list = new QListWidget(this);
    list->setObjectName("slideshowSets");
    list->setDragDropMode(QAbstractItemView::InternalMove);
    list->setDefaultDropAction(Qt::MoveAction);
    list->setIconSize(QSize(90, 50));
    layout->addWidget(list, 1);
    auto *selection = new QHBoxLayout;
    auto *all = new QPushButton(tr("Select all"), this);
    auto *none = new QPushButton(tr("Clear selection"), this);
    auto *up = new QPushButton(tr("Move up"), this);
    auto *down = new QPushButton(tr("Move down"), this);
    selection->addWidget(all);
    selection->addWidget(none);
    selection->addStretch();
    selection->addWidget(up);
    selection->addWidget(down);
    layout->addLayout(selection);
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
    auto sets = SetLibrary(managedRoot).sets();
    QStringList order = settings.setIds;
    for (const auto &set : sets) if (!order.contains(set.id)) order.append(set.id);
    for (const auto &id : order) {
        const auto found = std::find_if(sets.cbegin(), sets.cend(), [&id](const auto &set) { return set.id == id; });
        const bool missing = found == sets.cend();
        auto *item = new QListWidgetItem(missing ? tr("Missing set: %1").arg(id) : found->name, list);
        item->setData(Qt::UserRole, id);
        item->setFlags((item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled)
                       & ~Qt::ItemIsDropEnabled);
        item->setCheckState(settings.setIds.contains(id) ? Qt::Checked : Qt::Unchecked);
        if (!missing) {
            item->setIcon(QPixmap::fromImage(SetLibrary::preview(*found, QSize(90, 50))));
            item->setToolTip(found->problem);
        }
    }
    interval->setValue(settings.intervalMinutes);
    enabled->setChecked(settings.enabled);
    shuffle->setChecked(settings.shuffle);
    if (!loaded.success) {
        status->setText(loaded.message);
        buttons->button(QDialogButtonBox::Save)->setEnabled(false);
        nextButton->setEnabled(false);
    } else updateStatus();

    const auto markDirty = [this] { dirty = true; nextButton->setEnabled(false); };
    connect(list, &QListWidget::itemChanged, this, markDirty);
    connect(list->model(), &QAbstractItemModel::rowsMoved, this, markDirty);
    connect(interval, &QSpinBox::valueChanged, this, markDirty);
    connect(enabled, &QCheckBox::toggled, this, markDirty);
    connect(shuffle, &QCheckBox::toggled, this, markDirty);
    connect(all, &QPushButton::clicked, this, [this] {
        for (int i = 0; i < list->count(); ++i) list->item(i)->setCheckState(Qt::Checked);
    });
    connect(none, &QPushButton::clicked, this, [this] {
        for (int i = 0; i < list->count(); ++i) list->item(i)->setCheckState(Qt::Unchecked);
    });
    const auto move = [this, markDirty](int offset) {
        const int row = list->currentRow();
        if (row < 0 || row + offset < 0 || row + offset >= list->count()) return;
        auto *item = list->takeItem(row);
        list->insertItem(row + offset, item);
        list->setCurrentItem(item);
        markDirty();
    };
    connect(up, &QPushButton::clicked, this, [move] { move(-1); });
    connect(down, &QPushButton::clicked, this, [move] { move(1); });
    connect(buttons->button(QDialogButtonBox::Save), &QPushButton::clicked, this, [this] { save(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(refresh, &QPushButton::clicked, this, &SlideshowDialog::updateStatus);
    connect(nextButton, &QPushButton::clicked, this, [this] {
        QList<QRect> layout;
        for (auto *screen : QGuiApplication::screens()) layout.append(screen->geometry());
        DBusPlasmaApplicator applicator;
        const auto result = slideshow.next(layout, applicator);
        if (!result.success) QMessageBox::warning(this, tr("Could not advance slideshow"), result.message);
        updateStatus();
    });
    nextButton->setToolTip(tr("Save playlist changes before advancing."));
}

bool SlideshowDialog::save() {
    SlideshowSettings settings;
    settings.enabled = enabled->isChecked();
    settings.intervalMinutes = interval->value();
    settings.shuffle = shuffle->isChecked();
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->checkState() == Qt::Checked)
            settings.setIds.append(list->item(i)->data(Qt::UserRole).toString());
    const auto result = slideshow.configure(settings, *scheduler);
    if (!result.success) {
        // Scheduler failures can happen after the playlist has been saved.
        // Reflect that persisted state and keep manual advancement available.
        SlideshowSettings saved;
        if (slideshow.load(&saved).success && saved.setIds == settings.setIds
            && saved.intervalMinutes == settings.intervalMinutes && saved.shuffle == settings.shuffle) {
            const QSignalBlocker blocker(enabled);
            enabled->setChecked(saved.enabled);
            dirty = false;
        }
        QMessageBox::warning(this, tr("Could not save slideshow"), result.message);
        updateStatus();
        return false;
    }
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
    nextButton->setEnabled(result.success && !dirty && !settings.setIds.isEmpty());
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
