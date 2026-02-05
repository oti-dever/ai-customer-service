#pragma once

#include <QDialog>
#include <QListWidget>

class QScrollArea;
class QFrame;
class QStackedWidget;

/**
 * @brief 管理后台-机器人管理 窗口（仅界面，无业务逻辑）
 *
 * 点击主窗口左侧边栏「机器人管理」时以非模态方式弹出。
 * 布局：左侧深色导航栏 + 右侧白色主内容区（可切换：系统概览 / 机器人管理）。
 */
class RobotManageDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit RobotManageDialog(QWidget* parent = nullptr);
    ~RobotManageDialog() override = default;

private:
    void buildUI();
    void applyStyle();
    QWidget* buildLeftNav();
    QWidget* buildRightContent();
    QWidget* buildOverviewPage();
    QWidget* buildRobotManagePage();
    QWidget* buildKnowledgePage();
    QWidget* buildMessagePage();
    QWidget* buildJargonPage();
    QWidget* buildForbiddenPage();
    QWidget* buildHistoryPage();
    QFrame* makeCard(QWidget* parent, const QString& objectName = QString());
    void onNavItemChanged();

    QListWidget* m_navList = nullptr;
    QStackedWidget* m_contentStack = nullptr;
};
