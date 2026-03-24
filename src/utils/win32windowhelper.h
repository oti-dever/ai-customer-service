#ifndef WIN32WINDOWHELPER_H
#define WIN32WINDOWHELPER_H

#include <QIcon>
#include <QPoint>
#include <QVector>
#include <QWidget>
#include <QRect>
#include <QSize>
#include <QtGlobal>

struct WindowInfo {
    QString platformName;
    QString processName;
    QString processPath;
    QString windowTitle;
    QString className;
    quintptr handle = 0;
    bool isBrowserLike = false;
};

class Win32WindowHelper
{
public:
    static QVector<WindowInfo> enumTopLevelWindows();

    // Embed mode: SetParent-based
    static bool embedWindowIntoWidget(quintptr handle, QWidget* container);
    static void resizeEmbeddedWindow(quintptr handle, QWidget* container);
    static void detachWindow(quintptr handle, const QRect& targetClientRect = {});

    // Float-follow mode: positioned overlay without SetParent
    static void setupFloatFollow(quintptr handle,
                                 quintptr ownerHandle = 0,
                                 bool useOwner = true,
                                 bool useToolWindow = true);
    static void showWindowAt(quintptr handle,
                             int x,
                             int y,
                             int w,
                             int h,
                             bool raiseAbove = false);
    static void hideWindow(quintptr handle);
    static void detachFloatFollow(quintptr handle,
                                  bool showWindow = true,
                                  bool restoreTaskbarEntry = true,
                                  bool clearOwner = true);
    static void minimizeWindow(quintptr handle);

    // Query helpers
    static QRect windowRect(quintptr handle);
    /// 将 overlay 内选框映射为 target 窗口相对物理坐标（与 PrintWindow 位图一致）。
    /// selection 为 Qt 逻辑坐标；overlayLogicalClientSize 传 overlay->size()，用于与 GetClientRect 对齐 DPI。
    /// devicePixelRatio 仅在无法计算比例时作兜底。
    static QRect mapOverlaySelectionToTargetWindowRelative(quintptr overlayHwnd,
                                                           quintptr targetHwnd,
                                                           const QRect& selectionInOverlayClientLogical,
                                                           qreal devicePixelRatio,
                                                           const QSize& overlayLogicalClientSize);
    static QRect windowRectForClientRect(quintptr handle, const QRect& targetClientRect);
    static QIcon windowIcon(const WindowInfo& info);
    static unsigned int windowDpi(quintptr handle);

    /// 窗口所属进程 exe 文件名，如 Weixin.exe；失败返回空串
    static QString executableBaseNameForWindow(quintptr handle);
    static QString windowTitle(quintptr handle);

    static bool isWindowValid(quintptr handle);
    static bool isWindowVisible(quintptr handle);
    static bool isWindowHung(quintptr handle);
    static bool isLeftMouseButtonPressed();
    static bool isPointInsideWindow(quintptr handle, const QPoint& globalPos);
};

#endif // WIN32WINDOWHELPER_H
