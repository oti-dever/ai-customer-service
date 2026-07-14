#ifndef AICUSTOMERSERVICEBACKENDWINDOW_H
#define AICUSTOMERSERVICEBACKENDWINDOW_H

#include <QMainWindow>
#include <functional>

class QTreeWidget;
class QStackedWidget;
class AiProviderConfigPage;

class AiCustomerServiceBackendWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit AiCustomerServiceBackendWindow(QWidget* parent = nullptr);

    void focusApiModelPage();

signals:
    void aiProviderConfigChanged();

private:
    QWidget* buildNavSidebar();
    QWidget* buildDashboardPage();
    void applyLocalStyle();

    QTreeWidget* m_nav = nullptr;
    QStackedWidget* m_stack = nullptr;
    AiProviderConfigPage* m_apiConfigPage = nullptr;
    std::function<void()> m_refreshRobotConfigs;
    std::function<void()> m_refreshProductKnowledgeBases;
    std::function<void()> m_refreshEmailServiceConfig;
    bool m_productKnowledgeLoaded = false;
};

#endif
