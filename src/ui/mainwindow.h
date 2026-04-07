#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QSet>
#include <QLabel>
#include <QMainWindow>
#include <QMap>
#include <QVector>
#include <QStringList>
#include <QtGlobal>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTreeView>
#include <QRect>
#include <QProcess>
#include <QStringDecoder>
#include <QSharedPointer>
#include "../utils/applystyle.h"
#include "../utils/win32windowhelper.h"

class AggregateChatForm;
class RpaConsoleWindow;
class RpaManageDialog;
class QShowEvent;

struct QuickLaunchApp {
    QString name;   ///< 列表显示名（可编辑）
    QString path;   ///< .exe 或 .lnk 绝对路径
    QString group;  ///< 分组名；空表示「默认」分组
};

enum class WindowDisplayMode {
    Embed,
    FloatFollow
};

struct ManagedWindowEntry {
    QString platformName;
    QString platformId;
    quintptr handle = 0;
    WindowDisplayMode mode = WindowDisplayMode::Embed;
    bool isCustomerService = false;
    bool useFloatOwner = true;
    bool useFloatToolWindow = true;
    bool useFloatRaiseAbove = false;
    QWidget* container = nullptr;   // EmbeddedWindowContainer for embed mode
    QWidget* stackPage = nullptr;
    bool wasSetup = false;
    QRect lastDisplayGeometry;      // Last on-screen geometry/content area while managed
    qint64 invisibleSinceMs = 0;
    qint64 closeIntentSinceMs = 0;
};

class EmbeddedWindowContainer : public QWidget
{
public:
    explicit EmbeddedWindowContainer(QWidget* parent = nullptr);
    void setEmbeddedHandle(quintptr handle);
    quintptr embeddedHandle() const;
protected:
    void resizeEvent(QResizeEvent* event) override;
private:
    quintptr m_handle = 0;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT
    friend class RpaManageDialog;

public:
    explicit MainWindow(const QString& username, QWidget* parent = nullptr);
    ~MainWindow();

    static bool isWechatWindowInfo(const WindowInfo& info);

    /** 指定平台 RPA 子进程已缓存的控制台文本（按平台分桶，有长度上限）。 */
    QString rpaProcessLog(const QString& platformId) const;
    /** 清空指定平台已缓存的控制台文本（不影响子进程继续输出）。 */
    void clearRpaProcessLog(const QString& platformId);

signals:
    /** 某平台新增一段控制台输出（UTF-8 解码；与 rpaProcessLog() 缓存同步）。 */
    void rpaProcessOutputAppended(const QString& platformId, const QString& text);

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* buildLeftSidebar();
    QWidget* buildTopBar();
    QWidget* buildCenterContent();
    QWidget* buildReadyPage();
    void buildStatusBar();
    void setupStyles();
    void updateTreeViewHeight();
    void refreshUserProfileBar();
    void onUserProfileBarClicked();

    void showSystemReadyPage();
    void showPlaceholderPage(const QString& title);
    void openAddWindowDialog();
    void openAggregateChatForm();
    void startOneClickAggregate();
    void openQuickLaunchManager();
    void runQuickLaunchApps();
    void openAppHelpDialog();
    void openBugLogDialog();
    void openRpaManageDialog();
    void openRpaConsoleWindow();
    void appendRpaProcessLog(const QString& platformId, const QString& text);

public:
    void addWindowToPlatform(const WindowInfo& info);
    void startBatchAddWindows(const QVector<WindowInfo>& list);
    QSet<quintptr> managedWindowHandles() const;
    ApplyStyle::MainWindowTheme mainWindowTheme() const { return m_mainWindowTheme; }

private:
    void switchToWindow(const QString& platformId);
    void hideCurrentFloatWindow();
    void removeOnlinePlatformItem(const QString& platformId,
                                  bool keepVisible = true,
                                  bool showWindowAfterRelease = true);
    void detachAllWindows();
    void updateFloatFollowPosition();
    QRect displayRectForEntry(const ManagedWindowEntry& entry) const;
    void releaseManagedWindow(ManagedWindowEntry& entry,
                              bool keepVisible,
                              bool showWindowAfterRelease = true);

    static WindowDisplayMode determineDisplayMode(const WindowInfo& info);
    QString matchCustomerServicePlatform(const WindowInfo& info) const;
    QStandardItem* findGroupItem(const QString& groupId) const;
    QStandardItem* findChildItem(QStandardItem* parent, const QString& platformId) const;
    void showPlatformContextMenu(const QPoint& pos);
    void startWechatRpaCalibration(const QString& platformId);
    void startWechatRpaCalibrationStandalone();
    void startWechatRpaCalibrationByHwnd(quintptr hwnd);
    void startQianniuRpaCalibration(const QString& platformId);
    void startPddRpaCalibration(const QString& platformId);
    quintptr findWechatCalibrationWindow() const;
    bool mergeWriteWechatRpaConfig(quintptr hwnd,
                                   const QString& regionId,
                                   const QRect& regionRectWindowPx) const;
    bool mergeWriteQianniuRpaConfig(quintptr hwnd,
                                    const QRect& chatRectWindowPx,
                                    const QRect& headerRectWindowPx) const;
    bool mergeWritePddRpaConfig(quintptr hwnd,
                                 const QRect& chatRectWindowPx,
                                 const QRect& inputRectWindowPx) const;
    void refreshStatusMessage();
    void openStatusMessageManager();
    void applyMainWindowTheme(ApplyStyle::MainWindowTheme theme);
    int oneClickMaxOnlineLimit() const;
    void setOneClickMaxOnlineLimit(int n);
    void updateOneClickAggregateTooltip();
    void applyAlwaysOnTop(bool on);
    void updatePinTopButtonUi();

    QStringList runningRpaPlatformIds() const;
    void startRpaPlatforms(const QStringList& platformIds);
    void stopRpaPlatforms(const QStringList& platformIds);

private slots:
    void onPlatformTreeSelectionChanged();
    void onPlatformTreeClicked(const QModelIndex& idx);
    void checkManagedWindowsState();
    void processNextBatchAdd();

private:
    QString m_username;
    int m_userId = 0;
    QWidget* m_userProfileBar = nullptr;
    QLabel* m_userProfileAvatar = nullptr;
    QLabel* m_userProfileNick = nullptr;
    QStackedWidget* m_centerStack = nullptr;
    QStandardItemModel* m_platformTreeModel = nullptr;
    QTreeView* m_platformTree = nullptr;
    QLabel* m_placeholderLabel = nullptr;
    QWidget* m_placeholderPage = nullptr;
    QToolButton* m_btnAdd = nullptr;
    QToolButton* m_btnRefresh = nullptr;
    QToolButton* m_btnQuickStart = nullptr;
    QToolButton* m_btnOneClickAggregate = nullptr;
    QFrame* m_readyCard = nullptr;
    QLabel* m_readyTitle = nullptr;
    QLabel* m_readySubtitle = nullptr;
    QLabel* m_statusMessage = nullptr;
    QLabel* m_statusSeparator = nullptr;
    QLabel* m_statusTime = nullptr;
    QToolButton* m_btnThemeSwitch = nullptr;
    ApplyStyle::MainWindowTheme m_mainWindowTheme = ApplyStyle::MainWindowTheme::Default;
    AggregateChatForm* m_aggregateChatForm = nullptr;

    QStandardItem* m_onlineGroup = nullptr;
    QStandardItem* m_manageGroup = nullptr;
    QStandardItem* m_csGroup = nullptr;

    QMap<QString, ManagedWindowEntry> m_managedWindows;
    QStringList m_customStatusMessages;
    QString m_activeWindowId;
    int m_nextOnlineId = 0;
    QTimer* m_windowStateTimer = nullptr;

    QVector<WindowInfo> m_batchAddList;
    int m_batchAddIndex = 0;
    int m_batchAddSuccessCount = 0;
    QWidget* m_batchAddOverlay = nullptr;
    QLabel* m_batchAddPrompt = nullptr;

    QVector<QuickLaunchApp> m_quickLaunchApps;
    bool m_quickLaunchOnlyIfNotRunning = true;

    QMap<QString, QProcess*> m_rpaProcesses;
    /** 与 m_rpaProcesses 同步：子进程控制台输出按 UTF-8 增量解码。 */
    QMap<QString, QSharedPointer<QStringDecoder>> m_rpaConsoleDecoders;
    QMap<QString, QString> m_rpaProcessLogs;
    RpaConsoleWindow* m_rpaConsoleWindow = nullptr;
    QToolButton* m_btnRpaManage = nullptr;
    QToolButton* m_btnPinTop = nullptr;
    bool m_alwaysOnTop = false;
};

#endif // MAINWINDOW_H
