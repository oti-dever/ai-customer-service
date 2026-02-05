#pragma once

#include <QDialog>
#include <QListWidget>
#include <QStackedWidget>
#include <QScrollArea>

class QPushButton;
class QLabel;
class QFrame;

/**
 * @brief 设置对话框（View层）
 *
 * 负责智能回复设置的UI界面，包含左侧导航栏和右侧内容区域。
 * 遵循MVC模式，只负责界面展示，不包含业务逻辑。
 */
class SettingDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit SettingDialog(QWidget* parent = nullptr);
    ~SettingDialog() override = default;

private:
    /**
     * @brief 构建对话框UI
     */
    void buildUI();

    /**
     * @brief 构建顶部标题栏
     * @return 标题栏组件指针
     */
    QWidget* buildHeader();

    /**
     * @brief 构建左侧导航栏
     * @return 导航栏组件指针
     */
    QWidget* buildLeftSidebar();

    /**
     * @brief 构建右侧内容区域
     * @return 内容区域组件指针
     */
    QWidget* buildRightContent();

    /**
     * @brief 构建简易AI设置页面
     * @return 页面组件指针
     */
    QWidget* buildSimpleAIPage();

    /**
     * @brief 构建AI配置设置页面
     * @return 页面组件指针
     */
    QWidget* buildAIConfigPage();

    /**
     * @brief 构建首响提速设置页面
     * @return 页面组件指针
     */
    QWidget* buildFirstResponsePage();

    /**
     * @brief 构建关键词规则设置页面
     * @return 页面组件指针
     */
    QWidget* buildKeywordRulesPage();

    /**
     * @brief 构建内容替换设置页面
     * @return 页面组件指针
     */
    QWidget* buildContentReplacePage();

    /**
     * @brief 构建默认回复设置页面
     * @return 页面组件指针
     */
    QWidget* buildDefaultReplyPage();

    /**
     * @brief 构建消息推送设置页面
     * @return 页面组件指针
     */
    QWidget* buildMessagePushPage();

    /**
     * @brief 构建线索列表设置页面
     * @return 页面组件指针
     */
    QWidget* buildLeadListPage();

    /**
     * @brief 创建卡片容器
     * @param parent 父组件
     * @return 卡片Frame指针
     */
    QFrame* makeCard(QWidget* parent);

    /**
     * @brief 应用样式表
     */
    void applyStyle();

    /**
     * @brief 处理导航项选择变化
     * @param index 选中的索引
     */
    void onNavigationItemChanged(int index);

private:
    QListWidget* m_navList = nullptr;           ///< 左侧导航列表
    QStackedWidget* m_contentStack = nullptr;   ///< 右侧内容堆叠窗口
    QScrollArea* m_scrollArea = nullptr;        ///< 滚动区域
};
