#ifndef WALLPAPER_SPLITTER_WALLPAPERSETLIST_H
#define WALLPAPER_SPLITTER_WALLPAPERSETLIST_H

#include <QTreeWidget>

struct WallpaperSet;

class WallpaperSetList : public QTreeWidget {
    Q_OBJECT
public:
    enum Column { Name, DateAdded };

    explicit WallpaperSetList(QWidget *parent = nullptr);
    QTreeWidgetItem *addSet(const WallpaperSet &set, const QIcon &preview, Qt::CheckState checked);
    void sortBy(int column, Qt::SortOrder order);
    void refreshSort();
    void showPlaybackOrder(const QStringList &ids);
    void moveCurrent(int offset);
    QStringList setIds(bool checkedOnly = false) const;
    static QString setId(const QTreeWidgetItem *item);

signals:
    void headerSorted(int column, Qt::SortOrder order);
    void orderEdited();

protected:
    void dropEvent(QDropEvent *event) override;

private:
    int sortedColumn{Name};
    Qt::SortOrder direction{Qt::AscendingOrder};
};

#endif
