#include "wallpapersetlist.h"

#include "setlibrary.h"
#include <QCollator>
#include <QDateTime>
#include <QDropEvent>
#include <QHash>
#include <QHeaderView>
#include <QLocale>
#include <algorithm>

namespace {
class SetItem final : public QTreeWidgetItem {
public:
    QTreeWidgetItem *clone() const override { return new SetItem(*this); }

    bool operator<(const QTreeWidgetItem &other) const override {
        if (treeWidget()->sortColumn() == WallpaperSetList::DateAdded) {
            const auto date = data(WallpaperSetList::DateAdded, Qt::UserRole).toDateTime();
            const auto otherDate = other.data(WallpaperSetList::DateAdded, Qt::UserRole).toDateTime();
            if (date != otherDate) return date < otherDate;
        }
        static const QCollator collator = [] {
            QCollator names;
            names.setCaseSensitivity(Qt::CaseInsensitive);
            return names;
        }();
        const int order = collator.compare(text(WallpaperSetList::Name), other.text(WallpaperSetList::Name));
        return order == 0 ? WallpaperSetList::setId(this) < WallpaperSetList::setId(&other) : order < 0;
    }
};
}

WallpaperSetList::WallpaperSetList(QWidget *parent) : QTreeWidget(parent) {
    setHeaderLabels({tr("Name"), tr("Date Added")});
    setRootIsDecorated(false);
    setItemsExpandable(false);
    setUniformRowHeights(true);
    header()->setSectionsMovable(false);
    header()->setSectionsClickable(true);
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(Name, QHeaderView::Stretch);
    header()->setSectionResizeMode(DateAdded, QHeaderView::ResizeToContents);
    connect(header(), &QHeaderView::sectionClicked, this, [this](int column) {
        const auto order = header()->isSortIndicatorShown() && column == sortedColumn
            ? (direction == Qt::AscendingOrder ? Qt::DescendingOrder : Qt::AscendingOrder)
            : (column == DateAdded ? Qt::DescendingOrder : Qt::AscendingOrder);
        sortBy(column, order);
        emit headerSorted(column, order);
    });
    refreshSort();
}

QTreeWidgetItem *WallpaperSetList::addSet(const WallpaperSet &set, const QIcon &preview, Qt::CheckState checked) {
    auto *item = new SetItem;
    item->setText(Name, set.name);
    item->setIcon(Name, preview);
    item->setData(Name, Qt::UserRole, set.id);
    item->setFlags((item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled) & ~Qt::ItemIsDropEnabled);
    item->setCheckState(Name, checked);
    const auto date = QDateTime::fromString(set.manifest.value("createdAt").toString(), Qt::ISODateWithMs);
    item->setData(DateAdded, Qt::UserRole, date);
    const auto local = date.toLocalTime();
    item->setText(DateAdded, date.isValid() ? QLocale().toString(local.date(), QLocale::ShortFormat) : tr("Unknown"));
    if (date.isValid()) item->setToolTip(DateAdded, QLocale().toString(local, QLocale::ShortFormat));
    item->setToolTip(Name, set.problem.isEmpty() ? set.name : set.problem);
    addTopLevelItem(item);
    return item;
}

void WallpaperSetList::sortBy(int column, Qt::SortOrder order) {
    sortedColumn = column;
    direction = order;
    refreshSort();
}

void WallpaperSetList::refreshSort() {
    sortItems(sortedColumn, direction);
    header()->setSortIndicatorShown(true);
}

void WallpaperSetList::showPlaybackOrder(const QStringList &ids) {
    auto *current = currentItem();
    QHash<QString, qsizetype> positions;
    for (qsizetype i = 0; i < ids.size(); ++i) positions.insert(ids.at(i), i);
    auto items = invisibleRootItem()->takeChildren();
    std::stable_sort(items.begin(), items.end(), [&](const auto *left, const auto *right) {
        return positions.value(setId(left), ids.size()) < positions.value(setId(right), ids.size());
    });
    addTopLevelItems(items);
    if (current) setCurrentItem(current);
    header()->setSortIndicatorShown(false);
}

void WallpaperSetList::moveCurrent(int offset) {
    const int row = indexOfTopLevelItem(currentItem());
    if (row < 0 || row + offset < 0 || row + offset >= topLevelItemCount()) return;
    auto *item = takeTopLevelItem(row);
    insertTopLevelItem(row + offset, item);
    setCurrentItem(item);
    header()->setSortIndicatorShown(false);
    emit orderEdited();
}

QStringList WallpaperSetList::setIds(bool checkedOnly) const {
    QStringList ids;
    for (int i = 0; i < topLevelItemCount(); ++i) {
        const auto *item = topLevelItem(i);
        if (!checkedOnly || item->checkState(Name) == Qt::Checked) ids.append(setId(item));
    }
    return ids;
}

QString WallpaperSetList::setId(const QTreeWidgetItem *item) {
    return item ? item->data(Name, Qt::UserRole).toString() : QString();
}

void WallpaperSetList::dropEvent(QDropEvent *event) {
    const auto previous = setIds();
    QTreeWidget::dropEvent(event);
    if (previous == setIds()) return;
    header()->setSortIndicatorShown(false);
    emit orderEdited();
}
