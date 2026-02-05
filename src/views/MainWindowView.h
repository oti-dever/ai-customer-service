#pragma once

#include <QMainWindow>
#include <QToolButton>

class QTreeView;
class QStandardItemModel;
class QStackedWidget;
class QToolButton;
class QLabel;
class QFrame;

class PlatformModel;

/**
 * @brief 主窗口视图（View）
 *
 * 只负责界面结构与样式，不包含业务逻辑。
 * 所有"点击/选择"等交互，都通过信号暴露给 Controller。
 */
class MainWindowView final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindowView(QWidget* parent = nullptr);
    ~MainWindowView() override = default;

    void setPlatformModel(PlatformModel* model);  ///< 设置平台数据模型

signals:
    void requestQuickStart();                   ///< 右上角"快速启动应用"
    void requestAutoEmbed();                    ///< "自动嵌入窗口"
    void requestAdd();                          ///< 顶部"+"
    void requestRefresh();                      ///< 顶部刷新
    void requestSettings();                     ///< 设置按钮点击
    void platformGroupClicked(int row);         ///< 左侧平台分组点击（兼容）
    void platformSelected(const QString& id);   ///< 左侧选中平台（qq/workwechat/qianniu/online）

private:
    // 构建界面组件
    QWidget* buildTopBar();                     ///< 构建顶部工具栏
    QWidget* buildLeftSidebar();                ///< 构建左侧边栏
    QWidget* buildCenterContent();              ///< 构建中心内容区域
    QWidget* buildReadyPage();                  ///< 构建系统就绪页面
    QWidget* buildPlatformNotRunningPage(const QString& name, const QIcon& icon, const QString& objectName); ///< 构建平台未运行页面

    void applyStyle();                          ///< 应用样式表
    void onPlatformTreeSelectionChanged();      ///< 平台树选择变化处理
    void updateTreeViewHeight();                ///< 树视图在展开/折叠时动态调整高度

private:
    PlatformModel* m_platformModel = nullptr;   ///< 平台数据模型指针
    QTreeView* m_platformTree = nullptr;        ///< 左侧平台树视图
    QStandardItemModel* m_platformTreeModel = nullptr; ///< 平台树数据模型
    QStackedWidget* m_centerStack = nullptr;    ///< 中心区域堆叠窗口

    // 顶部工具栏按钮
    QToolButton* m_btnAdd = nullptr;            ///< 添加按钮
    QToolButton* m_btnRefresh = nullptr;        ///< 刷新按钮
    QToolButton* m_btnQuickStart = nullptr;     ///< 快速启动按钮

    // 左侧边栏按钮
    QToolButton* m_btnSettings = nullptr;       ///< 设置按钮

    // 就绪页面组件
    QFrame* m_readyCard = nullptr;              ///< 就绪卡片
    QLabel* m_readyTitle = nullptr;             ///< 就绪标题
    QLabel* m_readySubtitle = nullptr;          ///< 就绪副标题
};
