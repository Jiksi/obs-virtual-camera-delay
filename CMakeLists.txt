cmake_minimum_required(VERSION 3.28...3.30)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PROJECT_VERSION_STRING
     LIMIT_COUNT 1)
project(obs-virtual-camera-delay VERSION ${PROJECT_VERSION_STRING} LANGUAGES CXX)

add_library(${PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE OBS::libobs OBS::obs-frontend-api)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

target_sources(${PROJECT_NAME} PRIVATE
    src/plugin-main.cpp
    src/delayed-virtual-camera-output.cpp
    src/delayed-virtual-camera-output.hpp
    src/frame-buffer.cpp
    src/frame-buffer.hpp
    src/virtual-camera-delay.cpp
    src/virtual-camera-delay.hpp
    src/virtual-camera-delay-controller.cpp
    src/virtual-camera-delay-controller.hpp
)

include(CTest)
if(BUILD_TESTING)
    add_executable(frame-buffer-test
        tests/frame-buffer-test.cpp
        src/frame-buffer.cpp
        src/frame-buffer.hpp)
    target_include_directories(frame-buffer-test PRIVATE src)
    target_compile_features(frame-buffer-test PRIVATE cxx_std_17)
    add_test(NAME frame-buffer COMMAND frame-buffer-test)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

if(COMMAND set_target_properties_plugin)
    set_target_properties_plugin(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
endif()
