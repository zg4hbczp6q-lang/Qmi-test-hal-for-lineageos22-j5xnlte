
cmake_minimum_required(VERSION 3.22)
project(j5xnlte_qmi_radio_core VERSION 0.3.0 LANGUAGES CXX)

option(QMI_BUILD_TESTS "Build host-side protocol and transport tests" ON)

add_library(j5qmi_core STATIC
    src/qmi_frame.cpp
    src/qmi_message.cpp
    src/qmi_transport.cpp
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

if(QMI_BUILD_TESTS)
    enable_testing()

    add_executable(qmi_frame_test
        tests/qmi_frame_test.cpp
    )
    target_link_libraries(qmi_frame_test PRIVATE j5qmi_core)
    add_test(NAME qmi_frame_test COMMAND qmi_frame_test)

    add_executable(qmi_message_test
        tests/qmi_message_test.cpp
    )
    target_link_libraries(qmi_message_test PRIVATE j5qmi_core)
    add_test(NAME qmi_message_test COMMAND qmi_message_test)

    add_executable(qmi_transport_test
        tests/qmi_transport_test.cpp
    )
    target_link_libraries(qmi_transport_test PRIVATE j5qmi_core)
    add_test(NAME qmi_transport_test COMMAND qmi_transport_test)
endif()
