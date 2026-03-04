#ifndef WIN32WINDOWHELPER_H
#define WIN32WINDOWHELPER_H

#include <QVector>
#include <QWidget>

struct WindowInfo {
    QString platformName;
    QString processName;
    QString windowTitle;
    QString className;
    quintptr handle = 0;
    bool isBrowserLike = false;
};

class Win32WindowHelper
{
public:
    static QVector<WindowInfo> enumTopLevelWindows();
    static bool embedWindowIntoWidget(quintptr handle, QWidget* container);
    static void resizeEmbeddedWindow(quintptr handle, QWidget* container);
    static void detachWindow(quintptr handle);
};

#endif // WIN32WINDOWHELPER_H
