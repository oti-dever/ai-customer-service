#ifndef APPLYSTYLE_H
#define APPLYSTYLE_H

#include <QString>

class ApplyStyle {
public:
    // 应用机器人管理对话框样式
    static QString robotManageFullStyle();
    // 全局样式
    static QString globalStyle();
    // 左侧导航栏
    static QString leftNavStyle();
    // 右侧内容区
    static QString contentAreaStyle();
    // 系统概览页
    static QString overviewPageStyle();
    // 知识库管理页
    static QString knowledgePageStyle();
    // 消息处理页
    static QString messagePageStyle();
    // 行话转换页
    static QString jargonPageStyle();
    // 违禁词管理页
    static QString forbiddenPageStyle();
    // 对话历史页
    static QString historyPageStyle();
    // 数据备份页
    static QString backupPageStyle();
    // 日志管理页
    static QString logPageStyle();

    // 设置对话框样式
    static QString settingsStyle();
    // 设置界面左侧导航栏
    static QString settingsLeftNavStyle();

private:
    ApplyStyle() = delete; // 纯静态类，禁止实例化
    ~ApplyStyle() = delete;
};

#endif // APPLYSTYLE_H
