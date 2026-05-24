#include "svgresourcepixmap.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <QtGlobal>

QPixmap svgResourcePixmapFittedInSquare(const QString& qrcPath, int logicalSide)
{
    if (logicalSide < 1)
        return {};
    QSvgRenderer renderer(qrcPath);
    if (!renderer.isValid())
        return {};
    // Qt6：两参数 render 把 viewBox 映到 bounds；用 KeepAspectRatio 在方形容器内等比、居中、留透明边。
    // 单参数 render+手写 QTransform 与内部坐标系叠加易缩成 1 像素或空白（聚合窗/AI 助手头像不显示）。
    const qreal dpr = (qApp && qApp->devicePixelRatio() > 0) ? qApp->devicePixelRatio() : 1.0;
    const int px = qMax(1, qRound(logicalSide * dpr));
    const qreal side = static_cast<qreal>(px);

    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    renderer.setAspectRatioMode(Qt::KeepAspectRatio);
    renderer.render(&p, QRectF(0, 0, side, side));
    p.end();
    pm.setDevicePixelRatio(dpr);
    return pm;
}

QPixmap rasterResourcePixmapFittedInSquare(const QString& qrcPath, int logicalSide)
{
    if (logicalSide < 1)
        return {};
    QImage img;
    if (!img.load(qrcPath))
        return {};
    const qreal dpr = (qApp && qApp->devicePixelRatio() > 0) ? qApp->devicePixelRatio() : 1.0;
    const int px = qMax(1, qRound(logicalSide * dpr));
    const QImage scaled
        = img.scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (scaled.isNull())
        return {};
    QPixmap out(px, px);
    out.fill(Qt::transparent);
    QPainter p(&out);
    const int x = (px - scaled.width()) / 2;
    const int y = (px - scaled.height()) / 2;
    p.drawImage(x, y, scaled);
    p.end();
    out.setDevicePixelRatio(dpr);
    return out;
}
