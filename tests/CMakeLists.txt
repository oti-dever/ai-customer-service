find_package(Qt6 REQUIRED COMPONENTS Core Network Sql Test)

set(TEST_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/testsupport
)

if(WIN32 AND TARGET Qt6::Core)
    cmake_path(GET Qt6_DIR PARENT_PATH _qt_cmake_parent)
    cmake_path(GET _qt_cmake_parent PARENT_PATH _qt_lib_parent)
    cmake_path(GET _qt_lib_parent PARENT_PATH QT_INSTALL_PREFIX)
    cmake_path(APPEND QT_BIN "${QT_INSTALL_PREFIX}" "bin")
endif()

function(configure_app_test target)
    target_include_directories(${target} PRIVATE ${TEST_INCLUDE_DIRS})
    target_compile_definitions(${target} PRIVATE
        PROJECT_ROOT_DIR="${CMAKE_SOURCE_DIR}"
    )
    add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
    if(WIN32 AND DEFINED QT_BIN)
        set_tests_properties(${target} PROPERTIES
            ENVIRONMENT "PATH=${QT_BIN};$ENV{PATH}"
        )
    endif()
endfunction()

set(DATA_LAYER_SOURCES
    ${CMAKE_SOURCE_DIR}/src/models/unifiedmodels.cpp
    ${CMAKE_SOURCE_DIR}/src/core/types.cpp
    ${CMAKE_SOURCE_DIR}/src/data/database.cpp
    ${CMAKE_SOURCE_DIR}/src/data/conversationdao.cpp
    ${CMAKE_SOURCE_DIR}/src/data/messagedao.cpp
    ${CMAKE_SOURCE_DIR}/src/data/wechatmessagedao.cpp
)

set(AI_CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/services/ai/aiprovidercatalog.cpp
    ${CMAKE_SOURCE_DIR}/src/services/ai/airequestassembler.cpp
    ${CMAKE_SOURCE_DIR}/src/services/ai/aistreamingsession.cpp
    ${CMAKE_SOURCE_DIR}/src/services/ai/aiservicefacade.cpp
    ${CMAKE_SOURCE_DIR}/src/services/ai/openaicompatclient.cpp
    ${CMAKE_SOURCE_DIR}/src/services/ai/arkfilesresponses.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/imagedataurl.cpp
)

qt_add_executable(yy_ai_customer_service_tests
    test_cryptoutil.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/cryptoutil.cpp
)
set_target_properties(yy_ai_customer_service_tests PROPERTIES
    OUTPUT_NAME "yy-ai-customer-service-tests"
)
target_link_libraries(yy_ai_customer_service_tests PRIVATE
    Qt6::Core
    Qt6::Test
)
configure_app_test(yy_ai_customer_service_tests)

qt_add_executable(yy_ai_customer_service_data_tests
    test_data_access.cpp
    ${DATA_LAYER_SOURCES}
)
set_target_properties(yy_ai_customer_service_data_tests PROPERTIES
    OUTPUT_NAME "yy-ai-customer-service-data-tests"
)
target_link_libraries(yy_ai_customer_service_data_tests PRIVATE
    Qt6::Core
    Qt6::Sql
    Qt6::Test
)
configure_app_test(yy_ai_customer_service_data_tests)

qt_add_executable(yy_ai_customer_service_router_tests
    test_message_router.cpp
    ${CMAKE_SOURCE_DIR}/src/services/platforms/iplatformadapter.cpp
    ${CMAKE_SOURCE_DIR}/src/core/messagerouter.cpp
    ${DATA_LAYER_SOURCES}
)
set_target_properties(yy_ai_customer_service_router_tests PROPERTIES
    OUTPUT_NAME "yy-ai-customer-service-router-tests"
)
target_link_libraries(yy_ai_customer_service_router_tests PRIVATE
    Qt6::Core
    Qt6::Sql
    Qt6::Test
)
configure_app_test(yy_ai_customer_service_router_tests)

qt_add_executable(yy_ai_customer_service_openai_tests
    test_openaicompatclient.cpp
    ${CMAKE_SOURCE_DIR}/src/services/ai/openaicompatclient.cpp
)
set_target_properties(yy_ai_customer_service_openai_tests PROPERTIES
    OUTPUT_NAME "yy-ai-customer-service-openai-tests"
)
target_link_libraries(yy_ai_customer_service_openai_tests PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
)
configure_app_test(yy_ai_customer_service_openai_tests)

qt_add_executable(yy_ai_customer_service_auth_tests
    test_authmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/core/authmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/data/userdao.cpp
    ${CMAKE_SOURCE_DIR}/src/data/database.cpp
    ${CMAKE_SOURCE_DIR}/src/models/unifiedmodels.cpp
    ${CMAKE_SOURCE_DIR}/src/core/types.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/cryptoutil.cpp
)
set_target_properties(yy_ai_customer_service_auth_tests PROPERTIES
    OUTPUT_NAME "yy-ai-customer-service-auth-tests"
)
target_link_libraries(yy_ai_customer_service_auth_tests PRIVATE
    Qt6::Core
    Qt6::Sql
    Qt6::Test
)
configure_app_test(yy_ai_customer_service_auth_tests)

qt_add_executable(yy_ai_customer_service_ai_tests
    test_aiabstractions.cpp
    ${AI_CORE_SOURCES}
)
set_target_properties(yy_ai_customer_service_ai_tests PROPERTIES
    OUTPUT_NAME "yy-ai-customer-service-ai-tests"
)
target_link_libraries(yy_ai_customer_service_ai_tests PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Test
)
configure_app_test(yy_ai_customer_service_ai_tests)
