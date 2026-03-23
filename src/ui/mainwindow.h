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

struct QuickLaunchApp {
    QString name;
    QString path;
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

public:
    explicit MainWindow(const QString& username, QWidget* parent = nullptr);
    ~MainWindow();

    static bool isWechatWindowInfo(const WindowInfo& info);

protected:
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

    void showSystemReadyPage();
    void showPlaceholderPage(const QString& title);
    void openAddWindowDialog();
    void openAggregateChatForm();
    void startOneClickAggregate();
    void openQuickLaunchManager();
    void runQuickLaunchApps();
    void openAppHelpDialog();
    void openBugLogDialog();

public:
    void addWindowToPlatform(const WindowInfo& info);
    void startBatchAddWindows(const QVector<WindowInfo>& list);
    QSet<quintptr> managedWindowHandles() const;

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
    bool writeWechatRpaConfigRelativeToWindow(quintptr hwnd,
                                              const QRect& chatRectInWindowPx,
                                              const QSize& windowSizePx,
                                              const QString& platformConversationId,
                                              const QString& customerName) const;
    void refreshStatusMessage();
    void openStatusMessageManager();
    void applyMainWindowTheme(ApplyStyle::MainWindowTheme theme);
    int oneClickMaxOnlineLimit() const;
    void setOneClickMaxOnlineLimit(int n);
    void updateOneClickAggregateTooltip();

private slots:
    void onPlatformTreeSelectionChanged();
    void onPlatformTreeClicked(const QModelIndex& idx);
    void checkManagedWindowsState();
    void processNextBatchAdd();

private:
    QString m_username;
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
};

#endif // MAINWINDOW_H
