#ifndef APPLYSTYLE_H
#define APPLYSTYLE_H

#include <QColor>
#include <QString>

struct PlatformTreeColors {
    QColor groupBgDefault;
    QColor groupBgHover;
    QColor groupBgSelected;
    QColor groupTextColor;
    QColor groupArrowColor;
    QColor itemBgDefault;
    QColor itemBgHover;
    QColor itemBgSelected;
    QColor itemTextColor;
    QColor itemAccentBarColor;
    QColor itemInactiveBgDefault;
    QColor itemInactiveBgHover;
    QColor itemInactiveBgSelected;
    QColor itemInactiveTextColor;
    QColor csDotActivated;
    QColor csDotInactive;
    qreal itemInactiveIconOpacity;
};

class ApplyStyle {
public:
    enum class MainWindowTheme {
        Default = 0,
        Cool = 1,
        Warm = 2,
    };

    static QString loginWindowStyle();
    /** 主窗口 QSS；Default 为经典黑灰白（深侧栏 + 浅工作区 + 白卡片）。 */
    static QString mainWindowStyle();
    static QString mainWindowStyle(MainWindowTheme theme);
    static MainWindowTheme loadSavedMainWindowTheme();
    static void saveMainWindowTheme(MainWindowTheme theme);

    static QString aggregateChatFormStyle();
    static QString aggregateChatFormStyle(MainWindowTheme theme);
    static QString helpDialogStyle();
    static QString helpDialogStyle(MainWindowTheme theme);
    static QString helpDialogHtmlBodyTextColor(MainWindowTheme theme);
    static QString helpDialogHtmlHrBorderColor(MainWindowTheme theme);
    static QString helpDialogHtmlWarningColor(MainWindowTheme theme);
    static QString statusMessageManagerStyle(MainWindowTheme theme);
    static QString quickLaunchManagerStyle(MainWindowTheme theme);
    /** 与主窗口主题一致；Default 为天蓝色阶（见实现内注释）。 */
    static QString addWindowDialogStyle(MainWindowTheme theme);
    static QString addWindowDialogStyle();

    static PlatformTreeColors platformTreeColors(MainWindowTheme theme);

private:
    ApplyStyle() = delete;
    ~ApplyStyle() = delete;
};

#endif // APPLYSTYLE_H
