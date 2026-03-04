#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QLabel>
#include <QMainWindow>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTreeView>
#include "aggregatechatform.h"

QT_BEGIN_NAMESPACE
class QStackedWidget;
class QStandardItemModel;
class QTreeView;
class QLabel;
QT_END_NAMESPACE

class AggregateChatForm;

class EmbeddedWindowContainer : public QWidget
{
public:
    explicit EmbeddedWindowContainer(QWidget* parrent = nullptr);
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

private:
    QWidget* buildLeftSidebar();
    QWidget* buildTopBar();
    QWidget* buildCenterContent();
    void updateTreeViewHeight();
    void showSystemReadyPage();
    void showPlaceholderPage(const QString& title);
    QWidget* buildReadyPage();
    void setupStyles();
    void openAddWindowDialog();
    void openAggregateChatForm();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onPlatformTreeSelectionChanged();
    void onPlatformTreeClicked(const QModelIndex& idx);

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
    QFrame* m_readyCard = nullptr;
    QLabel* m_readyTitle = nullptr;
    QLabel* m_readySubtitle = nullptr;
    QWidget* m_embedPage = nullptr;
    EmbeddedWindowContainer* m_embedContainer = nullptr;
    quintptr m_embeddedHandle = 0;
    AggregateChatForm* m_aggregateChatForm = nullptr;
};

#endif // MAINWINDOW_H
