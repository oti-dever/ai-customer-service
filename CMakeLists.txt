cmake_minimum_required(VERSION 3.16)

project(demo1 VERSION 0.1.0 LANGUAGES CXX)

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
    src/utils/logger.cpp
    src/utils/cryptoutil.cpp
    src/utils/applystyle.cpp
    src/utils/win32windowhelper.cpp
    src/core/authmanager.cpp
    src/core/messagerouter.cpp
    src/core/conversationmanager.cpp
    src/services/platforms/simplatformadapter.cpp
    src/services/platforms/qianniurp_adapter.cpp
    src/services/platforms/wechatrp_adapter.cpp
    src/ui/loginwindow.cpp
    src/ui/mainwindow.cpp
    src/ui/addwindowdialog.cpp
    src/ui/aggregatechatform.cpp
)

set(HEADERS
    src/data/database.h
    src/data/userdao.h
    src/data/conversationdao.h
    src/data/messagedao.h
    src/utils/logger.h
    src/utils/cryptoutil.h
    src/utils/applystyle.h
    src/utils/win32windowhelper.h
    src/core/types.h
    src/core/authmanager.h
    src/core/messagerouter.h
    src/core/conversationmanager.h
    src/services/platforms/iplatformadapter.h
    src/services/platforms/simplatformadapter.h
    src/services/platforms/qianniurp_adapter.h
    src/services/platforms/wechatrp_adapter.h
    src/ui/loginwindow.h
    src/ui/mainwindow.h
    src/ui/addwindowdialog.h
    src/ui/aggregatechatform.h
)

qt_add_executable(demo1
    MANUAL_FINALIZATION
    ${SOURCES}
    ${HEADERS}
)

target_include_directories(demo1 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(demo1 PRIVATE
    PROJECT_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(demo1 PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Svg
    Qt6::Widgets
    Qt6::Sql
)

set_target_properties(demo1 PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(demo1)
endif()
