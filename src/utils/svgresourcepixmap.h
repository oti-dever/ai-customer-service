#ifndef SVGRESOURCEPIXMAP_H
#define SVGRESOURCEPIXMAP_H

#include <QPixmap>
#include <QString>

/** 将 qrc 中的 SVG 在保持纵横比下缩放到能放进 side×side 的正方形内，居中、透明边。 */
QPixmap svgResourcePixmapFittedInSquare(const QString& qrcPath, int logicalSide);

/** 将 png/jpg 等栅格图等比缩放到能放进 side×side 内并居中。 */
QPixmap rasterResourcePixmapFittedInSquare(const QString& qrcPath, int logicalSide);

#endif
