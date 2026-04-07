cmake_minimum_required(VERSION 3.16)

# 可执行文件名使用 yy-ai-customer-service（OUTPUT_NAME）；CMake 标识符不宜含连字符
project(yy_ai_customer_service VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/Zc:__cplusplus /permissive- /utf-8)
endif()

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Svg
    Widgets
    Sql
)

set(SOURCES
    src/main.cpp
    resources/app_resources.qrc
    resources/windows_app_icon.rc
    src/data/database.cpp
    src/data/userdao.cpp
    src/data/conversationdao.cpp
    src/data/messagedao.cpp
    src/data/messagesendeventdao.cpp
    src/utils/logger.cpp
    src/utils/cryptoutil.cpp
    src/utils/applystyle.cpp
    src/utils/win32windowhelper.cpp
    src/utils/swordcursor.cpp
    src/core/authmanager.cpp
    src/core/messagerouter.cpp
    src/core/conversationmanager.cpp
    src/services/platforms/simplatformadapter.cpp
    src/services/platforms/qianniurp_adapter.cpp
    src/services/platforms/wechatrp_adapter.cpp
    src/services/platforms/pddrp_adapter.cpp
    src/ui/loginwindow.cpp
    src/ui/editprofiledialog.cpp
    src/ui/mainwindow.cpp
    src/ui/rpamanagedialog.cpp
    src/ui/rpa_console_window.cpp
    src/ui/addwindowdialog.cpp
    src/ui/aggregatechatform.cpp
    src/ui/helpcenterdialog.cpp
)

set(HEADERS
    src/data/database.h
    src/data/userdao.h
    src/data/conversationdao.h
    src/data/messagedao.h
    src/data/messagesendeventdao.h
    src/utils/logger.h
    src/utils/cryptoutil.h
    src/utils/applystyle.h
    src/utils/win32windowhelper.h
    src/utils/swordcursor.h
    src/core/types.h
    src/core/authmanager.h
    src/core/messagerouter.h
    src/core/conversationmanager.h
    src/services/platforms/iplatformadapter.h
    src/services/platforms/simplatformadapter.h
    src/services/platforms/qianniurp_adapter.h
    src/services/platforms/wechatrp_adapter.h
    src/services/platforms/pddrp_adapter.h
    src/ui/loginwindow.h
    src/ui/editprofiledialog.h
    src/ui/mainwindow.h
    src/ui/rpamanagedialog.h
    src/ui/rpa_console_window.h
    src/ui/addwindowdialog.h
    src/ui/aggregatechatform.h
    src/ui/foldarrowcombobox.h
    src/ui/helpcenterdialog.h
)

qt_add_executable(yy_ai_customer_service
    MANUAL_FINALIZATION
    ${SOURCES}
    ${HEADERS}
)

set_target_properties(yy_ai_customer_service PROPERTIES
    OUTPUT_NAME "yy-ai-customer-service"
)

target_include_directories(yy_ai_customer_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(yy_ai_customer_service PRIVATE
    PROJECT_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(yy_ai_customer_service PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Svg
    Qt6::Widgets
    Qt6::Sql
)

if(WIN32)
    target_link_libraries(yy_ai_customer_service PRIVATE ole32 shell32)
endif()

set_target_properties(yy_ai_customer_service PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(yy_ai_customer_service)
endif()

enable_testing()
add_subdirectory(tests)
