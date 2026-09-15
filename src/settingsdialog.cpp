#include "settingsdialog.h"

#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStyle>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {
QWidget *directoryRow(QLineEdit **editor, QWidget *parent) {
    auto *container = new QWidget(parent);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    *editor = new QLineEdit(container);
    auto *browse = new QPushButton(QObject::tr("Browse"), container);
    browse->setIcon(QIcon::fromTheme(QStringLiteral("folder-open"),
                                    container->style()->standardIcon(QStyle::SP_DialogOpenButton)));
    layout->addWidget(*editor);
    layout->addWidget(browse);
    QObject::connect(browse, &QPushButton::clicked, container, [editor, container] {
        const QString selected = QFileDialog::getExistingDirectory(
                container, QObject::tr("Select folder"), (*editor)->text(),
                QFileDialog::ShowDirsOnly);
        if (!selected.isEmpty()) (*editor)->setText(selected);
    });
    return container;
}

QString picturesDirectory() {
    const QString location = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    return location.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
                              : location;
}
}

SettingsDialog::SettingsDialog(const UserPreferences &preferences, QWidget *parent)
        : QDialog(parent) {
    setWindowTitle(tr("Settings"));
    resize(650, 360);
    setMinimumSize(560, 300);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();
    form->addRow(tr("Input folder:"), directoryRow(&inputDirectory, this));
    form->addRow(tr("Export folder:"), directoryRow(&exportDirectory, this));
    fileNameTemplate = new QLineEdit(this);
    fileNameTemplate->setToolTip(tr("Fields: {source}, {screen}, {number}, {revision}, {digest}"));
    form->addRow(tr("Filename template:"), fileNameTemplate);
    collisionPolicy = new QComboBox(this);
    collisionPolicy->addItem(tr("Ask each time"), QStringLiteral("ask"));
    collisionPolicy->addItem(tr("Create a new revision"), QStringLiteral("revision"));
    collisionPolicy->addItem(tr("Replace existing files"), QStringLiteral("replace"));
    form->addRow(tr("When files exist:"), collisionPolicy);
    layout->addLayout(form);
    auto *help = new QLabel(tr("Exports are PNG files. Revisions use a set-wide -rN suffix unless "
                               "{revision} appears in the template."), this);
    help->setWordWrap(true);
    layout->addWidget(help);
    closeAfterApply = new QCheckBox(tr("Close the app after successfully applying a wallpaper set"), this);
    closeAfterApply->setObjectName(QStringLiteral("closeAfterApply"));
    closeAfterApply->setChecked(preferences.closeAfterApply);
    layout->addWidget(closeAfterApply);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel
                                        | QDialogButtonBox::RestoreDefaults, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked,
            this, &SettingsDialog::resetDefaults);
    layout->addWidget(buttons);

    inputDirectory->setText(preferences.inputDirectory);
    exportDirectory->setText(preferences.exportDirectory);
    fileNameTemplate->setText(preferences.fileNameTemplate);
    const int policyIndex = collisionPolicy->findData(
            OutputService::collisionPolicyName(preferences.collisionPolicy));
    collisionPolicy->setCurrentIndex(qMax(0, policyIndex));
}

UserPreferences SettingsDialog::preferences() const {
    UserPreferences result;
    result.closeAfterApply = closeAfterApply->isChecked();
    result.inputDirectory = inputDirectory->text().trimmed();
    result.exportDirectory = exportDirectory->text().trimmed();
    result.fileNameTemplate = fileNameTemplate->text();
    OutputService::parseCollisionPolicy(collisionPolicy->currentData().toString(),
                                        &result.collisionPolicy);
    return result;
}

void SettingsDialog::accept() {
    const UserPreferences values = preferences();
    if (values.inputDirectory.isEmpty() || values.exportDirectory.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid settings"),
                             tr("Input and export folders cannot be empty."));
        return;
    }
    const OperationResult validation = OutputService::validateFileNameTemplate(
            values.fileNameTemplate);
    if (!validation.success) {
        QMessageBox::warning(this, tr("Invalid filename template"), validation.message);
        return;
    }
    QDialog::accept();
}

void SettingsDialog::resetDefaults() {
    closeAfterApply->setChecked(true);
    inputDirectory->setText(picturesDirectory());
    exportDirectory->setText(picturesDirectory());
    fileNameTemplate->setText(QStringLiteral("{source}-{number}"));
    collisionPolicy->setCurrentIndex(collisionPolicy->findData(QStringLiteral("ask")));
}
