cmake_minimum_required(VERSION 3.20)
project(obs-virtual-camera-delay-tests LANGUAGES CXX)

enable_testing()
add_executable(frame-buffer-test
    frame-buffer-test.cpp
    ../src/frame-buffer.cpp
    ../src/frame-buffer.hpp)
target_include_directories(frame-buffer-test PRIVATE ../src)
target_compile_features(frame-buffer-test PRIVATE cxx_std_17)
add_test(NAME frame-buffer COMMAND frame-buffer-test)
