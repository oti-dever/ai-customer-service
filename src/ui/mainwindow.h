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
#include "../utils/applystyle.h"
#include "../utils/win32windowhelper.h"

class AggregateChatForm;
class RobotAssistantWidget;
class AiCustomerServiceBackendWindow;
class PythonServiceConnectionDialog;
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
    friend class PythonServiceConnectionDialog;

public:
    explicit MainWindow(const QString& username, QWidget* parent = nullptr);
    ~MainWindow();

    static bool isWechatWindowInfo(const WindowInfo& info);

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
    void openRobotAssistantPage();
    void startOneClickAggregate();
    void openQuickLaunchManager();
    void runQuickLaunchApps();
    void openAppHelpDialog();
    void openBugLogDialog();
    void openPythonServiceConnectionDialog();

public:
    void addWindowToPlatform(const WindowInfo& info);
    void startBatchAddWindows(const QVector<WindowInfo>& list);
    QSet<quintptr> managedWindowHandles() const;
    ApplyStyle::MainWindowTheme mainWindowTheme() const { return m_mainWindowTheme; }
    void openAiCustomerServiceBackendWindow(bool goToApiModelPage = false);

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
    void refreshStatusMessage();
    void openStatusMessageManager();
    void applyMainWindowTheme(ApplyStyle::MainWindowTheme theme);
    int oneClickMaxOnlineLimit() const;
    void setOneClickMaxOnlineLimit(int n);
    void updateOneClickAggregateTooltip();
    void applyAlwaysOnTop(bool on);
    void updatePinTopButtonUi();

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
    QToolButton* m_btnQuickStart = nullptr;
    QToolButton* m_btnOneClickAggregate = nullptr;
    QFrame* m_readyCard = nullptr;
    QLabel* m_readyTitle = nullptr;
    QLabel* m_readySubtitle = nullptr;
    QLabel* m_statusMessage = nullptr;
    QLabel* m_statusSeparator = nullptr;
    QLabel* m_statusTime = nullptr;
    ApplyStyle::MainWindowTheme m_mainWindowTheme = ApplyStyle::MainWindowTheme::Default;
    /** 聚合接待独立顶层窗；`m_aggregateChatForm` 为其 centralWidget，随窗体销毁。 */
    QMainWindow* m_aggregateReceptionWindow = nullptr;
    AggregateChatForm* m_aggregateChatForm = nullptr;
    RobotAssistantWidget* m_robotAssistantWidget = nullptr;
    AiCustomerServiceBackendWindow* m_aiCustomerServiceBackendWindow = nullptr;

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

    QToolButton* m_btnPythonServiceConnection = nullptr;
    QToolButton* m_btnPinTop = nullptr;
    bool m_alwaysOnTop = false;
};

#endif // MAINWINDOW_H
