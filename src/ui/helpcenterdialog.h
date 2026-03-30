#ifndef HELPCENTERDIALOG_H
#define HELPCENTERDIALOG_H

#include <QDialog>

class QWidget;
class QTreeWidget;
class QTextBrowser;

/**
 * 帮助中心：左侧目录 + 右侧 HTML 正文。
 * 文案与章节结构在 helpcenterdialog.cpp 的 populateContent() 中维护。
 */
class HelpCenterDialog : public QDialog
{
public:
    enum class InitialSection {
        UsageGuide, ///< 默认定位到「软件使用说明」首条
        BugLog      ///< 默认定位到「Bug 修复日志」首条
    };

    explicit HelpCenterDialog(InitialSection section, QWidget* parent = nullptr);

    static void openUsageGuide(QWidget* parent);
    static void openBugLog(QWidget* parent);

private:
    void populateContent();

    QTreeWidget* m_toc = nullptr;
    QTextBrowser* m_browser = nullptr;
};

#endif // HELPCENTERDIALOG_H
