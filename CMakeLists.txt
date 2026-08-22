cmake_minimum_required(VERSION 3.28...3.30)

project(obs-virtual-camera-delay VERSION 0.1.0 LANGUAGES C)

add_library(${PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE OBS::libobs)

target_sources(${PROJECT_NAME} PRIVATE
    src/plugin-main.c
    src/virtual-camera-delay.c
    src/virtual-camera-delay.h
)

set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

if(COMMAND set_target_properties_plugin)
    set_target_properties_plugin(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ${PROJECT_NAME})
endif()
