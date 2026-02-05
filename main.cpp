#include <QApplication>

#include "src/controllers/MainController.h"
#include "src/views/MainWindowView.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // MVC：由 Controller 负责组装 Model + View 并处理交互。
    // 这里不在 main 里写业务逻辑，保持入口整洁。
    MainController controller;
    controller.view()->show();

    return a.exec();
}
