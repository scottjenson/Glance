# Unit tests for the parts of Glance with no KWin in them.
add_executable(geometry_test geometry_test.cpp)
target_link_libraries(geometry_test PRIVATE glance-core Qt6::Test)
add_test(NAME geometry COMMAND geometry_test)
