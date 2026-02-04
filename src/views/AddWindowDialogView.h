#pragma once

#include <QDialog>

class QListView;
class QLineEdit;
class QPushButton;
class QLabel;

/**
 * @brief "添加新窗口"对话框视图（仅界面）
 *
 * 该类只负责搭建 UI，不包含任何业务逻辑。
 * 未来如果需要复杂交互，可以再为它添加独立的 Controller / Model。
 */
class AddWindowDialogView final : public QDialog
{
    Q_OBJECT

public:
    explicit AddWindowDialogView(QWidget* parent = nullptr);
    ~AddWindowDialogView() override = default;

private:
    void buildUi();      ///< 构建用户界面
    void applyStyle();   ///< 应用样式表

private:
    // 左侧：窗口列表区域
    QLineEdit* m_searchEdit = nullptr;   ///< 搜索框控件
    QListView* m_windowList = nullptr;   ///< 窗口列表控件
    QPushButton* m_btnRefresh = nullptr; ///< 刷新按钮控件

    // 右侧：表单区域（仅控件占位）
    QLineEdit* m_platformNameEdit = nullptr;   ///< 平台名称输入框
    QLineEdit* m_processNameEdit = nullptr;    ///< 进程名称输入框
    QLineEdit* m_windowTitleEdit = nullptr;    ///< 窗口标题输入框
    QLineEdit* m_windowClassEdit = nullptr;    ///< 窗口类名输入框
    QLineEdit* m_windowHandleEdit = nullptr;   ///< 窗口句柄输入框

    QPushButton* m_btnCancel = nullptr; ///< 取消按钮
    QPushButton* m_btnOk = nullptr;     ///< 确定按钮
};
