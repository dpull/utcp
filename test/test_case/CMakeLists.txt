get_filename_component(CURRENT_SOURCE_DIR_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)
set(LIB_NAME ${CURRENT_SOURCE_DIR_NAME}) 

file(GLOB_RECURSE SOURCE_FILES 
    *.h
    *.cpp
    *.cc
    *.c
)

include_directories(${GTEST_INCLUDE_DIRS})

include_directories(${CMAKE_SOURCE_DIR})
add_executable(${LIB_NAME} ${SOURCE_FILES})
target_link_libraries(${LIB_NAME} utcp)

target_link_libraries(${LIB_NAME} ${GTEST_LIBRARIES})
gtest_add_tests(TARGET ${LIB_NAME} TEST_LIST utcp_test_suite)
set_tests_properties(${utcp_test_suite} PROPERTIES RUN_SERIAL TRUE)