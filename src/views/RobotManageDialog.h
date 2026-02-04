#pragma once

#include <QDialog>
#include <QListWidget>

class QScrollArea;
class QFrame;

/**
 * @brief 管理后台-机器人管理 窗口（仅界面，无业务逻辑）
 *
 * 点击主窗口左侧边栏「机器人管理」时以非模态方式弹出。
 * 布局：左侧深色导航栏 + 右侧白色主内容区（标题栏、搜索、统计卡片、筛选栏、空状态）。
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
    QFrame* makeCard(QWidget* parent, const QString& objectName = QString());

    QListWidget* m_navList = nullptr;
    QScrollArea* m_contentScroll = nullptr;
};
