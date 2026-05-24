#include "imagedataurl.h"

#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>

bool imageFileToDataUrl(const QString& absolutePath, QString* outDataUrl, QString* error)
{
    QFile f(absolutePath);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("无法读取图片文件");
        return false;
    }
    constexpr qint64 kMaxBytes = 5 * 1024 * 1024;
    if (f.size() > kMaxBytes) {
        *error = QStringLiteral("图片须小于约 5MB");
        return false;
    }
    const QByteArray raw = f.readAll();
    f.close();

    QString mime = QMimeDatabase().mimeTypeForFile(absolutePath).name();
    const QString suf = QFileInfo(absolutePath).suffix().toLower();
    if (suf == QLatin1String("png"))
        mime = QStringLiteral("image/png");
    else if (suf == QLatin1String("jpg") || suf == QLatin1String("jpeg"))
        mime = QStringLiteral("image/jpeg");
    else if (suf == QLatin1String("webp"))
        mime = QStringLiteral("image/webp");
    else if (suf == QLatin1String("gif"))
        mime = QStringLiteral("image/gif");
    else if (suf == QLatin1String("bmp"))
        mime = QStringLiteral("image/bmp");
    else if (!mime.startsWith(QLatin1String("image/")))
        mime = QStringLiteral("image/jpeg");

    *outDataUrl = QStringLiteral("data:%1;base64,%2")
                      .arg(mime, QString::fromLatin1(raw.toBase64()));
    return true;
}
