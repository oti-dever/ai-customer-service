QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    src/views/SettingDialog.cpp \
    src/views/RobotManageDialog.cpp \
    src/views/GroupReceptionDialog.cpp \
    src\controllers\MainController.cpp \
    src\models\PlatformModel.cpp \
    src\views\MainWindowView.cpp \
    src\views\AddWindowDialogView.cpp

HEADERS += \
    mainwindow.h \
    src/views/SettingDialog.h \
    src/views/RobotManageDialog.h \
    src/views/GroupReceptionDialog.h \
    src\controllers\MainController.h \
    src\models\PlatformModel.h \
    src\views\MainWindowView.h \
    src\views\AddWindowDialogView.h

# NOTE: UI is built in code for better maintainability/extension (MVC).

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RC_ICONS = head.ico

RESOURCES += \
    resource.qrc
