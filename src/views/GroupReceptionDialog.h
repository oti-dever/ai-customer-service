#pragma once

#include <QDialog>

class QFrame;

/**
 * @brief 管理后台-聚合接待 窗口（仅界面，无业务逻辑）
 *
 * 点击主窗口左侧边栏「聚合接待」时以非模态方式弹出。
 * 三栏布局：左侧会话管理栏、中间聊天主区、右侧客户信息栏；随窗口自适应缩放。
 */
class GroupReceptionDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit GroupReceptionDialog(QWidget* parent = nullptr);
    ~GroupReceptionDialog() override = default;

private:
    void buildUI();
    void applyStyle();
    QWidget* buildLeftPanel();
    QWidget* buildCenterPanel();
    QWidget* buildRightPanel();
};
