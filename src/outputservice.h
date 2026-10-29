#ifndef WALLPAPER_SPLITTER_OUTPUTSERVICE_H
#define WALLPAPER_SPLITTER_OUTPUTSERVICE_H

#include <QJsonArray>
#include <QImage>
#include <QObject>
#include <QList>
#include <QRect>
#include <QSize>
#include <QStringList>

enum class OperationError {
    None,
    Arguments,
    ImageLayout,
    Collision,
    FileSystem,
    Plasma
};

enum class CollisionPolicy { Ask, Fail, Replace, Revision };
enum class OutputFormat { Automatic, Jpeg, Png };

struct OperationResult {
    bool success{false};
    OperationError error{OperationError::None};
    QString message;
    QStringList paths;
    QString manifestPath;

    static OperationResult ok(QStringList paths = {}, const QString &manifestPath = {});
    static OperationResult failure(OperationError error, const QString &message,
                                   QStringList paths = {}, const QString &manifestPath = {});
};

struct ScreenCrop {
    QString name;
    int number{1};
    QRect desktopGeometry;
    QRect cropRect;
};

struct CropArtifact {
    ScreenCrop screen;
    QImage image;
    QString digest;
    QString encodedPath;
};

struct ExportOptions {
    QString directory;
    QString fileNameTemplate{QStringLiteral("{source}-{number}")};
    CollisionPolicy collisionPolicy{CollisionPolicy::Ask};
    OutputFormat format{OutputFormat::Automatic};
    int jpegQuality{90};
};

class PlasmaApplicator {
public:
    virtual ~PlasmaApplicator() = default;
    virtual OperationResult wallpaperReferences() {
        return OperationResult::failure(OperationError::Plasma,
                                         QObject::tr("Plasma wallpaper references could not be checked."));
    }
    virtual OperationResult captureWallpaper(QJsonArray *) {
        return OperationResult::failure(OperationError::Plasma, QObject::tr("Wallpaper capture is unavailable."));
    }
    virtual OperationResult restoreWallpaper(const QJsonArray &) {
        return OperationResult::failure(OperationError::Plasma, QObject::tr("Wallpaper restoration is unavailable."));
    }
    virtual OperationResult apply(const QList<ScreenCrop> &screens,
                                  const QStringList &paths) = 0;
};

class DBusPlasmaApplicator final : public PlasmaApplicator {
public:
    OperationResult apply(const QList<ScreenCrop> &screens,
                          const QStringList &paths) override;
    OperationResult wallpaperReferences() override;
    OperationResult captureWallpaper(QJsonArray *snapshot) override;
    OperationResult restoreWallpaper(const QJsonArray &snapshot) override;
    static QString captureScript();
    static QString restoreScript(const QJsonArray &snapshot);
    static QString buildScript(const QList<ScreenCrop> &screens,
                               const QStringList &paths);
private:
    QString previewActivity;
};

class OutputService {
public:
    static QString defaultManagedRoot();
    static OperationResult saveManaged(const QImage &image, const QSize &originalSize,
                                       const QList<ScreenCrop> &screens, const QString &sourceName,
                                       const QString &sourcePath, const QString &managedRoot = {},
                                       OutputFormat format = OutputFormat::Automatic, int jpegQuality = 90);
    static OperationResult createCrops(const QImage &image,
                                       const QList<ScreenCrop> &screens,
                                       QList<CropArtifact> *artifacts);
    static OperationResult exportCrops(const QImage &image,
                                       const QList<ScreenCrop> &screens,
                                       const QString &sourceName,
                                       const ExportOptions &options);
    static OperationResult exportCrops(const QStringList &cropPaths,
                                       const QList<ScreenCrop> &screens,
                                       const QString &sourceName,
                                       const ExportOptions &options);
    static OperationResult applyManaged(const QImage &image,
                                        const QSize &originalSize,
                                        const QList<ScreenCrop> &screens,
                                        const QString &sourceName,
                                        const QString &sourcePath,
                                        PlasmaApplicator &applicator,
                                        const QString &managedRoot = {});

    static QString sanitizeFileComponent(const QString &value);
    static OperationResult validateFileNameTemplate(const QString &fileNameTemplate);
    static QString collisionPolicyName(CollisionPolicy policy);
    static bool parseCollisionPolicy(const QString &value, CollisionPolicy *policy);
    static QString outputFormatName(OutputFormat format);
    static bool parseOutputFormat(const QString &value, OutputFormat *format);
private:
    static OperationResult writeManaged(const QImage &image, const QSize &originalSize,
                                        const QList<ScreenCrop> &screens, const QString &sourceName,
                                        const QString &sourcePath, PlasmaApplicator *applicator,
                                        const QString &managedRoot, OutputFormat format, int jpegQuality);

};

#endif
