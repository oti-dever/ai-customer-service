#include "swordcursor.h"

#include <QApplication>
#include <QCursor>
#include <QFile>
#include <QImage>
#include <QLatin1String>
#include <QSettings>

namespace {

constexpr auto kOrg = "YangYangAI";
constexpr auto kApp = "CustomerServiceDemo";
constexpr auto kKeyUse = "ui/useSwordCursor";
constexpr auto kKeyPath = "ui/swordCursorImagePath";

constexpr int kCursorMaxSide = 32;

QImage loadSourceImage()
{
    QSettings cfg{QLatin1String(kOrg), QLatin1String(kApp)};
    const QString custom = cfg.value(QLatin1String(kKeyPath)).toString().trimmed();
    if (!custom.isEmpty() && QFile::exists(custom)) {
        QImage img(custom);
        if (!img.isNull())
            return img;
    }
    QImage img(QStringLiteral(":/sword_cursor.png"));
    return img;
}

QPoint hotspotTopLeftOpaque(const QImage& img)
{
    const int w = img.width();
    const int h = img.height();
    int minX = w;
    int minY = h;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (qAlpha(img.pixel(x, y)) > 16) {
                minX = qMin(minX, x);
                minY = qMin(minY, y);
                maxX = qMax(maxX, x);
                maxY = qMax(maxY, y);
            }
        }
    }
    if (maxX < 0)
        return QPoint(0, 0);
    return QPoint(minX, minY);
}

} // namespace

bool SwordCursor::isEnabledInSettings()
{
    QSettings cfg{QLatin1String(kOrg), QLatin1String(kApp)};
    return cfg.value(QLatin1String(kKeyUse), false).toBool();
}

void SwordCursor::setEnabledInSettings(bool on)
{
    QSettings cfg{QLatin1String(kOrg), QLatin1String(kApp)};
    cfg.setValue(QLatin1String(kKeyUse), on);
}

QString SwordCursor::customImagePath()
{
    QSettings cfg{QLatin1String(kOrg), QLatin1String(kApp)};
    return cfg.value(QLatin1String(kKeyPath)).toString();
}

void SwordCursor::setCustomImagePath(const QString& absolutePathOrEmpty)
{
    QSettings cfg{QLatin1String(kOrg), QLatin1String(kApp)};
    cfg.setValue(QLatin1String(kKeyPath), absolutePathOrEmpty.trimmed());
}

void SwordCursor::applyIfEnabled()
{
    while (QApplication::overrideCursor())
        QApplication::restoreOverrideCursor();

    if (!isEnabledInSettings())
        return;

    QImage src = loadSourceImage();
    if (src.isNull())
        return;

    const int mx = qMax(src.width(), src.height());
    if (mx > kCursorMaxSide) {
        src = src.scaled(kCursorMaxSide, kCursorMaxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    src = src.convertToFormat(QImage::Format_ARGB32);
    if (src.isNull())
        return;

    const QPoint hot = hotspotTopLeftOpaque(src);
    const QPixmap pm = QPixmap::fromImage(src);
    QApplication::setOverrideCursor(QCursor(pm, hot.x(), hot.y()));
}

void SwordCursor::restore()
{
    while (QApplication::overrideCursor())
        QApplication::restoreOverrideCursor();
}
