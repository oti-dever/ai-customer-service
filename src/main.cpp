#include "ui/loginwindow.h"
#include "ui/mainwindow.h"
#include "data/database.h"
#include "core/conversationmanager.h"
#include "core/platformbootstrap.h"
#include "ipc/ipcservice.h"
#include "utils/appsettings.h"
#include "utils/applystyle.h"
#include "utils/logger.h"
#include "utils/scrollbehavior.h"
#include "utils/swordcursor.h"
#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QMessageBox>

int main(int argc, char* argv[])
{
    QApplication a(argc, argv);
    AppSettings::configureApplication(a);
    a.setWindowIcon(QIcon(QStringLiteral(":/app_icon.svg")));
    a.setStyleSheet(ApplyStyle::globalScrollBarStyle());
    ScrollBehavior::install(a);

    SwordCursor::restore();

    qRegisterMetaType<PlatformMessage>("PlatformMessage");
    qRegisterMetaType<ConversationInfo>("ConversationInfo");
    qRegisterMetaType<MessageRecord>("MessageRecord");
    qRegisterMetaType<Models::Message>("Models::Message");
    qRegisterMetaType<Models::Conversation>("Models::Conversation");
    qRegisterMetaType<Ipc::AiSuggestionResponse>("Ipc::AiSuggestionResponse");

    Logger::init();

    if (!Database::getInstance().open()) {
        QMessageBox::critical(nullptr, "错误", "数据库初始化失败，无法启动应用。");
        return 1;
    }
    qInfo() << "数据库初始化成功";

    QObject::connect(&a, &QCoreApplication::aboutToQuit, [] {
        Ipc::IpcService::instance().shutdown();
        SwordCursor::restore();
    });

    qInfo() << "加载登录界面...";
    LoginWindow login;
    if (login.exec() != QDialog::Accepted) {
        Ipc::IpcService::instance().shutdown();
        SwordCursor::restore();
        Logger::shutdown();
        return 0;
    }

    PlatformBootstrap::initializeDefaultPlatforms(ConversationManager::instance());

    MainWindow w(login.loggedInUsername());
    w.show();
    qInfo() << "已进入AI客服主界面, 用户:" << login.loggedInUsername();

    int ret = a.exec();
    SwordCursor::restore();
    Logger::shutdown();
    return ret;
}
