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
#include <QSpinBox>
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
    resize(650, 430);
    setMinimumSize(560, 360);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();
    form->addRow(tr("Input folder:"), directoryRow(&inputDirectory, this));
    form->addRow(tr("Export folder:"), directoryRow(&exportDirectory, this));
    fileNameTemplate = new QLineEdit(this);
    fileNameTemplate->setToolTip(tr("Fields: {source}, {screen}, {number}, {revision}, {digest}"));
    form->addRow(tr("Filename template:"), fileNameTemplate);
    outputFormat = new QComboBox(this);
    outputFormat->addItem(tr("Automatic (preserve PNG, JPEG for opaque photos)"),
                          QStringLiteral("automatic"));
    outputFormat->addItem(tr("JPEG"), QStringLiteral("jpeg"));
    outputFormat->addItem(tr("PNG (lossless)"), QStringLiteral("png"));
    form->addRow(tr("Image format:"), outputFormat);
    jpegQuality = new QSpinBox(this);
    jpegQuality->setRange(1, 100);
    jpegQuality->setSuffix(tr("%"));
    form->addRow(tr("JPEG quality:"), jpegQuality);
    collisionPolicy = new QComboBox(this);
    collisionPolicy->addItem(tr("Ask each time"), QStringLiteral("ask"));
    collisionPolicy->addItem(tr("Create a new revision"), QStringLiteral("revision"));
    collisionPolicy->addItem(tr("Replace existing files"), QStringLiteral("replace"));
    form->addRow(tr("When files exist:"), collisionPolicy);
    layout->addLayout(form);
    auto *help = new QLabel(tr("Automatic keeps PNG sources lossless and uses compact, high-quality "
                               "JPEG for opaque photographic sources. In Automatic mode, crops with "
                               "transparency use PNG. Revisions use a set-wide -rN suffix unless {revision} appears "
                               "in the template."), this);
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
    const int formatIndex = outputFormat->findData(
            OutputService::outputFormatName(preferences.outputFormat));
    outputFormat->setCurrentIndex(qMax(0, formatIndex));
    jpegQuality->setValue(preferences.jpegQuality);
    connect(outputFormat, &QComboBox::currentIndexChanged, this, [this] {
        jpegQuality->setEnabled(outputFormat->currentData().toString() != QStringLiteral("png"));
    });
    jpegQuality->setEnabled(preferences.outputFormat != OutputFormat::Png);
}

UserPreferences SettingsDialog::preferences() const {
    UserPreferences result;
    result.closeAfterApply = closeAfterApply->isChecked();
    result.inputDirectory = inputDirectory->text().trimmed();
    result.exportDirectory = exportDirectory->text().trimmed();
    result.fileNameTemplate = fileNameTemplate->text();
    OutputService::parseCollisionPolicy(collisionPolicy->currentData().toString(),
                                        &result.collisionPolicy);
    OutputService::parseOutputFormat(outputFormat->currentData().toString(),
                                     &result.outputFormat);
    result.jpegQuality = jpegQuality->value();
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
    outputFormat->setCurrentIndex(outputFormat->findData(QStringLiteral("automatic")));
    jpegQuality->setValue(90);
}
