include(GoogleTest)
enable_testing()

# googletest
set(gtest_force_shared_crt ON CACHE BOOL "")
add_subdirectory(googletest)
set(GTEST_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/googletest/googletest/include)
set(GTEST_LIBRARIES gtest gtest_main)
set_property(TARGET gtest gtest_main gmock gmock_main PROPERTY FOLDER "googletest")

add_subdirectory(test_case)