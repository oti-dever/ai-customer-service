#ifndef SWORDCURSOR_H
#define SWORDCURSOR_H

#include <QString>

/**
 * 剑形鼠标光标（资源默认图或用户自定义 PNG/JPG），通过 QSettings 开关。
 * 使用 QApplication::overrideCursor；退出主流程前应调用 restore()。
 */
namespace SwordCursor {

bool isEnabledInSettings();
void setEnabledInSettings(bool on);

QString customImagePath();
void setCustomImagePath(const QString& absolutePathOrEmpty);

void applyIfEnabled();
void restore();

} // namespace SwordCursor

#endif // SWORDCURSOR_H
