cmake_minimum_required(VERSION 3.22)

project(j5xnlte_qmi_radio_core VERSION 0.5.0 LANGUAGES CXX)

option(QMI_BUILD_TESTS "Build host-side protocol and transport tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# QMI core library
# ============================================================

add_library(j5qmi_core STATIC
    src/qmi_frame.cpp
    src/qmi_message.cpp
    src/qmi_transport.cpp
    src/qmi_client_manager.cpp
)

target_include_directories(j5qmi_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(j5qmi_core PUBLIC cxx_std_17)

target_compile_options(j5qmi_core
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
)

set_target_properties(j5qmi_core PROPERTIES
    OUTPUT_NAME j5qmi_core
    POSITION_INDEPENDENT_CODE ON
)

# ============================================================
# QMI modem backend
# ============================================================

add_library(j5qmi_modem STATIC
    android/radio/modem/qmi_modem_backend.cpp
)

target_include_directories(j5qmi_modem
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/android/radio/modem
)

target_link_libraries(j5qmi_modem
    PUBLIC
        j5qmi_core
)

target_compile_features(j5qmi_modem PUBLIC cxx_std_17)

target_compile_options(j5qmi_modem
    PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Werror
)

set_target_properties(j5qmi_modem PROPERTIES
    OUTPUT_NAME j5qmi_modem
    POSITION_INDEPENDENT_CODE ON
)

# ============================================================
# Host-side tests
# ============================================================

if(QMI_BUILD_TESTS)
    enable_testing()

    add_executable(qmi_frame_test
        tests/qmi_frame_test.cpp
    )

    target_link_libraries(qmi_frame_test
        PRIVATE j5qmi_core
    )

    add_test(
        NAME qmi_frame_test
        COMMAND qmi_frame_test
    )

    add_executable(qmi_message_test
        tests/qmi_message_test.cpp
    )

    target_link_libraries(qmi_message_test
        PRIVATE j5qmi_core
    )

    add_test(
        NAME qmi_message_test
        COMMAND qmi_message_test
    )

    add_executable(qmi_transport_test
        tests/qmi_transport_test.cpp
    )

    target_link_libraries(qmi_transport_test
        PRIVATE j5qmi_core
    )

    add_test(
        NAME qmi_transport_test
        COMMAND qmi_transport_test
    )

    add_executable(qmi_client_manager_test
        tests/qmi_client_manager_test.cpp
    )

    target_link_libraries(qmi_client_manager_test
        PRIVATE j5qmi_core
    )

    add_test(
        NAME qmi_client_manager_test
        COMMAND qmi_client_manager_test
    )
endif()
