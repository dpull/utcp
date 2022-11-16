get_filename_component(CURRENT_SOURCE_DIR_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
set(APP_NAME ${CURRENT_SOURCE_DIR_NAME}) # 工程名, 默认文件夹名

file(GLOB SOURCE_FILES 
    *.h
    *.cpp
    *.cc
    *.c
)

if(WIN32)
file(GLOB PLATFORM_SOURCE_FILES
    platform/*.cpp
    platform/*.c
    platform/*.h
)
add_definitions(-D_WINSOCK_DEPRECATED_NO_WARNINGS)
endif(WIN32)

include_directories(${CMAKE_SOURCE_DIR})

add_executable(${APP_NAME} ${SOURCE_FILES} ${PLATFORM_SOURCE_FILES})
target_link_libraries(${APP_NAME} utcp)

if(APPLE)
    target_link_libraries(${APP_NAME} "-framework CoreFoundation")
endif(APPLE)

if(WIN32)
    target_link_libraries(${APP_NAME} "ws2_32.lib")
endif(WIN32)

if(LINUX)
    target_link_libraries(${APP_NAME} "pthread")
endif(LINUX)