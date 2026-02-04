#pragma once

#include <QObject>

class MainWindowView;
class PlatformModel;

/**
 * @brief 主控制器（Controller）
 *
 * 负责连接 View 的信号与 Model 的数据更新/业务命令。
 * 当前阶段只做"空逻辑占位"，保证 UI 架构清晰，便于后续扩展。
 */
class MainController final : public QObject
{
    Q_OBJECT

public:
    explicit MainController(QObject* parent = nullptr);
    ~MainController() override = default;

    MainWindowView* view() const { return m_view; }  ///< 获取主窗口视图

private slots:
    // 响应View发出的信号
    void onRequestQuickStart();          ///< 响应快速启动请求
    void onRequestAutoEmbed();           ///< 响应自动嵌入请求
    void onRequestAdd();                 ///< 响应添加请求
    void onRequestRefresh();             ///< 响应刷新请求
    void onRequestSettings();            ///< 响应设置请求
    void onPlatformGroupClicked(int row); ///< 响应平台分组点击
    void onPlatformSelected(const QString& id); ///< 响应平台选择

private:
    MainWindowView* m_view = nullptr;     ///< 主窗口视图实例
    PlatformModel* m_platformModel = nullptr; ///< 平台数据模型
};
