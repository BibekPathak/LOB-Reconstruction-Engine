include(GoogleTest)

add_executable(test_smoke test_smoke.cpp)
target_link_libraries(test_smoke PRIVATE lob_core gtest_main)
gtest_discover_tests(test_smoke)

add_executable(test_types test_types.cpp)
target_link_libraries(test_types PRIVATE lob_core gtest_main)
gtest_discover_tests(test_types)
