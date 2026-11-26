cmake_minimum_required(VERSION 3.16)
# version: version.txt written by the CI (0.1.<build>); locally "0.1.0-dev"
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/version.txt")
    set(PROJECT_VER "0.1.0-dev")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(amuletos)
