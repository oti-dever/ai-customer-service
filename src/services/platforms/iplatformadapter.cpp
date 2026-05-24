#include "iplatformadapter.h"

// 该文件的唯一目的：让 Qt AUTOMOC 为带 Q_OBJECT 的 IPlatformAdapter 生成 moc 实现，
// 否则链接阶段会缺少 metaObject/qt_metacast/qt_metacall/staticMetaObject 以及 signals 的桩实现。
