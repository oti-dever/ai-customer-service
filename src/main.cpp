#include "ui/loginwindow.h"
#include "ui/mainwindow.h"
#include "data/database.h"
#include "core/conversationmanager.h"
#include "utils/logger.h"
#include "utils/swordcursor.h"
#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QMessageBox>

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);
    a.setApplicationName("AI客服");
    a.setOrganizationName("Demo");
    a.setWindowIcon(QIcon(QStringLiteral(":/app_icon.svg")));

    SwordCursor::restore();

    qRegisterMetaType<PlatformMessage>("PlatformMessage");
    qRegisterMetaType<ConversationInfo>("ConversationInfo");
    qRegisterMetaType<MessageRecord>("MessageRecord");

    Logger::init();

    if (!Database::getInstance().open()) {
        QMessageBox::critical(nullptr, "错误", "数据库初始化失败，无法启动应用。");
        return 1;
    }
    qInfo() << "数据库初始化成功";

    ConversationManager::instance().initialize();

    QObject::connect(&a, &QCoreApplication::aboutToQuit, [] { SwordCursor::restore(); });

    qInfo() << "加载登录界面...";
    LoginWindow login;
    if (login.exec() != QDialog::Accepted) {
        SwordCursor::restore();
        Logger::shutdown();
        return 0;
    }

    MainWindow w(login.loggedInUsername());
    w.show();
    qInfo() << "已进入AI客服主界面, 用户:" << login.loggedInUsername();

    int ret = a.exec();
    SwordCursor::restore();
    Logger::shutdown();
    return ret;
}
