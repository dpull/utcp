cmake_minimum_required(VERSION 3.12)
set(CMAKE_SUPPRESS_REGENERATION TRUE CACHE BOOL "Disable Zero Check Project")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(PROJ_NAME "utcp")
project(${PROJ_NAME})

include(Common.cmake)

add_subdirectory(utcp)
add_subdirectory(abstract)
add_subdirectory(sample)
add_subdirectory(test)
