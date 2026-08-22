cmake_minimum_required(VERSION 3.28...3.30)

project(obs-virtual-camera-delay VERSION 0.1.0 LANGUAGES CXX)

add_library(${PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE OBS::libobs OBS::obs-frontend-api)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

target_sources(${PROJECT_NAME} PRIVATE
    src/plugin-main.cpp
    src/delayed-virtual-camera-output.cpp
    src/delayed-virtual-camera-output.hpp
    src/virtual-camera-delay.cpp
    src/virtual-camera-delay.hpp
    src/virtual-camera-delay-controller.cpp
    src/virtual-camera-delay-controller.hpp
)

set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

if(COMMAND set_target_properties_plugin)
    set_target_properties_plugin(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
endif()
