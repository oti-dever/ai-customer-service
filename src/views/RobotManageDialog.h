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
    QWidget* buildLeftNav(); // 构建左侧导航栏
    QWidget* buildRightContent();
    QWidget* buildOverviewPage(); // 构建系统概览页面
    QWidget* buildRobotManagePage(); // 构建机器人管理页面
    QWidget* buildKnowledgePage(); // 构建知识库管理页面
    QWidget* buildMessagePage(); // 构建消息处理页面
    QWidget* buildJargonPage(); // 构建行话转换页面
    QWidget* buildForbiddenPage(); // 构建违禁词管理页面
    QWidget* buildHistoryPage(); // 构建对话历史页面
    QWidget* buildBackupPage(); // 构建数据备份页面
    QWidget* buildLogPage(); // 构建日志管理页面
    // QScrollArea* buildCommonPage(const QString& title, const QString& sub);
    QFrame* makeCard(QWidget* parent, const QString& objectName = QString());
    void onNavItemChanged();

    QListWidget* m_navList = nullptr;
    QStackedWidget* m_contentStack = nullptr;
};
