#include "monitorsdialog.h"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QTimer>
#include <algorithm>
#include <cmath>

MonitorsDialog::MonitorsDialog(const QList<MonitorInfo> &monitors,
        const MonitorPreferences &preferences, QWidget *parent)
    : QDialog(parent), monitors(monitors), saved(preferences) {
    setWindowTitle(tr("Monitor Sizes and Positions"));
    resize(960, 620);
    auto *layout = new QVBoxLayout(this);
    enabled = new QCheckBox(tr("Use physical monitor sizes"), this);
    enabled->setObjectName("physicalSizing");
    enabled->setChecked(preferences.enabled);
    layout->addWidget(enabled);
    auto *help = new QLabel(tr("Enter the diagonal in inches or the visible panel width and height in millimeters. "
        "Left and top locate each panel in millimeters; include any bezel gaps. Increasing top moves a panel down. "
        "Initial positions are estimates from the desktop layout—adjust them to match your desk. "
        "Missing screen dimensions start with a 24-inch estimate."), this);
    help->setWordWrap(true);
    layout->addWidget(help);
    auto *table = new QTableWidget(monitors.size(), 6, this);
    table->setHorizontalHeaderLabels({tr("Monitor"), tr("Diagonal (in)"), tr("Width (mm)"),
                                      tr("Height (mm)"), tr("Left (mm)"), tr("Top (mm)")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->hide();
    table->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(table);
    const auto spin = [table](int row, int column, double minimum, double maximum, double value) {
        auto *editor = new QDoubleSpinBox(table);
        editor->setDecimals(1);
        editor->setRange(minimum, maximum);
        editor->setValue(value);
        editor->setKeyboardTracking(false);
        table->setCellWidget(row, column, editor);
        return editor;
    };
    const auto initial = MonitorLayout::measurements(monitors, preferences);
    for (int i = 0; i < monitors.size(); ++i) {
        const auto &monitor = monitors[i];
        const auto measurement = initial[i];
        auto *name = new QTableWidgetItem(monitor.displayName.isEmpty() ? monitor.name : monitor.displayName);
        name->setFlags(Qt::ItemIsEnabled);
        name->setToolTip(tr("Desktop: %1 × %2. Verify reported dimensions before enabling physical sizing.")
                            .arg(monitor.desktopGeometry.width()).arg(monitor.desktopGeometry.height()));
        table->setItem(i, 0, name);
        const auto size = measurement.millimeters;
        rows.append({spin(i, 1, 1, 250, std::hypot(size.width(), size.height()) / 25.4),
                     spin(i, 2, 10, 5000, size.width()), spin(i, 3, 10, 5000, size.height()),
                     spin(i, 4, -20000, 20000, measurement.position.x()),
                     spin(i, 5, -20000, 20000, measurement.position.y())});
        const auto &row = rows.last();
        row.diagonal->setObjectName(QStringLiteral("diagonal-%1").arg(i));
        row.width->setObjectName(QStringLiteral("width-%1").arg(i));
        row.height->setObjectName(QStringLiteral("height-%1").arg(i));
        connect(row.diagonal, &QDoubleSpinBox::valueChanged, this, [this, i](double inches) {
            const auto size = MonitorLayout::sizeFromDiagonal(inches, this->monitors[i].desktopGeometry.size());
            const QSignalBlocker blockWidth(rows[i].width), blockHeight(rows[i].height);
            rows[i].width->setValue(size.width());
            rows[i].height->setValue(size.height());
            updatePreview();
        });
        const auto dimensionsChanged = [this, i] {
            const QSignalBlocker block(rows[i].diagonal);
            rows[i].diagonal->setValue(std::hypot(rows[i].width->value(), rows[i].height->value()) / 25.4);
            updatePreview();
        };
        connect(row.width, &QDoubleSpinBox::valueChanged, this, dimensionsChanged);
        connect(row.height, &QDoubleSpinBox::valueChanged, this, dimensionsChanged);
        connect(row.left, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
        connect(row.top, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    }
    auto *arrangeButton = new QPushButton(tr("Arrange left to right, top aligned"), this);
    arrangeButton->setObjectName("arrangeMonitors");
    connect(arrangeButton, &QPushButton::clicked, this, &MonitorsDialog::arrange);
    layout->addWidget(arrangeButton);
    preview = new QGraphicsView(this);
    preview->setScene(new QGraphicsScene(preview));
    preview->setMinimumHeight(180);
    preview->setInteractive(false);
    layout->addWidget(preview, 1);
    auto *note = new QLabel(tr("These settings affect Wallpaper Splitter only. Your Plasma display settings stay the same."), this);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto *guide = buttons->addButton(tr("How to line up monitors"), QDialogButtonBox::HelpRole);
    guide->setIcon(QIcon::fromTheme(QStringLiteral("help-contextual"),
                                   style()->standardIcon(QStyle::SP_DialogHelpButton)));
    connect(guide, &QPushButton::clicked, this, [this] {
        QMessageBox help(this);
        help.setWindowTitle(tr("Lining Up Wallpapers"));
        help.setTextFormat(Qt::RichText);
        help.setText(tr(
            "<b>Same physical size, different resolutions</b>"
            "<p>For example, two 27-inch 16:9 monitors, one 1920 × 1080 and one 3840 × 2160: "
            "enter 27 inches for both. Their wallpaper areas will be the same physical size, "
            "so objects stay the same size as they cross between screens.</p>"
            "<ol><li>Enable <b>Use physical monitor sizes</b>.</li>"
            "<li>Enter each panel's diagonal, or measure its visible width and height (excluding the frame). "
            "Equal diagonals give equal dimensions only when the aspect ratios match.</li>"
            "<li>For side-by-side monitors, click <b>Arrange left to right, top aligned</b>.</li>"
            "<li>Adjust <b>Left</b> and <b>Top</b> to match your desk. "
            "These are positions in millimeters, measured from a common origin.</li>"
            "<li>Click <b>OK</b>, review the crop overlay, then apply the wallpaper.</li></ol>"
            "<b>Bezel gaps and uneven heights</b>"
            "<p>If the left panel is 600 mm wide and the gap between visible panels is 12 mm, "
            "set the left panel's Left to 0 and the right panel's Left to 612. "
            "Include both bezels in that gap; the corresponding strip of wallpaper is hidden.</p>"
            "<p>With the left panel's Top at 0, set the right panel's Top to −10 if it sits 10 mm higher, "
            "or 10 if it sits 10 mm lower. This also helps monitors with identical resolutions.</p>"));
        help.exec();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    QTimer::singleShot(0, this, [this] { updatePreview(); });
}

MonitorPreferences MonitorsDialog::preferences() const {
    auto result = saved; // Keep measurements for temporarily disconnected displays.
    result.enabled = enabled->isChecked();
    for (int i = 0; i < rows.size(); ++i) {
        const auto &row = rows[i];
        result.measurements.insert(monitors[i].id,
            {{row.width->value(), row.height->value()}, {row.left->value(), row.top->value()},
             monitors[i].desktopGeometry.height() > monitors[i].desktopGeometry.width()});
    }
    return result;
}

void MonitorsDialog::arrange() {
    QList<int> order;
    for (int i = 0; i < monitors.size(); ++i) order.append(i);
    std::stable_sort(order.begin(), order.end(), [this](int a, int b) {
        return monitors[a].desktopGeometry.x() < monitors[b].desktopGeometry.x();
    });
    double left = 0;
    for (int i : order) {
        rows[i].left->setValue(left);
        rows[i].top->setValue(0);
        left += rows[i].width->value();
    }
    updatePreview();
}

void MonitorsDialog::updatePreview() {
    auto *scene = preview->scene();
    scene->clear();
    for (int i = 0; i < rows.size(); ++i) {
        const auto &row = rows[i];
        const QRectF rect(row.left->value(), row.top->value(), row.width->value(), row.height->value());
        const QColor color = QColor::fromHsv((i * 75 + 200) % 360, 90, 200);
        scene->addRect(rect, QPen(palette().text().color()), QBrush(color));
        auto *label = scene->addText(QString::number(i + 1) + "  " + monitors[i].name);
        label->setDefaultTextColor(Qt::black);
        label->setScale(qMin(1.0, rect.width() / qMax(1.0, label->boundingRect().width())));
        label->setPos(rect.center() - label->boundingRect().center() * label->scale());
    }
    scene->setSceneRect(scene->itemsBoundingRect());
    preview->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
}

void MonitorsDialog::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    if (preview != nullptr) QTimer::singleShot(0, this, [this] { updatePreview(); });
}
